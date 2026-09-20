import {
  BoxGeometry,
  type BufferGeometry,
  CapsuleGeometry,
  ConeGeometry,
  DirectionalLight,
  Group,
  HemisphereLight,
  Mesh,
  PerspectiveCamera,
  Scene,
} from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

import type { RenderMaterials } from './materials.ts';

export const VIEWMODEL_FOV_DEG = 62;
export const VIEWMODEL_NEAR_M = 0.01;
export const VIEWMODEL_FAR_M = 12;
export const VIEWMODEL_BASE_POSITION = Object.freeze({ x: 0.17, y: -0.19, z: -0.02 });
export const SWITCH_DIP_MS = 260;
export const MUZZLE_FLASH_MS = 45;
export const RECOIL_IMPULSE = 1;
export const RECOIL_DECAY_PER_SECOND = 7.5;
export const BREATH_HZ = 0.42;
export const WALK_HZ = 3.4;
export const WALK_PHASE_PER_METRE = 0.55;
export const RELOAD_DIP_RAD = 0.42;
export const RELOAD_ROLL_RAD = 0.3;
export const DOWNED_DIP_RAD = 0.46;

export interface ViewModelModel {
  readonly scene: Scene;
  readonly camera: PerspectiveCamera;
  readonly root: Group;
  readonly materialCount: number;
  setViewport(width: number, height: number): void;
  setFov(fov: number): void;
  setWeapon(slot: 0 | 1 | 2): void;
  setMoveAmount(amount: number): void;
  setSpreadDeg(spreadDeg: number): void;
  setReloadRatio(ratio: number): void;
  setDowned(down: boolean): void;
  triggerFire(): void;
  update(dtMs: number): void;
  dispose(): void;
}

function boxPart(
  sizeX: number,
  sizeY: number,
  sizeZ: number,
  x: number,
  y: number,
  z: number,
): BufferGeometry {
  const geometry = new BoxGeometry(sizeX, sizeY, sizeZ);
  geometry.translate(x, y, z);
  return geometry;
}

function mergeParts(parts: readonly BufferGeometry[]): BufferGeometry {
  const merged = mergeGeometries(parts.slice());
  for (const part of parts) part.dispose();
  return merged ?? new BoxGeometry(0.05, 0.05, 0.05);
}

interface WeaponShape {
  readonly parts: readonly BufferGeometry[];
  readonly magazine: BufferGeometry;
  readonly muzzleZ: number;
}

function weaponShape(slot: 0 | 1 | 2): WeaponShape {
  if (slot === 1) {
    return {
      parts: [
        boxPart(0.085, 0.12, 0.44, 0, 0, -0.2),
        boxPart(0.04, 0.04, 0.46, 0, 0.028, -0.6),
        boxPart(0.07, 0.1, 0.24, 0, -0.03, 0.14),
        boxPart(0.06, 0.05, 0.16, 0, 0.09, -0.18),
      ],
      magazine: boxPart(0.06, 0.18, 0.08, 0, -0.16, -0.04),
      muzzleZ: -0.85,
    };
  }
  if (slot === 2) {
    return {
      parts: [
        boxPart(0.1, 0.13, 0.36, 0, 0, -0.18),
        boxPart(0.075, 0.075, 0.52, 0, 0.025, -0.58),
        boxPart(0.085, 0.12, 0.26, 0, -0.035, 0.14),
        boxPart(0.05, 0.05, 0.14, 0, 0.09, -0.1),
      ],
      magazine: boxPart(0.09, 0.08, 0.18, 0, -0.1, -0.26),
      muzzleZ: -0.88,
    };
  }
  return {
    parts: [
      boxPart(0.09, 0.11, 0.28, 0, 0, -0.14),
      boxPart(0.05, 0.05, 0.24, 0, 0.024, -0.38),
      boxPart(0.055, 0.13, 0.07, 0, -0.12, 0.02),
    ],
    magazine: boxPart(0.055, 0.13, 0.07, 0, -0.12, -0.02),
    muzzleZ: -0.54,
  };
}

export function reloadDipRad(ratio: number): number {
  if (!(ratio > 0)) return 0;
  return Math.sin(Math.PI * Math.min(1, ratio)) * RELOAD_DIP_RAD;
}

export function reloadRollRad(ratio: number): number {
  if (!(ratio > 0)) return 0;
  return Math.sin(Math.min(1, ratio) * Math.PI * 1.4) * RELOAD_ROLL_RAD;
}

export function switchDip(msLeft: number): number {
  if (!(msLeft > 0)) return 0;
  return Math.sin((msLeft / SWITCH_DIP_MS) * Math.PI);
}

export function walkSway(phase: number, amount: number): number {
  return Math.sin(phase) * 0.022 * amount;
}

