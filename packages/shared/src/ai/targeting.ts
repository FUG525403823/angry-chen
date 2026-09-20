import { SHEEP_AI } from '../config/sheep.ts';
import { createRayHit, rayVsAabb } from '../combat/raycast.ts';
import { createVec3, yawPitchToDirection } from '../math.ts';
import { getEntity, type Entity, type EntityId, type World } from '../world.ts';
import type { TargetState } from './sheepState.ts';

export function aggroIndexOf(playerIds: readonly EntityId[], pid: EntityId): number {
  for (let i = 0; i < playerIds.length && i < SHEEP_AI.aggroSlots; i += 1) {
    if (playerIds[i] === pid) return i;
  }
  return -1;
}

export function decayAggro(state: TargetState, playerCount: number): void {
  const decay = 1 - SHEEP_AI.aggroDecayPerTick;
  const slots = playerCount < SHEEP_AI.aggroSlots ? playerCount : SHEEP_AI.aggroSlots;
  for (let i = 0; i < slots; i += 1) {
    const value = (state.aggro[i] ?? 0) * decay;
    state.aggro[i] = value < 0.01 ? 0 : value;
  }
}

export function noteHit(state: TargetState, playerIds: readonly EntityId[], pid: EntityId): void {
  const index = aggroIndexOf(playerIds, pid);
  if (index < 0) return;
  state.aggro[index] = (state.aggro[index] ?? 0) + SHEEP_AI.aggroPerHit;
}

const occluderScratch = createRayHit();
const directionScratch = createVec3();

export function isVisible(
  world: World,
  fromX: number,
  fromY: number,
  fromZ: number,
  target: Entity,
): boolean {
  if (target.kind !== 'player') return true;
  const dx = target.pos.x - fromX;
  const dz = target.pos.z - fromZ;
  const dy = target.pos.y + 0.9 - fromY;
  const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (distance <= 1e-6) return true;
  yawPitchToDirection(directionScratch, Math.atan2(dx, dz), Math.asin(dy / distance));
  const barn = world.config.arena.barn;
  occluderScratch.hit = false;
  rayVsAabb(
    fromX,
    fromY,
    fromZ,
    directionScratch.x,
    directionScratch.y,
    directionScratch.z,
    barn.minX,
    barn.minY,
    barn.minZ,
    barn.maxX,
    barn.maxY,
    barn.maxZ,
    distance,
    occluderScratch,
  );
  return !occluderScratch.hit;
}

export function selectTarget(
  state: TargetState,
  world: World,
  self: Entity,
  playerIds: readonly EntityId[],
): EntityId {
  const current = state.targetId !== 0 ? getEntity(world, state.targetId) : undefined;
  const currentIndex = aggroIndexOf(playerIds, state.targetId);
  const currentAggro = currentIndex < 0 ? 0 : (state.aggro[currentIndex] ?? 0);
  const currentValid =
    current !== undefined && current.active && current.kind === 'player' && current.hp > 0;

  let bestId: EntityId = currentValid ? state.targetId : 0;
  let bestScore = currentValid ? currentAggro * SHEEP_AI.targetSwitchRatio : -1;
  let bestDistance = state.targetDistanceM;
  const eyeY = self.pos.y + 0.6;

  for (let i = 0; i < playerIds.length; i += 1) {
    const pid = playerIds[i] ?? 0;
    const player = getEntity(world, pid);
    if (player === undefined || !player.active || player.kind !== 'player' || player.hp <= 0)
      continue;
    const dx = player.pos.x - self.pos.x;
    const dz = player.pos.z - self.pos.z;
    const distance = Math.sqrt(dx * dx + dz * dz);
    if (distance > SHEEP_AI.sightM) continue;
    if (pid !== state.targetId && !isVisible(world, self.pos.x, eyeY, self.pos.z, player)) continue;
    if (pid === state.targetId) {
      bestDistance = distance;
      continue;
    }
    const score = (state.aggro[i] ?? 0) + 1 / (1 + distance);
    if (score <= bestScore) continue;
    bestScore = score;
    bestId = pid;
    bestDistance = distance;
  }

  state.targetId = bestId;
  state.targetDistanceM = bestDistance;
  return bestId;
}
