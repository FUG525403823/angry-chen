import * as THREE from 'three';

export interface ArenaParams {
  readonly halfSize: number;
  readonly fenceHeight: number;
  readonly fenceThickness: number;
  readonly barn: {
    readonly minX: number;
    readonly maxX: number;
    readonly minY: number;
    readonly maxY: number;
    readonly minZ: number;
    readonly maxZ: number;
  };
}

export interface ArenaMarker {
  readonly x: number;
  readonly z: number;
  readonly color?: number;
}

function addBox(
  parent: THREE.Object3D,
  material: THREE.Material,
  cx: number,
  cy: number,
  cz: number,
  sx: number,
  sy: number,
  sz: number,
): THREE.Mesh {
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(sx, sy, sz), material);
  mesh.position.set(cx, cy, cz);
  parent.add(mesh);
  return mesh;
}

export function createArena(
  params: ArenaParams,
  markers: readonly ArenaMarker[] = [],
): THREE.Group {
  const group = new THREE.Group();
  group.name = 'arena';

  const groundMaterial = new THREE.MeshLambertMaterial({ color: 0x2f4630 });
  const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(params.halfSize * 2, params.halfSize * 2),
    groundMaterial,
  );
  ground.rotation.x = -Math.PI / 2;
  group.add(ground);

  const grid = new THREE.GridHelper(params.halfSize * 2, 16, 0x3d5a3e, 0x36492f);
  grid.position.y = 0.01;
  group.add(grid);

  const fenceMaterial = new THREE.MeshLambertMaterial({ color: 0x7a5a3a });
  const half = params.halfSize;
  const thickness = params.fenceThickness;
  const height = params.fenceHeight;
  const span = half * 2 + thickness;
  addBox(group, fenceMaterial, 0, height / 2, -half, span, height, thickness);
  addBox(group, fenceMaterial, 0, height / 2, half, span, height, thickness);
  addBox(group, fenceMaterial, -half, height / 2, 0, thickness, height, span);
  addBox(group, fenceMaterial, half, height / 2, 0, thickness, height, span);

  const barn = params.barn;
  const barnWidth = barn.maxX - barn.minX;
  const barnHeight = barn.maxY - barn.minY;
  const barnDepth = barn.maxZ - barn.minZ;
  const barnMaterial = new THREE.MeshLambertMaterial({ color: 0x8c3b32 });
  addBox(
    group,
    barnMaterial,
    (barn.minX + barn.maxX) / 2,
    barn.minY + barnHeight / 2,
    (barn.minZ + barn.maxZ) / 2,
    barnWidth,
    barnHeight,
    barnDepth,
  );
  const roofMaterial = new THREE.MeshLambertMaterial({ color: 0x5b2a24 });
  addBox(
    group,
    roofMaterial,
    (barn.minX + barn.maxX) / 2,
    barn.maxY + 0.4,
    (barn.minZ + barn.maxZ) / 2,
    barnWidth + 1.2,
    0.8,
    barnDepth + 1.2,
  );

  const markerMaterials = new Map<number, THREE.MeshBasicMaterial>();
  const ringGeometry = new THREE.RingGeometry(0.45, 0.6, 16);
  for (const marker of markers) {
    const color = marker.color ?? 0x9fd2ff;
    let markerMaterial = markerMaterials.get(color);
    if (markerMaterial === undefined) {
      markerMaterial = new THREE.MeshBasicMaterial({ color });
      markerMaterials.set(color, markerMaterial);
    }
    const ring = new THREE.Mesh(ringGeometry, markerMaterial);
    ring.rotation.x = -Math.PI / 2;
    ring.position.set(marker.x, 0.02, marker.z);
    group.add(ring);
  }
  return group;
}

export function disposeArena(group: THREE.Group): void {
  group.traverse((object) => {
    const mesh = object as THREE.Mesh;
    if (mesh.geometry !== undefined) mesh.geometry.dispose();
    const material = mesh.material;
    if (Array.isArray(material)) {
      for (const entry of material) entry.dispose();
    } else if (material !== undefined) {
      material.dispose();
    }
  });
}
