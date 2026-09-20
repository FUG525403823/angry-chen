import { AdditiveBlending, MeshBasicMaterial, MeshLambertMaterial, type Material } from 'three';

import { EMBLEM_MAIN_COLOR, applyEmblemShaderPatch, getEmblemTexture } from './emblem.ts';

export const MATERIAL_BUDGET = 24;
export const SKY_COLOR = 0x9dc4e6;
export const FOG_NEAR_M = 42;
export const FOG_FAR_M = 138;
export const HIT_FLASH_COLOR = 0xffffff;
export const HIT_FLASH_TOTAL_MS = 110;

export interface ArtPalette {
  readonly groundGrass: number;
  readonly groundDirt: number;
  readonly fence: number;
  readonly barnWall: number;
  readonly barnRoof: number;
  readonly hay: number;
  readonly woolLight: number;
  readonly woolDark: number;
  readonly woolElite: number;
  readonly woolKing: number;
  readonly hornRam: number;
  readonly hornKing: number;
  readonly playerLocal: number;
  readonly playerRemote: number;
  readonly projectile: number;
  readonly pickup: number;
  readonly tracer: number;
  readonly particle: number;
  readonly danger: number;
  readonly viewmodelArm: number;
  readonly viewmodelBody: number;
}

export const DEFAULT_PALETTE: ArtPalette = Object.freeze({
  groundGrass: 0x6f9c52,
  groundDirt: 0x8a7250,
  fence: 0xa9835a,
  barnWall: 0xb45c4a,
  barnRoof: 0x59413c,
  hay: 0xd7c273,
  woolLight: 0xf2ede1,
  woolDark: 0x6d6f74,
  woolElite: 0xfbf4e4,
  woolKing: 0x4b2b31,
  hornRam: 0x2f3034,
  hornKing: 0x8c2f2a,
  playerLocal: 0x4fc3f7,
  playerRemote: 0xff8a65,
  projectile: 0xffe066,
  pickup: 0x9be36f,
  tracer: 0xfff0b8,
  particle: 0xffffff,
  danger: 0xd2402f,
  viewmodelArm: 0xc99a72,
  viewmodelBody: 0x353b47,
});

export const COLORBLIND_PALETTE: ArtPalette = Object.freeze({
  groundGrass: 0x7d9c62,
  groundDirt: 0x9c8253,
  fence: 0xb08b52,
  barnWall: 0xa95a3c,
  barnRoof: 0x4a4a52,
  hay: 0xf0e442,
  woolLight: 0xf4f4f4,
  woolDark: 0x5a5f66,
  woolElite: 0xffffff,
  woolKing: 0x3f2a44,
  hornRam: 0x2b2f36,
  hornKing: 0xd55e00,
  playerLocal: 0x56b4e9,
  playerRemote: 0xe69f00,
  projectile: 0xf0e442,
  pickup: 0x009e73,
  tracer: 0xdcefff,
  particle: 0xffffff,
  danger: 0xd55e00,
  viewmodelArm: 0xc99a72,
  viewmodelBody: 0x39393f,
});

export interface RenderMaterials {
  readonly groundGrass: MeshLambertMaterial;
  readonly groundDirt: MeshLambertMaterial;
  readonly fence: MeshLambertMaterial;
  readonly barnWall: MeshLambertMaterial;
  readonly barnRoof: MeshLambertMaterial;
  readonly hay: MeshLambertMaterial;
  readonly woolLight: MeshLambertMaterial;
  readonly woolDark: MeshLambertMaterial;
  readonly woolElite: MeshLambertMaterial;
  readonly woolKing: MeshLambertMaterial;
  readonly hornRam: MeshLambertMaterial;
  readonly hornKing: MeshLambertMaterial;
  readonly playerLocal: MeshLambertMaterial;
  readonly playerRemote: MeshLambertMaterial;
  readonly projectile: MeshLambertMaterial;
  readonly emblem: MeshLambertMaterial;
  readonly tracer: MeshBasicMaterial;
  readonly particle: MeshBasicMaterial;
  readonly ring: MeshBasicMaterial;
  readonly chargeRing: MeshBasicMaterial;
  readonly viewmodelArm: MeshLambertMaterial;
  readonly viewmodelBody: MeshLambertMaterial;
  readonly count: number;
  setPalette(palette: ArtPalette): void;
  setHitFlash(amount: number): void;
  dispose(): void;
}

export function hitFlashAmount(leftMs: number): number {
  if (!Number.isFinite(leftMs) || leftMs <= 0) return 0;
  const ratio = leftMs / HIT_FLASH_TOTAL_MS;
  return ratio > 1 ? 1 : ratio;
}

