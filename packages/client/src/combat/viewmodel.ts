import {
  BoxGeometry,
  ConeGeometry,
  Group,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  PointLight,
  type PerspectiveCamera,
} from 'three';

import {
  MS_PER_SECOND,
  RECOIL_PITCH_PER_SHOT_DEG,
  RECOIL_YAW_JITTER_DEG,
  signedJitter,
  WEAPONS,
  WEAPON_SLOT_ORDER,
} from '@ac/shared';

export const VIEW_MODEL_STATE = Object.freeze({
  idle: 'idle',
  firing: 'firing',
  reloading: 'reloading',
  switching: 'switching',
} as const);

export type ViewModelState = (typeof VIEW_MODEL_STATE)[keyof typeof VIEW_MODEL_STATE];

export const MUZZLE_FLASH_MS = 45;
export const SWITCH_DIP_MS = 260;
export const RECOIL_RECOVERY_PER_SECOND = 9;
export const RELOAD_DIP_DEG = 22;

export interface ViewModel {
  readonly root: Group;
  readonly recoilPitchDeg: number;
  readonly recoilYawDeg: number;
  setWeapon(slot: 0 | 1 | 2): void;
  setReloadProgress(ratio: number): void;
  setDowned(down: boolean): void;
  triggerFire(seq: number, spreadDeg: number): void;
  update(dtMs: number): void;
  dispose(): void;
}

