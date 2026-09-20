import {
  BoxGeometry,
  type BufferGeometry,
  BufferAttribute,
  Color,
  Group,
  InstancedMesh,
  Matrix4,
  Mesh,
  Quaternion,
  type Scene,
  SphereGeometry,
  Vector3,
} from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

import type { ViewEntity } from '../net/state.ts';
import { ELITE_BOLT_ENTITY_KIND } from './effects.ts';
import type { ArtPalette, RenderMaterials } from './materials.ts';
import { type ChargeWarning, type SheepFlock, createSheepFlock } from './sheepModel.ts';

export const SMALL_ENTITY_CAPACITY = 160;
export const PROJECTILE_SIZE_M = 0.16;
export const PICKUP_SIZE_M = 0.4;
export const PLAYER_BODY_HEIGHT_M = 0.95;
export const PLAYER_HEAD_HEIGHT_M = 1.55;
export const DOWNED_TILT_RAD = 1.35;
export const SNAPSHOT_FLAG_DOWNED = 1;

export interface SheepVisual {
  readonly form: number;
  readonly windup: boolean;
}

export interface EntityViews {
  sync(sheepVisual: (id: number, state: number) => SheepVisual, dtMs: number): void;
  readonly meshCount: number;
  readonly liveCount: number;
  readonly bossHpRatio: number | undefined;
  readonly chargeWarnings: readonly ChargeWarning[];
  flashHit(id: number): void;
  setPalette(palette: ArtPalette): void;
  dispose(): void;
}

interface PlayerNode {
  readonly root: Group;
  readonly parts: readonly Mesh[];
  isLocal: boolean;
  seenFrame: number;
}

function mergeParts(parts: readonly BufferGeometry[]): BufferGeometry {
  const merged = mergeGeometries(parts.slice());
  for (const part of parts) part.dispose();
  return merged ?? new BoxGeometry(0.1, 0.1, 0.1);
}

function withShade(geometry: BufferGeometry, shade: number): BufferGeometry {
  const position = geometry.getAttribute('position');
  const colors = new Float32Array(position.count * 3);
  colors.fill(shade);
  geometry.setAttribute('color', new BufferAttribute(colors, 3));
  return geometry;
}

function createPlayerGeometry(): BufferGeometry {
  const torso = new BoxGeometry(0.6, 0.86, 0.36);
  torso.translate(0, PLAYER_BODY_HEIGHT_M, 0);
  const head = new SphereGeometry(0.2, 12, 8);
  head.translate(0, PLAYER_HEAD_HEIGHT_M, 0.02);
  const legLeft = new BoxGeometry(0.18, 0.72, 0.18);
  legLeft.translate(0.16, 0.36, 0);
  const legRight = new BoxGeometry(0.18, 0.72, 0.18);
  legRight.translate(-0.16, 0.36, 0);
  const gun = new BoxGeometry(0.12, 0.14, 0.5);
  gun.translate(0.24, 1.12, 0.28);
  return mergeParts([
    withShade(torso, 1),
    withShade(head, 0.92),
    withShade(legLeft, 0.6),
    withShade(legRight, 0.6),
    withShade(gun, 0.45),
  ]);
}

