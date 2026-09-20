import {
  BoxGeometry,
  type BufferGeometry,
  Color,
  Euler,
  Group,
  InstancedMesh,
  Matrix4,
  Quaternion,
  RingGeometry,
  type Scene,
  SphereGeometry,
  TorusGeometry,
  Vector3,
} from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

import type { ViewEntity } from '../net/state.ts';
import type { RenderMaterials } from './materials.ts';
import { PARTICLE_EMITTER, type ParticleSystem } from './particles.ts';
import type { ChargeWarning } from './sheepModel.ts';

export const TRACER_CAPACITY = 32;
export const TRACER_LOCAL_MS = 45;
export const TRACER_REMOTE_MS = 90;
export const ELITE_BOLT_CAPACITY = 24;
export const ELITE_BOLT_ENTITY_KIND = 2;
export const ELITE_BOLT_SPIN_HZ = 1.6;
export const ELITE_BOLT_GLOW_SCALE = 1.6;
export const HIT_FLASH_MS = 150;
export const HIT_MARKER_MS = 140;
export const SHAKE_DECAY_PER_SECOND = 7.5;
export const SHAKE_MAX_M = 0.22;
export const CHARGE_RING_CAPACITY = 12;
export const CHARGE_RING_LIFETIME_MS = 1150;
export const CHARGE_RING_MISS_MS = 220;
export const CHARGE_RING_MISS_STEP_MS = 16;
export const VIGNETTE_BASE_OPACITY = 0.12;
export const VIGNETTE_LOW_HP_OPACITY = 0.35;
export const VIGNETTE_PULSE = 0.06;
export const BLOOD_MAX_OPACITY = 0.55;

export interface EffectsOptions {
  readonly isReduceMotion: () => boolean;
  readonly onChargeWindup?: (id: number) => void;
}

export interface EliteBoltSource {
  forEachVisible(cb: (entity: ViewEntity) => void): void;
}

export interface Effects {
  readonly root: Group;
  spawnTracer(
    fromX: number,
    fromY: number,
    fromZ: number,
    toX: number,
    toY: number,
    toZ: number,
    local: boolean,
  ): void;
  spawnImpact(x: number, y: number, z: number): void;
  spawnMuzzleFlash(x: number, y: number, z: number, dirX: number, dirY: number, dirZ: number): void;
  spawnDeathDebris(x: number, y: number, z: number): void;
  showHitMarker(headshot: boolean): void;
  pulseHitFlash(): void;
  setBlood(amount: number): void;
  setBerserk(active: boolean): void;
  triggerShake(amount: number): void;
  setChargeWarnings(warnings: readonly ChargeWarning[]): void;
  syncEliteBolts(source: EliteBoltSource): void;
  readonly shakeX: number;
  readonly shakeY: number;
  readonly activeChargeRings: number;
  readonly activeEliteBolts: number;
  update(dtMs: number): void;
  dispose(): void;
}

interface TracerSlot {
  leftMs: number;
  totalMs: number;
  local: boolean;
}

interface ChargeRing {
  x: number;
  z: number;
  radiusM: number;
  ageMs: number;
  missMs: number;
}

function overlay(host: HTMLElement, className: string): HTMLDivElement {
  const element = document.createElement('div');
  element.className = className;
  host.append(element);
  return element;
}

function setOpacity(element: HTMLElement, value: number, previous: { value: number }): void {
  const clamped = value < 0 ? 0 : value > 1 ? 1 : value;
  if (Math.abs(clamped - previous.value) < 0.01) return;
  previous.value = clamped;
  element.style.opacity = clamped.toFixed(3);
}

function createEliteBoltGeometry(): BufferGeometry {
  const hook = new TorusGeometry(0.115, 0.038, 5, 14, Math.PI * 1.3);
  hook.rotateZ(Math.PI * 0.15);
  const dot = new SphereGeometry(0.05, 8, 6);
  dot.translate(0, -0.26, 0);
  const merged = mergeGeometries([hook, dot]);
  hook.dispose();
  dot.dispose();
  return merged ?? new SphereGeometry(0.06, 6, 4);
}

