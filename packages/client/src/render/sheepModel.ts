import {
  BoxGeometry,
  type BufferGeometry,
  BufferAttribute,
  Color,
  ConeGeometry,
  IcosahedronGeometry,
  InstancedBufferAttribute,
  InstancedMesh,
  Matrix4,
  PlaneGeometry,
  Quaternion,
  type Scene,
  Vector3,
} from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

import {
  EMBLEM_PLANE_KING_M,
  EMBLEM_PLANE_M,
  emblemEmissiveTint,
  emblemIntensity,
  emblemSmoothingStep,
} from './emblem.ts';
import type { RenderMaterials } from './materials.ts';
import { createChargeWarningPool } from './sheepInstancePool.ts';

export const SHEEP_FORM = Object.freeze({ grunt: 0, ram: 1, elite: 2, king: 3 } as const);
export const SHEEP_FORM_COUNT = 4;
export const SHEEP_FORM_CAPACITY = 64;
export const SHEEP_TOTAL_CAPACITY = SHEEP_FORM_CAPACITY * SHEEP_FORM_COUNT;
export const WOOL_CLUSTERS_PER_SHEEP = 12;
export const WOOL_CLUSTERS_GRUNT = 8;
export const KING_BODY_SCALE = 1.6;
export const SHEEP_FORM_SCALE: readonly number[] = Object.freeze([1, 1.06, 1.12, KING_BODY_SCALE]);
export const SHEEP_HORN_FORMS: readonly number[] = Object.freeze([SHEEP_FORM.ram, SHEEP_FORM.king]);
export const CHARGE_WARNING_RADIUS_M = 1.1;
/** O06：蓄力警告对象池容量（与可见羊上限一致）。 */
export const CHARGE_WARNING_CAPACITY = 256;
export const FLASH_DECAY_PER_SECOND = 9;
export const SHEEP_DEAD_STATE = 8;
export const CORPSE_FADE_MS = 1500;
export const CORPSE_SHRINK = 0.72;
export const CORPSE_SINK_M = 0.16;
export const CORPSE_DARKEN = 0.62;
export const SHEEP_BODY_HEIGHT_M = 0.62;
export const WOOL_CLUSTER_RADIUS_M = 0.21;
export const EMBLEM_HEAD_OFFSET = Object.freeze({ y: 0.9, z: 0.92 });
export const EMBLEM_LOCAL_SCALE: readonly number[] = Object.freeze(
  SHEEP_FORM_SCALE.map(
    (scale, form) => (form === SHEEP_FORM.king ? EMBLEM_PLANE_KING_M : EMBLEM_PLANE_M) / scale,
  ),
);

/** O06：字段可变，以便从环形池复用（详见 sheepInstancePool.ts）。 */
export interface SheepInstance {
  id: number;
  form: number;
  x: number;
  y: number;
  z: number;
  yaw: number;
  hpRatio: number;
  windup: boolean;
  state?: number;
}

export interface ChargeWarning {
  id: number;
  x: number;
  z: number;
  radiusM: number;
}

export interface SheepFlock {
  begin(): void;
  push(instance: SheepInstance): void;
  end(dtMs: number): void;
  flash(id: number): void;
  readonly chargeWarnings: readonly ChargeWarning[];
  readonly bossHpRatio: number | undefined;
  readonly visibleCount: number;
  readonly corpseCount: number;
  dispose(): void;
}

const GOLDEN_ANGLE = Math.PI * (3 - Math.sqrt(5));

export function corpseFadeProgress(elapsedMs: number): number {
  if (!Number.isFinite(elapsedMs) || elapsedMs <= 0) return 0;
  const ratio = elapsedMs / CORPSE_FADE_MS;
  return ratio > 1 ? 1 : ratio;
}

function withShade(geometry: BufferGeometry, shade: number): BufferGeometry {
  const position = geometry.getAttribute('position');
  const colors = new Float32Array(position.count * 3);
  colors.fill(shade);
  geometry.setAttribute('color', new BufferAttribute(colors, 3));
  return geometry;
}

function box(
  sizeX: number,
  sizeY: number,
  sizeZ: number,
  x: number,
  y: number,
  z: number,
  shade: number,
): BufferGeometry {
  const geometry = new BoxGeometry(sizeX, sizeY, sizeZ);
  geometry.translate(x, y, z);
  return withShade(geometry, shade);
}

