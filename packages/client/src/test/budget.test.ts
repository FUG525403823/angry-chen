import {
  type BufferGeometry,
  InstancedMesh,
  Mesh,
  PerspectiveCamera,
  Scene,
  type Material,
  type Object3D,
} from 'three';
import { describe, expect, it } from 'vitest';
import { SHEEP_STATE } from '@ac/shared';

import { createSheepVisualTracker } from '../net/sheepVisual.ts';
import type { ViewEntity } from '../net/state.ts';
import { createArenaArt } from '../render/arenaArt.ts';
import { createEffects } from '../render/effects.ts';
import { createEntityViews } from '../render/entityViews.ts';
import { DRAW_CALL_BUDGET, TRIANGLE_BUDGET } from '../ui/debugPanel.ts';
import { DEFAULT_PALETTE, MATERIAL_BUDGET, createMaterials } from '../render/materials.ts';
import { PARTICLE_CAPACITY, createParticles } from '../render/particles.ts';
import { createViewModelModel } from '../render/viewmodelModel.ts';

const SHEEP_COUNT = 40;
const PLAYER_COUNT = 4;

function triangleCount(geometry: BufferGeometry): number {
  const index = geometry.getIndex();
  if (index !== null) return index.count / 3;
  return geometry.getAttribute('position').count / 3;
}

interface Tally {
  drawCalls: number;
  triangles: number;
  materials: Set<Material>;
}

function tally(root: Object3D): Tally {
  const result: Tally = { drawCalls: 0, triangles: 0, materials: new Set<Material>() };
  root.traverseVisible((node) => {
    if (!(node instanceof Mesh)) return;
    const instances = node instanceof InstancedMesh ? node.count : 1;
    if (instances <= 0) return;
    result.drawCalls += 1;
    result.triangles += triangleCount(node.geometry) * instances;
    const material = node.material;
    if (Array.isArray(material)) for (const item of material) result.materials.add(item);
    else result.materials.add(material);
  });
  return result;
}

function sheepEntity(id: number, state: number, x: number): ViewEntity {
  return {
    id,
    kind: 1,
    pos: { x, y: 0, z: -6 },
    yaw: 0,
    pitch: 0,
    hpRatio: 0.4 + (id % 5) * 0.1,
    state,
    flags: 0,
  };
}

function playerEntity(id: number, x: number): ViewEntity {
  return {
    id,
    kind: 0,
    pos: { x, y: 0, z: 4 },
    yaw: 0.4,
    pitch: 0,
    hpRatio: 0.8,
    state: 0,
    flags: 0,
  };
}

function smallEntity(id: number, kind: 2 | 3, x: number): ViewEntity {
  return {
    id,
    kind,
    pos: { x, y: 1, z: x },
    yaw: 0.2,
    pitch: 0,
    hpRatio: 1,
    state: 0,
    flags: 0,
  };
}

describe('P09 §5.3 冻结性能预算（场景静态上界）', () => {
  it('满编一局的绘制批次/三角形/材质均不超预算', () => {
    const scene = new Scene();
    const camera = new PerspectiveCamera(75, 16 / 9, 0.1, 200);
    const materials = createMaterials();
    const entities: ViewEntity[] = [];
    const states = [
      SHEEP_STATE.graze,
      SHEEP_STATE.windup,
      SHEEP_STATE.ranged,
      SHEEP_STATE.kingPhase2,
    ];
    for (let i = 0; i < SHEEP_COUNT; i += 1) {
      entities.push(sheepEntity(100 + i, states[i % states.length] ?? SHEEP_STATE.graze, i));
    }
    for (let i = 0; i < PLAYER_COUNT; i += 1) entities.push(playerEntity(i + 1, i));
    for (let i = 0; i < 6; i += 1) entities.push(smallEntity(500 + i, i % 2 === 0 ? 2 : 3, i));

    const arenaArt = createArenaArt(
      {
        halfSize: 40,
        fenceHeight: 1.4,
        fenceThickness: 0.3,
        barn: { minX: -10, maxX: -4, minY: 0, maxY: 5, minZ: 6, maxZ: 12 },
      },
      [
        { x: -6, z: -18, team: 0 },
        { x: 6, z: -18, team: 0 },
        { x: 0, z: 20, team: 1 },
      ],
      materials,
      DEFAULT_PALETTE,
    );
    scene.add(arenaArt.root);
    const views = createEntityViews(
      scene,
      {
        forEachVisible(callback: (entity: ViewEntity) => void): void {
          for (const entity of entities) callback(entity);
        },
      },
      () => 1,
      materials,
      DEFAULT_PALETTE,
    );
    const particles = createParticles(scene, camera, materials);
    const effects = createEffects(scene, particles, materials, document.createElement('div'), {
      isReduceMotion: () => false,
    });
    const viewModel = createViewModelModel(materials);

    const tracker = createSheepVisualTracker();
    for (let frame = 0; frame < 3; frame += 1) {
      views.sync((id, state) => tracker.resolve(id, state), 16.7);
      effects.setChargeWarnings(views.chargeWarnings);
      effects.update(16.7);
      viewModel.update(16.7);
    }
    effects.spawnTracer(0, 1.6, 0, 0, 1.5, -20, true);
    effects.spawnImpact(1, 1, -4);
    particles.emit(3, 0, 1, -4, 6, 0, 1, 0);
    effects.update(16.7);

    const main = tally(scene);
    const overlay = tally(viewModel.scene);
    expect(views.meshCount).toBeGreaterThan(0);
    expect(views.chargeWarnings.length).toBeGreaterThan(0);
    expect(effects.activeChargeRings).toBeGreaterThan(0);
    expect(particles.activeCount).toBeGreaterThan(0);
    expect(main.drawCalls + overlay.drawCalls).toBeLessThanOrEqual(DRAW_CALL_BUDGET);
    expect(main.triangles + overlay.triangles).toBeLessThanOrEqual(TRIANGLE_BUDGET);
    expect(main.materials.size + overlay.materials.size).toBeLessThanOrEqual(MATERIAL_BUDGET);
    expect(particles.capacity).toBe(PARTICLE_CAPACITY);
    expect(materials.count).toBeLessThanOrEqual(MATERIAL_BUDGET);

    arenaArt.dispose();
    viewModel.dispose();
    effects.dispose();
    particles.dispose();
    views.dispose();
    materials.dispose();
  });
});
