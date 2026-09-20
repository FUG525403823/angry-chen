import { Material, ShaderLib } from 'three';
import { describe, expect, it } from 'vitest';

import { applyEmblemShaderPatch } from './emblem.ts';
import {
  COLORBLIND_PALETTE,
  DEFAULT_PALETTE,
  MATERIAL_BUDGET,
  createMaterials,
} from './materials.ts';

describe('共享材质与调色板', () => {
  it('材质实例数不超过冻结预算 24', () => {
    const materials = createMaterials();
    try {
      expect(materials.count).toBeLessThanOrEqual(MATERIAL_BUDGET);
      const instances = new Set<unknown>();
      for (const value of Object.values(materials)) {
        if (value instanceof Material) instances.add(value);
      }
      expect(instances.size).toBe(materials.count);
    } finally {
      materials.dispose();
    }
  });

  it('色盲安全变体用蓝橙对比替代红绿对立', () => {
    expect(COLORBLIND_PALETTE.playerLocal).toBe(0x56b4e9);
    expect(COLORBLIND_PALETTE.playerRemote).toBe(0xe69f00);
    expect(COLORBLIND_PALETTE.danger).not.toBe(DEFAULT_PALETTE.danger);
    const materials = createMaterials(COLORBLIND_PALETTE);
    try {
      expect(materials.playerLocal.color.getHex()).toBe(0x56b4e9);
      expect(materials.ring.color.getHex()).toBe(COLORBLIND_PALETTE.danger);
      materials.setPalette(DEFAULT_PALETTE);
      expect(materials.playerLocal.color.getHex()).toBe(DEFAULT_PALETTE.playerLocal);
    } finally {
      materials.dispose();
    }
  });

  it('受击白闪写入自发光通道并可归零', () => {
    const materials = createMaterials();
    try {
      materials.setHitFlash(1);
      expect(materials.playerLocal.emissive.r).toBeGreaterThan(0.5);
      materials.setHitFlash(0);
      expect(materials.playerLocal.emissive.r).toBe(0);
    } finally {
      materials.dispose();
    }
  });

  it('额标材质保留正的自发光强度，逐实例强度不会被零乘成黑', () => {
    const materials = createMaterials();
    try {
      expect(materials.emblem.emissiveIntensity).toBeGreaterThan(0);
    } finally {
      materials.dispose();
    }
  });

  it('three 的 lambert 着色器仍带额标补丁所需锚点', () => {
    expect(ShaderLib.lambert.fragmentShader).toContain('#include <emissivemap_fragment>');
    expect(ShaderLib.lambert.vertexShader).toContain('#include <begin_vertex>');
    const patched = {
      vertexShader: ShaderLib.lambert.vertexShader,
      fragmentShader: ShaderLib.lambert.fragmentShader,
    };
    applyEmblemShaderPatch(patched);
    expect(patched.vertexShader).toContain('attribute float instanceEmissiveIntensity;');
    expect(patched.fragmentShader).toContain('vEmissiveTint * vEmissiveIntensity');
  });
});