export function createMaterials(palette: ArtPalette = DEFAULT_PALETTE): RenderMaterials {
  const groundGrass = new MeshLambertMaterial({ color: palette.groundGrass });
  const groundDirt = new MeshLambertMaterial({ color: palette.groundDirt });
  const fence = new MeshLambertMaterial({ color: palette.fence });
  const barnWall = new MeshLambertMaterial({ color: palette.barnWall });
  const barnRoof = new MeshLambertMaterial({ color: palette.barnRoof });
  const hay = new MeshLambertMaterial({ color: palette.hay });
  const woolLight = new MeshLambertMaterial({ color: palette.woolLight, vertexColors: true });
  const woolDark = new MeshLambertMaterial({ color: palette.woolDark, vertexColors: true });
  const woolElite = new MeshLambertMaterial({ color: palette.woolElite, vertexColors: true });
  const woolKing = new MeshLambertMaterial({ color: palette.woolKing, vertexColors: true });
  const hornRam = new MeshLambertMaterial({ color: palette.hornRam, vertexColors: true });
  const hornKing = new MeshLambertMaterial({ color: palette.hornKing, vertexColors: true });
  const playerLocal = new MeshLambertMaterial({ color: palette.playerLocal, vertexColors: true });
  const playerRemote = new MeshLambertMaterial({ color: palette.playerRemote, vertexColors: true });
  const projectile = new MeshLambertMaterial({ color: palette.projectile });
  const emblem = new MeshLambertMaterial({
    color: EMBLEM_MAIN_COLOR,
    map: getEmblemTexture(),
    emissiveMap: getEmblemTexture(),
    emissive: HIT_FLASH_COLOR,
    emissiveIntensity: 1,
    transparent: true,
    depthWrite: false,
  });
  emblem.onBeforeCompile = (shader): void => applyEmblemShaderPatch(shader);
  emblem.customProgramCacheKey = (): string => 'ac-emblem-instanced-emissive';
  const tracer = new MeshBasicMaterial({
    color: palette.tracer,
    transparent: true,
    depthWrite: false,
    blending: AdditiveBlending,
  });
  const particle = new MeshBasicMaterial({
    color: palette.particle,
    transparent: true,
    depthWrite: false,
  });
  const ring = new MeshBasicMaterial({
    color: palette.danger,
    transparent: true,
    depthWrite: false,
  });
  const chargeRing = new MeshBasicMaterial({
    color: palette.danger,
    transparent: true,
    depthWrite: false,
    blending: AdditiveBlending,
  });
  const viewmodelArm = new MeshLambertMaterial({ color: palette.viewmodelArm });
  const viewmodelBody = new MeshLambertMaterial({ color: palette.viewmodelBody });

  const flashable: readonly MeshLambertMaterial[] = [
    groundGrass,
    groundDirt,
    fence,
    barnWall,
    barnRoof,
    hay,
    woolLight,
    woolDark,
    woolElite,
    woolKing,
    hornRam,
    hornKing,
    playerLocal,
    playerRemote,
    projectile,
  ];
  const materials: readonly Material[] = [
    ...flashable,
    emblem,
    tracer,
    particle,
    ring,
    chargeRing,
    viewmodelArm,
    viewmodelBody,
  ];

  return {
    groundGrass,
    groundDirt,
    fence,
    barnWall,
    barnRoof,
    hay,
    woolLight,
    woolDark,
    woolElite,
    woolKing,
    hornRam,
    hornKing,
    playerLocal,
    playerRemote,
    projectile,
    emblem,
    tracer,
    particle,
    ring,
    chargeRing,
    viewmodelArm,
    viewmodelBody,
    get count(): number {
      return materials.length;
    },
    setPalette(next: ArtPalette): void {
      groundGrass.color.setHex(next.groundGrass);
      groundDirt.color.setHex(next.groundDirt);
      fence.color.setHex(next.fence);
      barnWall.color.setHex(next.barnWall);
      barnRoof.color.setHex(next.barnRoof);
      hay.color.setHex(next.hay);
      woolLight.color.setHex(next.woolLight);
      woolDark.color.setHex(next.woolDark);
      woolElite.color.setHex(next.woolElite);
      woolKing.color.setHex(next.woolKing);
      hornRam.color.setHex(next.hornRam);
      hornKing.color.setHex(next.hornKing);
      playerLocal.color.setHex(next.playerLocal);
      playerRemote.color.setHex(next.playerRemote);
      projectile.color.setHex(next.projectile);
      tracer.color.setHex(next.tracer);
      particle.color.setHex(next.particle);
      ring.color.setHex(next.danger);
      chargeRing.color.setHex(next.danger);
      viewmodelArm.color.setHex(next.viewmodelArm);
      viewmodelBody.color.setHex(next.viewmodelBody);
    },
    setHitFlash(amount: number): void {
      const value = amount < 0 ? 0 : amount > 1 ? 1 : amount;
      for (let i = 0; i < flashable.length; i += 1) {
        const material = flashable[i];
        if (material === undefined) continue;
        material.emissive.setHex(HIT_FLASH_COLOR).multiplyScalar(value * 0.9);
      }
    },
    dispose(): void {
      for (const material of materials) material.dispose();
    },
  };
}