export function createViewModel(camera: PerspectiveCamera): ViewModel {
  const root = new Group();
  const bodyMaterial = new MeshStandardMaterial({
    color: 0x2f3540,
    roughness: 0.6,
    metalness: 0.35,
  });
  const armMaterial = new MeshStandardMaterial({ color: 0xc99a72, roughness: 0.9 });
  const flashMaterial = new MeshBasicMaterial({ color: 0xffd479, transparent: true, opacity: 0 });

  const barrels: Mesh[] = [];
  const magazines: Mesh[] = [];
  for (let slot = 0; slot < WEAPON_SLOT_ORDER.length; slot += 1) {
    const weapon = new Group();
    const body = new Mesh(new BoxGeometry(0.09, 0.11, 0.34), bodyMaterial);
    body.position.set(0, 0, -0.16);
    const barrel = new Mesh(new BoxGeometry(0.045, 0.045, 0.3), bodyMaterial);
    barrel.position.set(0, 0.02, -0.44);
    const magazine = new Mesh(new BoxGeometry(0.06, 0.12, 0.07), bodyMaterial);
    magazine.position.set(0, -0.11, -0.06);
    const grip = new Mesh(new BoxGeometry(0.05, 0.13, 0.06), bodyMaterial);
    grip.position.set(0, -0.11, 0.06);
    weapon.add(body, barrel, magazine, grip);
    weapon.visible = slot === 0;
    barrels.push(barrel);
    magazines.push(magazine);
    root.add(weapon);
  }

  const leftArm = new Mesh(new BoxGeometry(0.07, 0.07, 0.26), armMaterial);
  leftArm.position.set(-0.17, -0.12, -0.16);
  const rightArm = new Mesh(new BoxGeometry(0.07, 0.07, 0.26), armMaterial);
  rightArm.position.set(0.09, -0.13, -0.1);
  root.add(leftArm, rightArm);

  const flash = new Mesh(new ConeGeometry(0.07, 0.16, 6), flashMaterial);
  flash.rotation.x = Math.PI / 2;
  flash.position.set(0, 0.02, -0.62);
  root.add(flash);
  const light = new PointLight(0xffc46b, 0, 3.2, 2);
  light.position.copy(flash.position);
  root.add(light);

  root.position.set(0.16, -0.19, 0);
  camera.add(root);

  let activeSlot: 0 | 1 | 2 = 0;
  let state: ViewModelState = VIEW_MODEL_STATE.idle;
  let flashLeftMs = 0;
  let switchLeftMs = 0;
  let reloadRatio = 0;
  let downed = false;
  let recoilPitch = 0;
  let recoilYaw = 0;
  let kick = 0;

  function currentMagazineMesh(): Mesh | undefined {
    return magazines[activeSlot];
  }

  const model: ViewModel = {
    root,
    get recoilPitchDeg(): number {
      return recoilPitch;
    },
    get recoilYawDeg(): number {
      return recoilYaw;
    },
    setWeapon(slot: 0 | 1 | 2): void {
      if (slot === activeSlot) return;
      activeSlot = slot;
      for (let i = 0; i < root.children.length; i += 1) {
        const child = root.children[i];
        if (child !== undefined && child.type === 'Group' && i < WEAPON_SLOT_ORDER.length) {
          child.visible = i === slot;
        }
      }
      switchLeftMs = SWITCH_DIP_MS;
      state = VIEW_MODEL_STATE.switching;
    },
    setReloadProgress(ratio: number): void {
      reloadRatio = ratio < 0 ? 0 : ratio > 1 ? 1 : ratio;
      state = reloadRatio > 0 ? VIEW_MODEL_STATE.reloading : VIEW_MODEL_STATE.idle;
    },
    setDowned(down: boolean): void {
      downed = down;
    },
    triggerFire(seq: number, spreadDeg: number): void {
      const def = WEAPONS[WEAPON_SLOT_ORDER[activeSlot] ?? 'pistol'];
      void def;
      flashLeftMs = MUZZLE_FLASH_MS;
      state = VIEW_MODEL_STATE.firing;
      kick = 1;
      recoilPitch += RECOIL_PITCH_PER_SHOT_DEG;
      recoilYaw += signedJitter(seq, 0x2b1) * RECOIL_YAW_JITTER_DEG * spreadDeg;
    },
    update(dtMs: number): void {
      const dtSeconds = dtMs / MS_PER_SECOND;
      const recovery = RECOIL_RECOVERY_PER_SECOND * dtSeconds * (1 + recoilPitch * 0.6);
      recoilPitch = recoilPitch > recovery ? recoilPitch - recovery : 0;
      const yawRecovery = RECOIL_RECOVERY_PER_SECOND * dtSeconds;
      recoilYaw =
        Math.abs(recoilYaw) > yawRecovery ? recoilYaw - Math.sign(recoilYaw) * yawRecovery : 0;

      if (flashLeftMs > 0) flashLeftMs = Math.max(0, flashLeftMs - dtMs);
      if (switchLeftMs > 0) {
        switchLeftMs = Math.max(0, switchLeftMs - dtMs);
        if (switchLeftMs === 0) state = VIEW_MODEL_STATE.idle;
      }
      kick = kick > 0 ? Math.max(0, kick - dtSeconds * 7) : 0;

      const magazine = currentMagazineMesh();
      if (magazine !== undefined) {
        magazine.position.y =
          -0.11 - (reloadRatio > 0 ? 0.14 * Math.sin(Math.PI * reloadRatio) : 0);
      }

      const flashScale = flashLeftMs / MUZZLE_FLASH_MS;
      flashMaterial.opacity = flashScale;
      flash.visible = flashLeftMs > 0;
      light.intensity = flashScale * 2.4;

      const dip = switchLeftMs > 0 ? Math.sin((switchLeftMs / SWITCH_DIP_MS) * Math.PI) : 0;
      const reloadDip = reloadRatio > 0 ? Math.sin(Math.PI * reloadRatio) * RELOAD_DIP_DEG : 0;
      const downedDip = downed ? 26 : 0;
      root.rotation.x = -reloadDip / 57.3 - downedDip / 57.3;
      root.rotation.z = recoilYaw / 57.3 + dip * 0.35;
      root.position.set(0.16, -0.19 - kick * 0.012 - dip * 0.09 - downedDip * 0.004, kick * 0.035);
      flash.position.set(0, 0.02, -0.62 - kick * 0.01);
      void state;
    },
    dispose(): void {
      root.removeFromParent();
      root.traverse((child) => {
        const mesh = child as Mesh;
        if (mesh.geometry !== undefined) mesh.geometry.dispose();
      });
      bodyMaterial.dispose();
      armMaterial.dispose();
      flashMaterial.dispose();
      for (const barrel of barrels) barrel.geometry.dispose();
      light.dispose();
    },
  };
  return model;
}