function buildHorn(form: number, side: number): BufferGeometry {
  const radius = form === SHEEP_FORM.king ? 0.11 : 0.075;
  const length = form === SHEEP_FORM.king ? 0.52 : 0.38;
  const geometry = new ConeGeometry(radius, length, 5);
  geometry.rotateZ(side * 0.95);
  geometry.rotateX(-0.25);
  geometry.translate(side * 0.21, 1.02, 0.62);
  return withShade(geometry, 1);
}

function mergeParts(parts: readonly BufferGeometry[]): BufferGeometry {
  const merged = mergeGeometries(parts.slice());
  for (const part of parts) part.dispose();
  return merged ?? new BoxGeometry(1, 1, 1);
}

function buildBodyGeometry(form: number): BufferGeometry {
  const parts: BufferGeometry[] = [
    box(1.1, 0.72, 0.72, 0, SHEEP_BODY_HEIGHT_M, 0, 1),
    box(0.16, 0.52, 0.16, 0.36, 0.26, 0.24, 0.55),
    box(0.16, 0.52, 0.16, -0.36, 0.26, 0.24, 0.55),
    box(0.16, 0.52, 0.16, 0.36, 0.26, -0.24, 0.55),
    box(0.16, 0.52, 0.16, -0.36, 0.26, -0.24, 0.55),
    box(0.44, 0.44, 0.5, 0, 0.9, 0.66, 0.62),
    box(0.22, 0.08, 0.16, 0.3, 0.99, 0.6, 0.5),
    box(0.22, 0.08, 0.16, -0.3, 0.99, 0.6, 0.5),
    box(0.12, 0.2, 0.12, 0, 0.72, -0.4, 0.5),
  ];
  if (form === SHEEP_FORM.king) parts.push(box(0.5, 0.5, 0.24, 0, 1.02, 0.78, 0.8));
  return mergeParts(parts);
}

function clusterOffsets(count: number): Matrix4[] {
  const offsets: Matrix4[] = [];
  for (let i = 0; i < count; i += 1) {
    const ratio = (i + 0.5) / count;
    const angle = i * GOLDEN_ANGLE;
    const ring = Math.sqrt(ratio);
    const scale = 0.86 + hashFloat(i * 977 + count) * 0.28;
    const matrix = new Matrix4();
    matrix.compose(
      new Vector3(
        Math.cos(angle) * ring * 0.62,
        SHEEP_BODY_HEIGHT_M + (ratio - 0.5) * 0.4,
        Math.sin(angle) * ring * 0.42,
      ),
      new Quaternion(),
      new Vector3(scale, scale, scale),
    );
    offsets.push(matrix);
  }
  return offsets;
}

function hashFloat(seed: number): number {
  let h = Math.imul(seed ^ 0x9e3779b9, 0x85ebca6b);
  h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}

function materialFor(form: number, materials: RenderMaterials) {
  if (form === SHEEP_FORM.ram) return materials.woolDark;
  if (form === SHEEP_FORM.elite) return materials.woolElite;
  if (form === SHEEP_FORM.king) return materials.woolKing;
  return materials.woolLight;
}

function clustersFor(form: number): number {
  return form === SHEEP_FORM.grunt ? WOOL_CLUSTERS_GRUNT : WOOL_CLUSTERS_PER_SHEEP;
}