export function createEffects(
  scene: Scene,
  particles: ParticleSystem,
  materials: RenderMaterials,
  hudHost: HTMLElement,
  options: EffectsOptions,
): Effects {
  const root = new Group();
  root.name = 'effects';
  scene.add(root);

  const tracerGeometry = new BoxGeometry(0.012, 0.012, 1);
  const tracerMesh = new InstancedMesh(tracerGeometry, materials.tracer, TRACER_CAPACITY);
  tracerMesh.name = 'tracers';
  tracerMesh.frustumCulled = false;
  root.add(tracerMesh);
  const tracers: TracerSlot[] = [];
  for (let i = 0; i < TRACER_CAPACITY; i += 1) tracers.push({ leftMs: 0, totalMs: 1, local: true });
  let tracerCursor = 0;
  const tracerMatrix = new Matrix4();
  const tracerStart = new Vector3();
  const tracerDirection = new Vector3();
  const forward = new Vector3(0, 0, 1);
  const tracerOrientation = new Quaternion();
  const tracerColor = new Color();
  const tracerScale = new Vector3();
  const zeroMatrix = new Matrix4().makeScale(0, 0, 0);
  for (let i = 0; i < TRACER_CAPACITY; i += 1) {
    tracerMesh.setMatrixAt(i, zeroMatrix);
    tracerMesh.setColorAt(i, tracerColor.setRGB(0, 0, 0));
  }
  tracerMesh.count = 0;

  const ringGeometry = new RingGeometry(0.42, 0.5, 22);
  const chargeMesh = new InstancedMesh(ringGeometry, materials.chargeRing, CHARGE_RING_CAPACITY);
  chargeMesh.name = 'charge-warnings';
  chargeMesh.frustumCulled = false;
  root.add(chargeMesh);
  const rings = new Map<number, ChargeRing>();
  const ringMatrix = new Matrix4();
  const flatEuler = new Euler(-Math.PI / 2, 0, 0);
  const ringQuaternion = new Quaternion().setFromEuler(flatEuler);
  const ringPosition = new Vector3();
  const ringScale = new Vector3();
  const ringColor = new Color();
  for (let i = 0; i < CHARGE_RING_CAPACITY; i += 1) {
    chargeMesh.setMatrixAt(i, zeroMatrix);
    chargeMesh.setColorAt(i, ringColor.setRGB(0, 0, 0));
  }
  chargeMesh.count = 0;
  let ringSlots = 0;

  const boltGeometry = createEliteBoltGeometry();
  const boltMesh = new InstancedMesh(boltGeometry, materials.tracer, ELITE_BOLT_CAPACITY);
  boltMesh.name = 'elite-bolts';
  boltMesh.frustumCulled = false;
  root.add(boltMesh);
  const boltMatrix = new Matrix4();
  const boltPosition = new Vector3();
  const boltScale = new Vector3();
  const boltYawQuaternion = new Quaternion();
  const boltSpinQuaternion = new Quaternion();
  const boltQuaternion = new Quaternion();
  const boltColor = new Color();
  const upAxis = new Vector3(0, 1, 0);
  const spinAxis = new Vector3(0, 0, 1);
  for (let i = 0; i < ELITE_BOLT_CAPACITY; i += 1) boltMesh.setMatrixAt(i, zeroMatrix);
  boltMesh.count = 0;
  let boltPhase = 0;

  const hitMarker = overlay(hudHost, 'fx-hit-marker');
  const vignette = overlay(hudHost, 'fx-vignette');
  const blood = overlay(hudHost, 'fx-blood');
  const berserk = overlay(hudHost, 'fx-berserk');
  const hitFlash = overlay(hudHost, 'fx-hit-flash');

  const vignetteOpacity = { value: -1 };
  const bloodOpacity = { value: -1 };
  const hitFlashOpacity = { value: -1 };
  const berserkOpacity = { value: -1 };

  let hitFlashLeftMs = 0;
  let hitMarkerLeftMs = 0;
  let bloodAmount = 0;
  let berserkActive = false;
  let berserkPhase = 0;
  let shake = 0;
  let shakePhase = 0;
  let vignettePhase = 0;

  return {
    root,
    spawnTracer(fromX, fromY, fromZ, toX, toY, toZ, local): void {
      const slotIndex = tracerCursor % TRACER_CAPACITY;
      tracerCursor += 1;
      const slot = tracers[slotIndex];
      if (slot === undefined) return;
      tracerStart.set(fromX, fromY, fromZ);
      tracerDirection.set(toX - fromX, toY - fromY, toZ - fromZ);
      const length = tracerDirection.length();
      if (length <= 0.001) return;
      tracerDirection.divideScalar(length);
      tracerOrientation.setFromUnitVectors(forward, tracerDirection);
      tracerScale.set(1, 1, length);
      tracerMatrix.compose(
        tracerStart.addScaledVector(tracerDirection, length / 2),
        tracerOrientation,
        tracerScale,
      );
      tracerMesh.setMatrixAt(slotIndex, tracerMatrix);
      tracerMesh.count = Math.min(TRACER_CAPACITY, tracerCursor);
      tracerMesh.setColorAt(
        slotIndex,
        tracerColor.setRGB(local ? 1.7 : 1.1, local ? 1.6 : 0.7, local ? 1.4 : 0.42),
      );
      slot.leftMs = local ? TRACER_LOCAL_MS : TRACER_REMOTE_MS;
      slot.totalMs = slot.leftMs;
      slot.local = local;
      tracerMesh.instanceMatrix.needsUpdate = true;
      if (tracerMesh.instanceColor !== null) tracerMesh.instanceColor.needsUpdate = true;
    },
    spawnImpact(x, y, z): void {
      particles.emit(PARTICLE_EMITTER.bullet, x, y, z, 1, 0, 0.35, 0);
      particles.emit(PARTICLE_EMITTER.spark, x, y, z, 5, 0, 0.2, 0);
    },
    spawnMuzzleFlash(x, y, z, dirX, dirY, dirZ): void {
      particles.emit(PARTICLE_EMITTER.muzzle, x, y, z, 4, dirX, dirY, dirZ);
    },
    spawnDeathDebris(x, y, z): void {
      particles.emit(PARTICLE_EMITTER.debris, x, y, z, 14, 0, 0.4, 0);
      particles.emit(PARTICLE_EMITTER.wool, x, y, z, 18, 0, 0.5, 0);
    },
    showHitMarker(headshot): void {
      hitMarker.textContent = headshot ? '✕' : '✳';
      hitMarker.classList.toggle('fx-hit-marker-headshot', headshot);
      hitMarkerLeftMs = HIT_MARKER_MS;
    },
    pulseHitFlash(): void {
      hitFlashLeftMs = HIT_FLASH_MS;
    },
    setBlood(amount): void {
      bloodAmount = amount < 0 ? 0 : amount > 1 ? 1 : amount;
    },
    setBerserk(active): void {
      berserkActive = active;
    },
    triggerShake(amount): void {
      if (options.isReduceMotion()) return;
      const next = shake + amount;
      shake = next > SHAKE_MAX_M ? SHAKE_MAX_M : next;
    },
    setChargeWarnings(warnings): void {
      const seen = new Set<number>();
      for (const warning of warnings) {
        seen.add(warning.id);
        const existing = rings.get(warning.id);
        if (existing === undefined) {
          if (rings.size >= CHARGE_RING_CAPACITY) continue;
          rings.set(warning.id, {
            x: warning.x,
            z: warning.z,
            radiusM: warning.radiusM,
            ageMs: 0,
            missMs: 0,
          });
          options.onChargeWindup?.(warning.id);
          continue;
        }
        existing.x = warning.x;
        existing.z = warning.z;
        existing.radiusM = warning.radiusM;
        existing.missMs = 0;
      }
      for (const [id, ring] of rings) {
        if (seen.has(id)) continue;
        ring.missMs += CHARGE_RING_MISS_STEP_MS;
      }
    },
    syncEliteBolts(source): void {
      const spin = options.isReduceMotion() ? 0 : boltPhase;
      const pulse = options.isReduceMotion() ? 1 : 1 + Math.sin(boltPhase * 3.1) * 0.08;
      boltColor.copy(materials.tracer.color).multiplyScalar(ELITE_BOLT_GLOW_SCALE);
      boltSpinQuaternion.setFromAxisAngle(spinAxis, spin);
      let count = 0;
      source.forEachVisible((entity) => {
        if (count >= ELITE_BOLT_CAPACITY) return;
        if (entity.kind !== ELITE_BOLT_ENTITY_KIND) return;
        boltYawQuaternion.setFromAxisAngle(upAxis, entity.yaw);
        boltQuaternion.copy(boltYawQuaternion).multiply(boltSpinQuaternion);
        boltPosition.set(entity.pos.x, entity.pos.y, entity.pos.z);
        boltScale.setScalar(pulse);
        boltMatrix.compose(boltPosition, boltQuaternion, boltScale);
        boltMesh.setMatrixAt(count, boltMatrix);
        boltMesh.setColorAt(count, boltColor);
        count += 1;
      });
      boltMesh.count = count;
      boltMesh.instanceMatrix.needsUpdate = true;
      if (boltMesh.instanceColor !== null) boltMesh.instanceColor.needsUpdate = true;
    },
    get shakeX(): number {
      return options.isReduceMotion() ? 0 : Math.sin(shakePhase) * shake;
    },
    get shakeY(): number {
      return options.isReduceMotion() ? 0 : Math.cos(shakePhase * 1.37) * shake * 0.6;
    },
    get activeChargeRings(): number {
      return rings.size;
    },
    get activeEliteBolts(): number {
      return boltMesh.count;
    },
    update(dtMs): void {
      particles.update(dtMs);
      vignettePhase += dtMs / 1000;
      berserkPhase += dtMs / 1000;
      boltPhase += (dtMs / 1000) * Math.PI * 2 * ELITE_BOLT_SPIN_HZ;
      shakePhase += (dtMs / 1000) * 42;
      if (shake > 0) shake = Math.max(0, shake - (dtMs / 1000) * SHAKE_DECAY_PER_SECOND);
      if (hitFlashLeftMs > 0) hitFlashLeftMs = Math.max(0, hitFlashLeftMs - dtMs);
      if (hitMarkerLeftMs > 0) {
        hitMarkerLeftMs = Math.max(0, hitMarkerLeftMs - dtMs);
      }
      hitMarker.style.opacity = (hitMarkerLeftMs / HIT_MARKER_MS).toFixed(2);

      let tracerDirty = false;
      for (let i = 0; i < TRACER_CAPACITY; i += 1) {
        const slot = tracers[i];
        if (slot === undefined || slot.leftMs <= 0) continue;
        tracerDirty = true;
        slot.leftMs -= dtMs;
        if (slot.leftMs <= 0) {
          slot.leftMs = 0;
          tracerMesh.setMatrixAt(i, zeroMatrix);
          continue;
        }
        const fade = slot.leftMs / slot.totalMs;
        const base = slot.local ? 1.7 : 1.1;
        tracerMesh.setColorAt(
          i,
          tracerColor.setRGB(base * fade, (base - 0.1) * fade, (base - 0.4) * fade),
        );
      }
      if (tracerDirty) {
        tracerMesh.instanceMatrix.needsUpdate = true;
        if (tracerMesh.instanceColor !== null) tracerMesh.instanceColor.needsUpdate = true;
      }

      for (const [id, ring] of rings) {
        ring.ageMs += dtMs;
        if (
          ring.missMs > CHARGE_RING_MISS_MS ||
          ring.ageMs > CHARGE_RING_LIFETIME_MS + CHARGE_RING_MISS_MS
        ) {
          rings.delete(id);
        }
      }
      ringSlots = 0;
      for (const ring of rings.values()) {
        if (ringSlots >= CHARGE_RING_CAPACITY) break;
        const progress = Math.min(1, ring.ageMs / CHARGE_RING_LIFETIME_MS);
        const size = ring.radiusM * (0.35 + progress * 1.1);
        ringPosition.set(ring.x, 0.03, ring.z);
        ringScale.set(size, size, size);
        ringMatrix.compose(ringPosition, ringQuaternion, ringScale);
        chargeMesh.setMatrixAt(ringSlots, ringMatrix);
        chargeMesh.setColorAt(
          ringSlots,
          ringColor.setHex(materials.chargeRing.color.getHex()).multiplyScalar(1 - progress * 0.8),
        );
        ringSlots += 1;
      }
      chargeMesh.count = ringSlots;
      chargeMesh.instanceMatrix.needsUpdate = true;
      if (chargeMesh.instanceColor !== null) chargeMesh.instanceColor.needsUpdate = true;

      const vignetteBase = VIGNETTE_BASE_OPACITY + bloodAmount * VIGNETTE_LOW_HP_OPACITY;
      const vignetteTarget = options.isReduceMotion()
        ? vignetteBase
        : vignetteBase + Math.sin(vignettePhase * 0.7) * VIGNETTE_PULSE;
      setOpacity(vignette, vignetteTarget, vignetteOpacity);
      setOpacity(blood, bloodAmount * BLOOD_MAX_OPACITY, bloodOpacity);
      setOpacity(hitFlash, hitFlashLeftMs / HIT_FLASH_MS, hitFlashOpacity);
      setOpacity(
        berserk,
        berserkActive ? 0.5 + Math.sin(berserkPhase * 5) * 0.12 : 0,
        berserkOpacity,
      );
    },
    dispose(): void {
      scene.remove(root);
      tracerGeometry.dispose();
      ringGeometry.dispose();
      boltGeometry.dispose();
      tracerMesh.dispose();
      chargeMesh.dispose();
      boltMesh.dispose();
      rings.clear();
      hitMarker.remove();
      vignette.remove();
      blood.remove();
      berserk.remove();
      hitFlash.remove();
    },
  };
}
