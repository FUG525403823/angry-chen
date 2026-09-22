import { createVec3, rightFromYaw, yawPitchToDirection } from '@ac/shared';
import { Vector3 } from 'three';
import { describe, expect, it } from 'vitest';

import {
  SIM_TO_VIEW_YAW_OFFSET,
  createScene,
  muzzleOrigin,
  updateCamera,
} from '../render/scene.ts';

const YAWS = [0, 0.7, Math.PI / 2, 2.2, -1.4, Math.PI];
const PITCHES = [0, 0.3, -0.4];

describe('视角与模拟 yaw 的换算（真人试玩缺陷回归）', () => {
  it('相机前向 = 服务端射线方向（准星与命中判定同源）', () => {
    const { camera } = createScene(75);
    const direction = new Vector3();
    const aim = createVec3();
    for (const yaw of YAWS) {
      for (const pitch of PITCHES) {
        updateCamera(camera, { x: 0, y: 0, z: 0 }, yaw, pitch);
        camera.updateMatrixWorld(true);
        camera.getWorldDirection(direction);
        yawPitchToDirection(aim, yaw, pitch);
        expect(direction.x * aim.x + direction.y * aim.y + direction.z * aim.z).toBeGreaterThan(
          0.999,
        );
      }
    }
  });

  it('俯仰语义不变：正 pitch 仍是抬头', () => {
    const { camera } = createScene(75);
    const direction = new Vector3();
    updateCamera(camera, { x: 0, y: 0, z: 0 }, 1.1, 0.5);
    camera.updateMatrixWorld(true);
    camera.getWorldDirection(direction);
    expect(direction.y).toBeCloseTo(Math.sin(0.5), 6);
  });

  it('横移轴在采样层取反后指向相机屏幕右侧（D = 画面右）', () => {
    const { camera } = createScene(75);
    const direction = new Vector3();
    const simRight = createVec3();
    for (const yaw of YAWS) {
      updateCamera(camera, { x: 0, y: 0, z: 0 }, yaw, 0);
      camera.updateMatrixWorld(true);
      camera.getWorldDirection(direction);
      // 相机屏幕右向 = 前向 × up(+Y)
      const cameraRightX = -direction.z;
      const cameraRightZ = direction.x;
      // 采样器把 D 键映射到 -rightFromYaw(yaw)（见 input/sampler.ts 的 recomputeAxes）
      rightFromYaw(simRight, yaw);
      const dot = cameraRightX * -simRight.x + cameraRightZ * -simRight.z;
      expect(dot).toBeGreaterThan(0.99);
    }
  });

  it('枪口起点偏离视线且在近裁剪面之外（曳光不再退化成点）', () => {
    const { camera } = createScene(75);
    const forward = new Vector3();
    const aim = createVec3();
    const muzzle = new Vector3();
    const offset = new Vector3();
    for (const yaw of YAWS) {
      updateCamera(camera, { x: 0, y: 0, z: 0 }, yaw, 0.2);
      camera.updateMatrixWorld(true);
      yawPitchToDirection(aim, yaw, 0.2);
      forward.set(aim.x, aim.y, aim.z);
      muzzleOrigin(muzzle, camera, forward);
      offset.copy(muzzle).sub(camera.position);
      expect(offset.length()).toBeGreaterThan(camera.near);
      expect(Math.abs(offset.normalize().dot(forward))).toBeLessThan(0.99);
    }
  });

  it('视图 yaw 偏移为 π（three.js 相机沿 -Z 看，模拟前向是 +Z 旋转）', () => {
    expect(SIM_TO_VIEW_YAW_OFFSET).toBeCloseTo(Math.PI, 12);
  });
});