export function createViewModelModel(materials: RenderMaterials): ViewModelModel {
  const scene = new Scene();
  scene.name = 'viewmodel';
  const camera = new PerspectiveCamera(VIEWMODEL_FOV_DEG, 1, VIEWMODEL_NEAR_M, VIEWMODEL_FAR_M);
  camera.position.set(0, 0, 0);
  camera.updateProjectionMatrix();

  scene.add(new HemisphereLight(0xdfe9ff, 0x30343c, 1.4));
  const key = new DirectionalLight(0xfff1d6, 1.5);
  key.position.set(-0.6, 1.2, 0.8);
  scene.add(key);

  const root = new Group();
  root.name = 'viewmodel-root';
  root.position.set(
    VIEWMODEL_BASE_POSITION.x,
    VIEWMODEL_BASE_POSITION.y,
    VIEWMODEL_BASE_POSITION.z,
  );
  scene.add(root);

  const armGeometry = new CapsuleGeometry(0.05, 0.3, 4, 8);
  const leftArm = new Mesh(armGeometry, materials.viewmodelArm);
  leftArm.position.set(-0.16, -0.2, -0.24);
  leftArm.rotation.set(0.35, 0.2, -0.2);
  const rightArm = new Mesh(armGeometry, materials.viewmodelArm);
  rightArm.position.set(0.1, -0.22, -0.2);
  rightArm.rotation.set(0.3, -0.3, 0.25);
  root.add(leftArm, rightArm);

  const weapons: Group[] = [];
  const magazines: Mesh[] = [];
  const muzzleZ: number[] = [];
  const slots: readonly (0 | 1 | 2)[] = [0, 1, 2];
  for (const slot of slots) {
    const shape = weaponShape(slot);
    const group = new Group();
    group.name = 'viewmodel-weapon-' + String(slot);
    group.add(new Mesh(mergeParts(shape.parts), materials.viewmodelBody));
    const magazine = new Mesh(shape.magazine, materials.viewmodelBody);
    group.add(magazine);
    group.visible = slot === 0;
    weapons.push(group);
    magazines.push(magazine);
    muzzleZ.push(shape.muzzleZ);
    root.add(group);
  }

  const flashGeometry = new ConeGeometry(0.06, 0.16, 6);
  flashGeometry.rotateX(-Math.PI / 2);
  const flash = new Mesh(flashGeometry, materials.tracer);
  flash.visible = false;
  root.add(flash);

  let activeSlot: 0 | 1 | 2 = 0;
  let moveAmount = 0;
  let spreadDeg = 0;
  let reloadRatio = 0;
  let downed = false;
  let recoil = 0;
  let flashLeftMs = 0;
  let switchLeftMs = 0;
  let breathPhase = 0;
  let walkPhase = 0;

  return {
    scene,
    camera,
    root,
    materialCount: 2,
    setViewport(width: number, height: number): void {
      camera.aspect = height === 0 ? 1 : width / height;
      camera.updateProjectionMatrix();
    },
    setFov(fov: number): void {
      if (camera.fov === fov) return;
      camera.fov = fov;
      camera.updateProjectionMatrix();
    },
    setWeapon(slot: 0 | 1 | 2): void {
      if (slot === activeSlot) return;
      activeSlot = slot;
      for (let i = 0; i < weapons.length; i += 1) {
        const weapon = weapons[i];
        if (weapon !== undefined) weapon.visible = i === slot;
      }
      switchLeftMs = SWITCH_DIP_MS;
    },
    setMoveAmount(amount: number): void {
      moveAmount = amount < 0 ? 0 : amount > 1 ? 1 : amount;
    },
    setSpreadDeg(value: number): void {
      spreadDeg = Number.isFinite(value) ? value : 0;
    },
    setReloadRatio(ratio: number): void {
      reloadRatio = ratio < 0 ? 0 : ratio > 1 ? 1 : ratio;
    },
    setDowned(down: boolean): void {
      downed = down;
    },
    triggerFire(): void {
      recoil = RECOIL_IMPULSE;
      flashLeftMs = MUZZLE_FLASH_MS;
    },
    update(dtMs: number): void {
      const dtSeconds = dtMs <= 0 ? 0 : dtMs / 1000;
      breathPhase += dtSeconds * Math.PI * 2 * BREATH_HZ;
      walkPhase += dtSeconds * Math.PI * 2 * WALK_HZ * (0.4 + moveAmount);
      recoil = recoil > 0 ? Math.max(0, recoil - dtSeconds * RECOIL_DECAY_PER_SECOND) : 0;
      if (flashLeftMs > 0) flashLeftMs = Math.max(0, flashLeftMs - dtMs);
      if (switchLeftMs > 0) switchLeftMs = Math.max(0, switchLeftMs - dtMs);

      const breath = Math.sin(breathPhase) * 0.006;
      const sway = walkSway(walkPhase, moveAmount);
      const dip = switchDip(switchLeftMs);
      const reloadDip = reloadDipRad(reloadRatio);
      const roll = reloadRollRad(reloadRatio);
      const downedDip = downed ? DOWNED_DIP_RAD : 0;
      const spreadKick = Math.min(spreadDeg, 5) * 0.004;

      root.position.set(
        VIEWMODEL_BASE_POSITION.x + sway * 0.5,
        VIEWMODEL_BASE_POSITION.y + breath + sway * 0.35 - dip * 0.22 - downedDip * 0.3,
        VIEWMODEL_BASE_POSITION.z + recoil * 0.05 + spreadKick,
      );
      root.rotation.set(
        -breath * 1.4 + recoil * 0.09 + reloadDip * 0.5 + dip * 0.5 + downedDip,
        sway * 0.6 + roll * 0.2,
        roll + sway * 0.25 + recoil * 0.02,
      );

      const magazine = magazines[activeSlot];
      if (magazine !== undefined) {
        magazine.position.y =
          reloadRatio > 0 ? -0.17 * Math.sin(Math.PI * Math.min(1, reloadRatio)) : 0;
      }

      const muzzle = muzzleZ[activeSlot] ?? -0.6;
      flash.position.set(0, 0.02, muzzle);
      flash.visible = flashLeftMs > 0 && reloadRatio === 0;
      flash.scale.setScalar(0.6 + (flashLeftMs / MUZZLE_FLASH_MS) * 0.8);
    },
    dispose(): void {
      scene.remove(root);
      root.traverse((object) => {
        if (object instanceof Mesh) object.geometry.dispose();
      });
      armGeometry.dispose();
      flashGeometry.dispose();
    },
  };
}
