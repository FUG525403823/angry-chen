import * as THREE from 'three';

export const EYE_HEIGHT_M = 1.6;
export const SKY_COLOR = 0x1b2430;

export interface SceneBundle {
  readonly scene: THREE.Scene;
  readonly camera: THREE.PerspectiveCamera;
}

export function createScene(fov: number): SceneBundle {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(SKY_COLOR);
  scene.fog = new THREE.Fog(SKY_COLOR, 50, 130);

  const camera = new THREE.PerspectiveCamera(fov, 1, 0.1, 300);
  camera.rotation.order = 'YXZ';

  const hemi = new THREE.HemisphereLight(0xdfe9ff, 0x2a2f3a, 1.15);
  const sun = new THREE.DirectionalLight(0xfff3dd, 1.5);
  sun.position.set(24, 40, 18);
  scene.add(hemi);
  scene.add(sun);
  return { scene, camera };
}

export function updateCamera(
  camera: THREE.PerspectiveCamera,
  pos: { x: number; y: number; z: number },
  yaw: number,
  pitch: number,
): void {
  camera.position.set(pos.x, pos.y + EYE_HEIGHT_M, pos.z);
  camera.rotation.set(pitch, yaw, 0);
}

export function applyFov(camera: THREE.PerspectiveCamera, fov: number): void {
  if (camera.fov === fov) return;
  camera.fov = fov;
  camera.updateProjectionMatrix();
}
