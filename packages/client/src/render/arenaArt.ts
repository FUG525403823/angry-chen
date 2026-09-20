import {
  BoxGeometry,
  type BufferGeometry,
  CanvasTexture,
  CircleGeometry,
  Color,
  CylinderGeometry,
  Group,
  InstancedMesh,
  Matrix4,
  Mesh,
  PlaneGeometry,
  RepeatWrapping,
  RingGeometry,
  SRGBColorSpace,
  type Texture,
} from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

import { type ArtPalette, type RenderMaterials } from './materials.ts';

export const GROUND_TEXTURE_SIZE = 256;
export const WOOD_TEXTURE_SIZE = 128;
export const GROUND_TILE_M = 6;
export const DIRT_TILE_M = 4;
export const MARKER_RING_SEGMENTS = 20;
export const MARKER_CAPACITY = 32;
export const BARN_ROOF_OVERHANG_M = 1.1;
export const HAY_BALE_COUNT = 5;
export const TROUGH_Y_M = 0.35;
export const DRAW_CALLS = 8;

export interface ArenaArtParams {
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

export interface ArenaArtMarker {
  readonly x: number;
  readonly z: number;
  readonly team: number;
}

export interface ArenaArt {
  readonly root: Group;
  readonly drawCalls: number;
  setPalette(palette: ArtPalette): void;
  dispose(): void;
}

function hashNoise(x: number, y: number): number {
  let h = Math.imul(x + 0x9e37, 0x85ebca6b) ^ Math.imul(y + 0x27d4, 0xc2b2ae35);
  h = Math.imul(h ^ (h >>> 15), 0x2545f491);
  return ((h ^ (h >>> 13)) >>> 0) / 4294967295;
}

function createTexture(
  size: number,
  tileM: number,
  spanM: number,
  draw: (context: CanvasRenderingContext2D, size: number) => void,
): CanvasTexture {
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const context = canvas.getContext('2d');
  if (context !== null) draw(context, size);
  const texture = new CanvasTexture(canvas);
  texture.colorSpace = SRGBColorSpace;
  texture.wrapS = RepeatWrapping;
  texture.wrapT = RepeatWrapping;
  texture.repeat.set(spanM / tileM, spanM / tileM);
  texture.needsUpdate = true;
  return texture;
}

function drawGrass(context: CanvasRenderingContext2D, size: number): void {
  context.fillStyle = '#dfe8d2';
  context.fillRect(0, 0, size, size);
  const blocks = 18;
  const cell = size / blocks;
  for (let y = 0; y < blocks; y += 1) {
    for (let x = 0; x < blocks; x += 1) {
      const value = 198 + Math.round(hashNoise(x, y) * 57);
      context.fillStyle =
        'rgb(' + String(value) + ',' + String(value + 6) + ',' + String(value - 14) + ')';
      context.fillRect(x * cell, y * cell, cell + 1, cell + 1);
    }
  }
  for (let i = 0; i < 420; i += 1) {
    const x = hashNoise(i, 17) * size;
    const y = hashNoise(i, 91) * size;
    const blade = 2 + hashNoise(i, 53) * 4;
    context.fillStyle = hashNoise(i, 7) > 0.55 ? '#ffffff' : '#8f9c78';
    context.fillRect(x, y, 1.6, blade);
  }
}

function drawDirt(context: CanvasRenderingContext2D, size: number): void {
  context.fillStyle = '#cfc0a4';
  context.fillRect(0, 0, size, size);
  for (let i = 0; i < 900; i += 1) {
    const x = hashNoise(i, 3) * size;
    const y = hashNoise(i, 11) * size;
    const radius = 1 + hashNoise(i, 29) * 5;
    const value = 168 + Math.round(hashNoise(i, 47) * 70);
    context.fillStyle =
      'rgb(' + String(value) + ',' + String(value - 12) + ',' + String(value - 34) + ')';
    context.beginPath();
    context.arc(x, y, radius, 0, Math.PI * 2);
    context.fill();
  }
}

function drawWood(context: CanvasRenderingContext2D, size: number): void {
  context.fillStyle = '#e6d5b8';
  context.fillRect(0, 0, size, size);
  const plank = size / 6;
  for (let i = 0; i < 6; i += 1) {
    const value = 214 + Math.round(hashNoise(i, 5) * 34);
    context.fillStyle =
      'rgb(' + String(value) + ',' + String(value - 10) + ',' + String(value - 30) + ')';
    context.fillRect(0, i * plank, size, plank - 1);
    context.fillStyle = '#9c7f56';
    context.fillRect(0, i * plank + plank - 2, size, 2);
  }
  for (let i = 0; i < 240; i += 1) {
    const x = hashNoise(i, 23) * size;
    const y = hashNoise(i, 61) * size;
    context.fillStyle = 'rgba(120,92,58,0.35)';
    context.fillRect(x, y, 1, 6 + hashNoise(i, 83) * 12);
  }
}

function boxPart(
  sizeX: number,
  sizeY: number,
  sizeZ: number,
  x: number,
  y: number,
  z: number,
  rotateY = 0,
): BufferGeometry {
  const geometry = new BoxGeometry(sizeX, sizeY, sizeZ);
  if (rotateY !== 0) geometry.rotateY(rotateY);
  geometry.translate(x, y, z);
  return geometry;
}

function mergeParts(parts: readonly BufferGeometry[]): BufferGeometry {
  const merged = mergeGeometries(parts.slice());
  for (const part of parts) part.dispose();
  return merged ?? new BoxGeometry(1, 1, 1);
}

export function createArenaArt(
  params: ArenaArtParams,
  markers: readonly ArenaArtMarker[],
  materials: RenderMaterials,
  palette: ArtPalette,
): ArenaArt {
  const root = new Group();
  root.name = 'arena-art';
  const half = params.halfSize;
  const span = half * 2;
  const textures: Texture[] = [];

  const grassTexture = createTexture(GROUND_TEXTURE_SIZE, GROUND_TILE_M, span, drawGrass);
  const dirtTexture = createTexture(GROUND_TEXTURE_SIZE, DIRT_TILE_M, 14, drawDirt);
  const woodTexture = createTexture(WOOD_TEXTURE_SIZE, 2.4, 2.4, drawWood);
  textures.push(grassTexture, dirtTexture, woodTexture);
  materials.groundGrass.map = grassTexture;
  materials.groundGrass.needsUpdate = true;
  materials.groundDirt.map = dirtTexture;
  materials.groundDirt.needsUpdate = true;
  materials.fence.map = woodTexture;
  materials.fence.needsUpdate = true;

  const ground = new Mesh(new PlaneGeometry(span, span), materials.groundGrass);
  ground.rotation.x = -Math.PI / 2;
  ground.frustumCulled = true;
  root.add(ground);

  const dirt = new Mesh(new CircleGeometry(7.5, 28), materials.groundDirt);
  dirt.rotation.x = -Math.PI / 2;
  dirt.position.y = 0.005;
  dirt.frustumCulled = true;
  root.add(dirt);

  const markerGeometry = new RingGeometry(0.45, 0.6, MARKER_RING_SEGMENTS);
  markerGeometry.rotateX(-Math.PI / 2);
  const markerMesh = new InstancedMesh(markerGeometry, materials.ring, MARKER_CAPACITY);
  markerMesh.frustumCulled = true;
  const matrix = new Matrix4();
  const scratchColor = new Color();
  const markerTeams: number[] = [];
  let markerCount = 0;
  for (const marker of markers) {
    if (markerCount >= MARKER_CAPACITY) break;
    matrix.makeTranslation(marker.x, 0.02, marker.z);
    markerMesh.setMatrixAt(markerCount, matrix);
    markerTeams.push(marker.team);
    markerMesh.setColorAt(
      markerCount,
      scratchColor.setHex(marker.team === 1 ? palette.playerRemote : palette.playerLocal),
    );
    markerCount += 1;
  }
  markerMesh.count = markerCount;
  markerMesh.instanceMatrix.needsUpdate = true;
  if (markerMesh.instanceColor !== null) markerMesh.instanceColor.needsUpdate = true;
  root.add(markerMesh);

  const height = params.fenceHeight;
  const thickness = params.fenceThickness;
  const fenceSpan = span + thickness;
  const fenceMesh = new Mesh(
    mergeParts([
      boxPart(fenceSpan, height, thickness, 0, height / 2, -half),
      boxPart(fenceSpan, height, thickness, 0, height / 2, half),
      boxPart(thickness, height, fenceSpan, -half, height / 2, 0),
      boxPart(thickness, height, fenceSpan, half, height / 2, 0),
    ]),
    materials.fence,
  );
  fenceMesh.frustumCulled = true;
  root.add(fenceMesh);

  const barn = params.barn;
  const barnWidth = barn.maxX - barn.minX;
  const barnHeight = barn.maxY - barn.minY;
  const barnDepth = barn.maxZ - barn.minZ;
  const barnCenterX = (barn.minX + barn.maxX) / 2;
  const barnCenterZ = (barn.minZ + barn.maxZ) / 2;
  const barnWallMesh = new Mesh(
    mergeParts([
      boxPart(
        barnWidth,
        barnHeight,
        barnDepth,
        barnCenterX,
        barn.minY + barnHeight / 2,
        barnCenterZ,
      ),
      boxPart(barnWidth + BARN_ROOF_OVERHANG_M, 0.9, 0.16, barnCenterX, barn.maxY + 0.2, barn.minZ),
    ]),
    materials.barnWall,
  );
  barnWallMesh.frustumCulled = true;
  root.add(barnWallMesh);

  const barnRoofMesh = new Mesh(
    mergeParts([
      boxPart(barnWidth + 2.2, 0.5, barnDepth + 2.2, barnCenterX, barn.maxY + 0.8, barnCenterZ),
      boxPart(0.3, 1.1, 0.3, barnCenterX, barn.maxY + 1.6, barnCenterZ),
    ]),
    materials.barnRoof,
  );
  barnRoofMesh.frustumCulled = true;
  root.add(barnRoofMesh);

  // 杂物与生成点标记纯视觉：碰撞来自 @ac/shared 的 ARENA，渲染层不参与判定（P09 §4）。
  const baleParts: BufferGeometry[] = [];
  for (let i = 0; i < HAY_BALE_COUNT; i += 1) {
    const angle = (i / HAY_BALE_COUNT) * Math.PI * 2;
    const radius = half * 0.55;
    const bale = new CylinderGeometry(0.55, 0.55, 1.1, 10);
    bale.rotateZ(Math.PI / 2);
    bale.rotateY(angle + 0.35);
    bale.translate(Math.cos(angle) * radius, 0.55, Math.sin(angle) * radius);
    baleParts.push(bale);
  }
  const baleMesh = new Mesh(mergeParts(baleParts), materials.hay);
  baleMesh.frustumCulled = true;
  root.add(baleMesh);

  const troughMesh = new Mesh(
    mergeParts([
      boxPart(3.4, 0.7, 1.1, -half * 0.4, TROUGH_Y_M, half * 0.4),
      boxPart(2.6, 0.1, 0.6, -half * 0.4, TROUGH_Y_M + 0.3, half * 0.4),
    ]),
    materials.barnRoof,
  );
  troughMesh.frustumCulled = true;
  root.add(troughMesh);

  return {
    root,
    drawCalls: DRAW_CALLS,
    setPalette(next: ArtPalette): void {
      if (markerMesh.instanceColor === null) return;
      for (let i = 0; i < markerTeams.length; i += 1) {
        markerMesh.setColorAt(
          i,
          scratchColor.setHex(markerTeams[i] === 1 ? next.playerRemote : next.playerLocal),
        );
      }
      markerMesh.instanceColor.needsUpdate = true;
    },
    dispose(): void {
      root.removeFromParent();
      root.traverse((object) => {
        if (object instanceof Mesh) object.geometry.dispose();
      });
      for (const texture of textures) texture.dispose();
    },
  };
}