export function createEntityViews(
  scene: Scene,
  view: { forEachVisible(cb: (entity: ViewEntity) => void): void },
  localPlayerId: () => number,
  materials: RenderMaterials,
  palette: ArtPalette,
): EntityViews {
  let currentPalette = palette;
  let liveCount = 0;
  let frame = 0;
  const flock: SheepFlock = createSheepFlock(scene, materials);
  const playerGeometryShared = createPlayerGeometry();
  const smallGeometry = withShade(
    new BoxGeometry(PROJECTILE_SIZE_M, PROJECTILE_SIZE_M, PROJECTILE_SIZE_M),
    1,
  );
  const playerPool = new Map<number, PlayerNode>();
  const scratchColor = new Color();
  const smallMatrix = new Matrix4();
  const smallPosition = new Vector3();
  const smallScale = new Vector3();
  const upAxis = new Vector3(0, 1, 0);
  const yawQuaternion = new Quaternion();
  const zeroMatrix = new Matrix4().makeScale(0, 0, 0);
  const smallMesh = new InstancedMesh(smallGeometry, materials.projectile, SMALL_ENTITY_CAPACITY);
  smallMesh.name = 'small-entities';
  smallMesh.frustumCulled = false;
  for (let i = 0; i < SMALL_ENTITY_CAPACITY; i += 1) {
    smallMesh.setMatrixAt(i, zeroMatrix);
    smallMesh.setColorAt(i, scratchColor.setHex(palette.projectile));
  }
  smallMesh.count = 0;
  scene.add(smallMesh);

  function createPlayerNode(id: number, isLocal: boolean): PlayerNode {
    const root = new Group();
    root.name = 'player:' + String(id);
    const material = isLocal ? materials.playerLocal : materials.playerRemote;
    const body = new Mesh(playerGeometryShared, material);
    root.add(body);
    scene.add(root);
    return { root, parts: [body], isLocal, seenFrame: 0 };
  }

  return {
    sync(sheepVisual, dtMs): void {
      frame += 1;
      liveCount = 0;
      flock.begin();
      let smallCount = 0;
      const mine = localPlayerId();
      view.forEachVisible((entity) => {
        liveCount += 1;
        if (entity.kind === 1) {
          const visual = sheepVisual(entity.id, entity.state);
          flock.push({
            id: entity.id,
            form: visual.form,
            x: entity.pos.x,
            y: entity.pos.y,
            z: entity.pos.z,
            yaw: entity.yaw,
            hpRatio: entity.hpRatio,
            windup: visual.windup,
            state: entity.state,
          });
          return;
        }
        if (entity.kind === 0) {
          const isLocal = entity.id === mine;
          let node = playerPool.get(entity.id);
          if (node === undefined) {
            node = createPlayerNode(entity.id, isLocal);
            playerPool.set(entity.id, node);
          } else if (node.isLocal !== isLocal) {
            node.isLocal = isLocal;
            const material = isLocal ? materials.playerLocal : materials.playerRemote;
            for (const part of node.parts) part.material = material;
          }
          node.seenFrame = frame;
          node.root.position.set(entity.pos.x, entity.pos.y, entity.pos.z);
          node.root.rotation.set(0, entity.yaw, 0);
          node.root.rotation.z = (entity.flags & SNAPSHOT_FLAG_DOWNED) !== 0 ? DOWNED_TILT_RAD : 0;
          node.root.visible = !isLocal;
          return;
        }
        if (entity.kind === ELITE_BOLT_ENTITY_KIND) return;
        if (smallCount >= SMALL_ENTITY_CAPACITY) return;
        smallPosition.set(entity.pos.x, entity.pos.y, entity.pos.z);
        smallScale.setScalar(PICKUP_SIZE_M);
        yawQuaternion.setFromAxisAngle(upAxis, entity.yaw);
        smallMatrix.compose(smallPosition, yawQuaternion, smallScale);
        smallMesh.setMatrixAt(smallCount, smallMatrix);
        smallMesh.setColorAt(smallCount, scratchColor.setHex(currentPalette.pickup));
        smallCount += 1;
      });
      for (const node of playerPool.values()) {
        if (node.seenFrame !== frame) node.root.visible = false;
      }
      flock.end(dtMs);
      smallMesh.count = smallCount;
      smallMesh.instanceMatrix.needsUpdate = true;
      if (smallMesh.instanceColor !== null) smallMesh.instanceColor.needsUpdate = true;
    },
    get meshCount(): number {
      return playerPool.size;
    },
    get liveCount(): number {
      return liveCount;
    },
    get bossHpRatio(): number | undefined {
      return flock.bossHpRatio;
    },
    get chargeWarnings(): readonly ChargeWarning[] {
      return flock.chargeWarnings;
    },
    flashHit(id: number): void {
      flock.flash(id);
    },
    setPalette(next: ArtPalette): void {
      currentPalette = next;
      for (const node of playerPool.values()) {
        const material = node.isLocal ? materials.playerLocal : materials.playerRemote;
        for (const part of node.parts) part.material = material;
      }
    },
    dispose(): void {
      for (const node of playerPool.values()) {
        scene.remove(node.root);
      }
      playerPool.clear();
      playerGeometryShared.dispose();
      smallGeometry.dispose();
      smallMesh.dispose();
      scene.remove(smallMesh);
      flock.dispose();
    },
  };
}
