import { Frustum, Matrix4, PerspectiveCamera, Scene, type InstancedMesh } from 'three';
import { describe, expect, it } from 'vitest';

import type { ViewEntity } from '../net/state.ts';
import { createSheepVisualTracker } from '../net/sheepVisual.ts';
import { createEntityViews } from '../render/entityViews.ts';
import { DEFAULT_PALETTE, createMaterials } from '../render/materials.ts';

function sheepEntity(id: number, x: number, z: number): ViewEntity {
  return { id, kind: 1, pos: { x, y: 0, z }, yaw: 0, pitch: 0, hpRatio: 1, state: 0, flags: 0 };
}

function sheepMeshes(scene: Scene): InstancedMesh[] {
  const meshes: InstancedMesh[] = [];
  scene.traverse((node) => {
    const mesh = node as InstancedMesh;
    if (mesh.isInstancedMesh === true && mesh.name.startsWith('sheep-')) meshes.push(mesh);
  });
  return meshes;
}

function frustumFor(camera: PerspectiveCamera, x: number, z: number, yaw: number): Frustum {
  camera.position.set(x, 1.6, z);
  camera.rotation.set(0, yaw, 0);
  camera.updateMatrixWorld(true);
  camera.updateProjectionMatrix();
  const matrix = new Matrix4().multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
  return new Frustum().setFromProjectionMatrix(matrix);
}

/**
 * 与 WebGLRenderer 的逐对象剔除判定同构：
 * `!object.frustumCulled || object.intersectsFrustum(frustum)`（three.module.js:17929）。
 */
function isRendered(mesh: InstancedMesh, frustum: Frustum): boolean {
  return mesh.frustumCulled === false || frustum.intersectsObject(mesh);
}

describe('羊群实例网格的可见性', () => {
  it('羊在玩家正前方时不被剔除，即使玩家背对世界原点', () => {
    const scene = new Scene();
    const camera = new PerspectiveCamera(75, 16 / 9, 0.1, 300);
    camera.rotation.order = 'YXZ';
    const materials = createMaterials();
    const tracker = createSheepVisualTracker();
    const entities: ViewEntity[] = [];
    const views = createEntityViews(
      scene,
      {
        forEachVisible(cb: (entity: ViewEntity) => void): void {
          for (const entity of entities) cb(entity);
        },
      },
      () => 1,
      materials,
      DEFAULT_PALETTE,
    );
    try {
      // ① 启动帧还没有羊：此刻做一次剔除判定会把 InstancedMesh.boundingSphere 算成空球并永久缓存
      //    （three 只在 boundingSphere === null 时计算，之后实例矩阵再怎么变都不重算）。
      const bootFrustum = frustumFor(camera, 0, 18, 0);
      views.sync((id, state) => tracker.resolve(id, state), 16.7);
      const meshes = sheepMeshes(scene);
      expect(meshes.length).toBeGreaterThan(0);
      for (const mesh of meshes) expect(isRendered(mesh, bootFrustum)).toBe(true);

      // ② 战斗帧：8 只羊站在玩家（z=18）正前方 z=30，玩家朝 +Z（背对世界原点）。
      for (let i = 0; i < 8; i += 1) entities.push(sheepEntity(100 + i, -6 + i * 1.7, 30));
      views.sync((id, state) => tracker.resolve(id, state), 16.7);
      const body = meshes.find((mesh) => mesh.name === 'sheep-body-0');
      expect(body?.count).toBe(8);
      const inFront = frustumFor(camera, 0, 18, Math.PI);
      for (const mesh of meshes) {
        expect(isRendered(mesh, inFront)).toBe(true);
      }

      // ③ 朝世界原点看时同样可见（旧实现在这里恰好会“正常”，所以不能只测这一种姿态）。
      const towardOrigin = frustumFor(camera, 0, 18, 0);
      for (const mesh of meshes) expect(isRendered(mesh, towardOrigin)).toBe(true);
    } finally {
      views.dispose();
      materials.dispose();
    }
  });
});