export function createSheepFlock(scene: Scene, materials: RenderMaterials): SheepFlock {
  const bodyMeshes: InstancedMesh[] = [];
  const woolMeshes: InstancedMesh[] = [];
  const woolOffsets: Matrix4[][] = [];
  const woolGeometries: BufferGeometry[] = [];
  const bodyGeometries: BufferGeometry[] = [];
  const hornMeshes: (InstancedMesh | undefined)[] = [];
  const hornGeometries: (BufferGeometry | undefined)[] = [];
  const scratchColor = new Color();
  const identity = new Matrix4();

  for (let form = 0; form < SHEEP_FORM_COUNT; form += 1) {
    const bodyGeometry = buildBodyGeometry(form);
    bodyGeometries.push(bodyGeometry);
    const bodyMesh = new InstancedMesh(
      bodyGeometry,
      materialFor(form, materials),
      SHEEP_FORM_CAPACITY,
    );
    bodyMesh.name = 'sheep-body-' + String(form);
    // 实例矩阵每帧都变，而 three 只会计算一次 InstancedMesh.boundingSphere 并永久缓存
    // （启动首帧羊数为 0 ⇒ 空球 ⇒ 整局被剔除）。与 smallMesh/粒子/曳光一致：不做视锥剔除。
    bodyMesh.frustumCulled = false;
    for (let i = 0; i < SHEEP_FORM_CAPACITY; i += 1) {
      identity.makeScale(0, 0, 0);
      bodyMesh.setMatrixAt(i, identity);
      bodyMesh.setColorAt(i, scratchColor.setRGB(1, 1, 1));
    }
    bodyMesh.count = 0;
    bodyMeshes.push(bodyMesh);
    scene.add(bodyMesh);

    const clusterGeometry = withShade(new IcosahedronGeometry(WOOL_CLUSTER_RADIUS_M, 0), 1);
    woolGeometries.push(clusterGeometry);
    const clusters = clustersFor(form);
    woolOffsets.push(clusterOffsets(clusters));
    const woolMesh = new InstancedMesh(
      clusterGeometry,
      materialFor(form, materials),
      SHEEP_FORM_CAPACITY * clusters,
    );
    woolMesh.name = 'sheep-wool-' + String(form);
    woolMesh.frustumCulled = false;
    for (let i = 0; i < SHEEP_FORM_CAPACITY * clusters; i += 1) {
      identity.makeScale(0, 0, 0);
      woolMesh.setMatrixAt(i, identity);
    }
    woolMesh.count = 0;
    woolMeshes.push(woolMesh);
    scene.add(woolMesh);

    if (form === SHEEP_FORM.ram || form === SHEEP_FORM.king) {
      const hornGeometry = mergeParts([buildHorn(form, 1), buildHorn(form, -1)]);
      hornGeometries.push(hornGeometry);
      const hornMaterial = form === SHEEP_FORM.king ? materials.hornKing : materials.hornRam;
      const hornMesh = new InstancedMesh(hornGeometry, hornMaterial, SHEEP_FORM_CAPACITY);
      hornMesh.name = 'sheep-horn-' + String(form);
      hornMesh.frustumCulled = false;
      for (let i = 0; i < SHEEP_FORM_CAPACITY; i += 1) {
        identity.makeScale(0, 0, 0);
        hornMesh.setMatrixAt(i, identity);
      }
      hornMesh.count = 0;
      hornMeshes[form] = hornMesh;
      scene.add(hornMesh);
    } else {
      hornMeshes[form] = undefined;
    }
  }

  const emblemGeometry = new PlaneGeometry(EMBLEM_PLANE_M, EMBLEM_PLANE_M);
  const tints = new Float32Array(SHEEP_TOTAL_CAPACITY * 3);
  const intensities = new Float32Array(SHEEP_TOTAL_CAPACITY);
  const tintAttribute = new InstancedBufferAttribute(tints, 3);
  const intensityAttribute = new InstancedBufferAttribute(intensities, 1);
  emblemGeometry.setAttribute('instanceEmissiveTint', tintAttribute);
  emblemGeometry.setAttribute('instanceEmissiveIntensity', intensityAttribute);
  const emblemMesh = new InstancedMesh(emblemGeometry, materials.emblem, SHEEP_TOTAL_CAPACITY);
  emblemMesh.name = 'sheep-emblem';
  emblemMesh.frustumCulled = false;
  for (let i = 0; i < SHEEP_TOTAL_CAPACITY; i += 1) {
    identity.makeScale(0, 0, 0);
    emblemMesh.setMatrixAt(i, identity);
    intensities[i] = 0;
  }
  emblemMesh.count = 0;
  scene.add(emblemMesh);

  const counts = new Int32Array(SHEEP_FORM_COUNT);
  const slotId = new Int32Array(SHEEP_TOTAL_CAPACITY).fill(-1);
  const slotFlash = new Float32Array(SHEEP_TOTAL_CAPACITY);
  const pendingFlash = new Map<number, number>();
  const warnings: ChargeWarning[] = [];
  const warningPool = createChargeWarningPool(CHARGE_WARNING_CAPACITY);
  const sheepMatrix = new Matrix4();
  const localMatrix = new Matrix4();
  const position = new Vector3();
  const quaternion = new Quaternion();
  const scale = new Vector3();
  let bossHp: number | undefined;
  let visible = 0;
  let emblemSlots = 0;
  const list: SheepInstance[] = [];
  const deadIds = new Set<number>();
  const corpseElapsed = new Map<number, number>();
  const upAxis = new Vector3(0, 1, 0);

  function slotFor(form: number, index: number): number {
    return form * SHEEP_FORM_CAPACITY + index;
  }

  return {
    begin(): void {
      counts.fill(0);
      warnings.length = 0;
      warningPool.reset();
      bossHp = undefined;
      visible = 0;
      emblemSlots = 0;
      list.length = 0;
    },
    push(instance: SheepInstance): void {
      list.push(instance);
    },
    end(dtMs: number): void {
      const flashDelta = (dtMs / 1000) * FLASH_DECAY_PER_SECOND;
      deadIds.clear();
      for (const [id, left] of pendingFlash) {
        const next = left - dtMs;
        if (next <= 0) pendingFlash.delete(id);
        else pendingFlash.set(id, next);
      }
      for (let i = 0; i < SHEEP_TOTAL_CAPACITY; i += 1) {
        const next = (slotFlash[i] ?? 0) - flashDelta;
        slotFlash[i] = next > 0 ? next : 0;
      }
      for (const instance of list) {
        const form =
          instance.form < 0 || instance.form >= SHEEP_FORM_COUNT ? SHEEP_FORM.grunt : instance.form;
        const index = counts[form] ?? 0;
        if (index >= SHEEP_FORM_CAPACITY) continue;
        counts[form] = index + 1;
        visible += 1;
        const slot = slotFor(form, index);
        const formScale = SHEEP_FORM_SCALE[form] ?? 1;
        let fade = 0;
        if (instance.state === SHEEP_DEAD_STATE) {
          deadIds.add(instance.id);
          const elapsed = (corpseElapsed.get(instance.id) ?? 0) + dtMs;
          corpseElapsed.set(instance.id, elapsed);
          fade = corpseFadeProgress(elapsed);
        }
        const shrink = formScale * (1 - CORPSE_SHRINK * fade);
        quaternion.setFromAxisAngle(upAxis, instance.yaw);
        position.set(instance.x, instance.y - CORPSE_SINK_M * fade, instance.z);
        scale.set(shrink, shrink, shrink);
        sheepMatrix.compose(position, quaternion, scale);
        const bodyMesh = bodyMeshes[form];
        if (bodyMesh !== undefined) {
          bodyMesh.setMatrixAt(index, sheepMatrix);
          const flash = pendingFlash.get(instance.id) === undefined ? (slotFlash[slot] ?? 0) : 1;
          if (pendingFlash.delete(instance.id)) slotFlash[slot] = 1;
          const boost = (1 + flash * 2.6) * (1 - CORPSE_DARKEN * fade);
          bodyMesh.setColorAt(index, scratchColor.setRGB(boost, boost, boost));
        }
        const hornMesh = hornMeshes[form];
        if (hornMesh !== undefined) hornMesh.setMatrixAt(index, sheepMatrix);
        const woolMesh = woolMeshes[form];
        const offsets = woolOffsets[form];
        if (woolMesh !== undefined && offsets !== undefined) {
          for (let c = 0; c < offsets.length; c += 1) {
            const offset = offsets[c];
            if (offset === undefined) continue;
            localMatrix.multiplyMatrices(sheepMatrix, offset);
            woolMesh.setMatrixAt(index * offsets.length + c, localMatrix);
          }
        }
        if (slotId[slot] !== instance.id) {
          slotId[slot] = instance.id;
          intensities[slot] = emblemIntensity(instance.hpRatio);
          emblemEmissiveTint(form === SHEEP_FORM.king, scratchColor);
          tints[slot * 3] = scratchColor.r;
          tints[slot * 3 + 1] = scratchColor.g;
          tints[slot * 3 + 2] = scratchColor.b;
        }
        const target = emblemIntensity(instance.hpRatio);
        const smoothed = emblemSmoothingStep(intensities[slot] ?? target, target, dtMs);
        intensities[slot] = smoothed;
        localMatrix.compose(
          position.set(0, EMBLEM_HEAD_OFFSET.y, EMBLEM_HEAD_OFFSET.z),
          quaternion.identity(),
          scale.setScalar(EMBLEM_LOCAL_SCALE[form] ?? EMBLEM_PLANE_M),
        );
        localMatrix.premultiply(sheepMatrix);
        emblemMesh.setMatrixAt(slot, localMatrix);
        if (slot + 1 > emblemSlots) emblemSlots = slot + 1;
        if (instance.windup && fade === 0) {
          const warning = warningPool.acquire(warnings.length);
          warning.id = instance.id;
          warning.x = instance.x;
          warning.z = instance.z;
          warning.radiusM = CHARGE_WARNING_RADIUS_M * formScale;
          warnings.push(warning);
        }
        if (form === SHEEP_FORM.king) {
          bossHp = bossHp === undefined ? instance.hpRatio : Math.min(bossHp, instance.hpRatio);
        }
      }
      for (let form = 0; form < SHEEP_FORM_COUNT; form += 1) {
        const count = counts[form] ?? 0;
        // 只有本帧真的写过实例（count > 0）或实例数变化时才上传整块缓冲：
        // 空场帧（大厅/波间清场）不再每帧重传 sheeps 的全部实例矩阵。
        const bodyMesh = bodyMeshes[form];
        if (bodyMesh !== undefined) {
          const upload = count > 0 || bodyMesh.count !== count;
          bodyMesh.count = count;
          if (upload) {
            bodyMesh.instanceMatrix.needsUpdate = true;
            if (bodyMesh.instanceColor !== null) bodyMesh.instanceColor.needsUpdate = true;
          }
        }
        const offsets = woolOffsets[form];
        const woolMesh = woolMeshes[form];
        const clusters = offsets === undefined ? 0 : offsets.length;
        if (woolMesh !== undefined) {
          const instanceCount = count * clusters;
          const upload = instanceCount > 0 || woolMesh.count !== instanceCount;
          woolMesh.count = instanceCount;
          if (upload) woolMesh.instanceMatrix.needsUpdate = true;
        }
        const hornMesh = hornMeshes[form];
        if (hornMesh !== undefined) {
          const upload = count > 0 || hornMesh.count !== count;
          hornMesh.count = count;
          if (upload) hornMesh.instanceMatrix.needsUpdate = true;
        }
      }
      for (const id of corpseElapsed.keys()) {
        if (!deadIds.has(id)) corpseElapsed.delete(id);
      }
      const emblemUpload = emblemSlots > 0 || emblemMesh.count !== emblemSlots;
      emblemMesh.count = emblemSlots;
      if (emblemUpload) {
        emblemMesh.instanceMatrix.needsUpdate = true;
        intensityAttribute.needsUpdate = true;
        tintAttribute.needsUpdate = true;
      }
      list.length = 0;
    },
    flash(id: number): void {
      pendingFlash.set(id, 140);
    },
    chargeWarnings: warnings,
    get bossHpRatio(): number | undefined {
      return bossHp;
    },
    get visibleCount(): number {
      return visible;
    },
    get corpseCount(): number {
      return corpseElapsed.size;
    },
    dispose(): void {
      corpseElapsed.clear();
      deadIds.clear();
      for (const mesh of bodyMeshes) {
        scene.remove(mesh);
        mesh.dispose();
      }
      for (const mesh of woolMeshes) {
        scene.remove(mesh);
        mesh.dispose();
      }
      for (const mesh of hornMeshes) {
        if (mesh === undefined) continue;
        scene.remove(mesh);
        mesh.dispose();
      }
      scene.remove(emblemMesh);
      emblemMesh.dispose();
      for (const geometry of bodyGeometries) geometry.dispose();
      for (const geometry of woolGeometries) geometry.dispose();
      for (const geometry of hornGeometries) {
        if (geometry !== undefined) geometry.dispose();
      }
      emblemGeometry.dispose();
    },
  };
}

export function sheepFormRadiusM(form: number): number {
  return CHARGE_WARNING_RADIUS_M * (SHEEP_FORM_SCALE[form] ?? 1);
}
