import * as THREE from 'three';

import { FOG_FAR_M, FOG_NEAR_M, SKY_COLOR } from './materials.ts';

export const EYE_HEIGHT_M = 1.6;
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
  camera.rotation.set(pitch, yaw, 0);
  camera.position.set(pos.x, pos.y + EYE_HEIGHT_M, pos.z);
  if (shakeX === 0 && shakeY === 0) return;
  shakeOffset.set(shakeX, shakeY, 0).applyEuler(camera.rotation);
  camera.position.add(shakeOffset);
}

export function applyFov(camera: THREE.PerspectiveCamera, fov: number): void {
  if (camera.fov === fov) return;
  camera.fov = fov;
  camera.updateProjectionMatrix();
}
