import * as THREE from 'three';

import { FOG_FAR_M, FOG_NEAR_M, SKY_COLOR } from './materials.ts';

export const EYE_HEIGHT_M = 1.6;
/**
 * 视图与模拟的 yaw 换算（真人试玩缺陷修复：W/S 反向、鼠标左右反向、打不到）。
 *
 * 模拟约定（@ac/shared 的 localStep / yawPitchToDirection）：forward = (sin yaw, 0, cos yaw)，
 * 即 yaw 把模型空间 +Z 旋转到世界前向 —— 这与 `Object3D.rotation.y = yaw` 一致，
 * 但 three.js 相机沿 -Z 看，因此相机 yaw 必须再加 π 才看向模拟前向。
 * 该偏移同时保证「相机朝向 = 服务端射线方向（yawPitchToDirection）」，准星与命中判定一致。
 */
export const SIM_TO_VIEW_YAW_OFFSET = Math.PI;
export const SUN_POSITION = Object.freeze({ x: 26, y: 44, z: 18 });
export const HEMISPHERE_INTENSITY = 1.25;
export const SUN_INTENSITY = 1.55;

const shakeOffset = new THREE.Vector3();

export interface SceneBundle {
  readonly scene: THREE.Scene;
  readonly camera: THREE.PerspectiveCamera;
}

export function createScene(fov: number): SceneBundle {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(SKY_COLOR);
  scene.fog = new THREE.Fog(SKY_COLOR, FOG_NEAR_M, FOG_FAR_M);

  const camera = new THREE.PerspectiveCamera(fov, 1, 0.1, 300);
  camera.rotation.order = 'YXZ';

  const hemi = new THREE.HemisphereLight(0xdfe9ff, 0x3a4436, HEMISPHERE_INTENSITY);
  const sun = new THREE.DirectionalLight(0xfff3dd, SUN_INTENSITY);
  sun.position.set(SUN_POSITION.x, SUN_POSITION.y, SUN_POSITION.z);
  scene.add(hemi);
  scene.add(sun);
  return { scene, camera };
}

export function updateCamera(
  camera: THREE.PerspectiveCamera,
  pos: { x: number; y: number; z: number },
  yaw: number,
  pitch: number,
  shakeX = 0,
  shakeY = 0,
): void {
  camera.rotation.set(pitch, yaw + SIM_TO_VIEW_YAW_OFFSET, 0);
  camera.position.set(pos.x, pos.y + EYE_HEIGHT_M, pos.z);
  if (shakeX === 0 && shakeY === 0) return;
  shakeOffset.set(shakeX, shakeY, 0).applyEuler(camera.rotation);
  camera.position.add(shakeOffset);
}

/** 枪口相对眼位的偏移（米）：曳光/枪口火焰从枪口出发，避免与视线共线而不可见。 */
export const MUZZLE_FORWARD_M = 0.55;
export const MUZZLE_RIGHT_M = 0.2;
export const MUZZLE_DOWN_M = 0.12;

const muzzleRight = new THREE.Vector3();
const muzzleUp = new THREE.Vector3();

/**
 * 枪口世界坐标 = 眼位 + forward × 前偏移 + 相机右向量 × 右偏移 − 相机上向量 × 下偏移。
 * 从眼位沿视线画曳光会与视线共线，正面投影退化成一点 ⇒ 玩家看不到弹道。
 */
export function muzzleOrigin(
  out: THREE.Vector3,
  camera: THREE.PerspectiveCamera,
  forward: THREE.Vector3,
): THREE.Vector3 {
  camera.updateMatrixWorld();
  const elements = camera.matrixWorld.elements;
  muzzleRight.set(elements[0] ?? 0, elements[1] ?? 0, elements[2] ?? 0);
  muzzleUp.set(elements[4] ?? 0, elements[5] ?? 0, elements[6] ?? 0);
  return out
    .copy(camera.position)
    .addScaledVector(forward, MUZZLE_FORWARD_M)
    .addScaledVector(muzzleRight, MUZZLE_RIGHT_M)
    .addScaledVector(muzzleUp, -MUZZLE_DOWN_M);
}

export function applyFov(camera: THREE.PerspectiveCamera, fov: number): void {
  if (camera.fov === fov) return;
  camera.fov = fov;
  camera.updateProjectionMatrix();
}
