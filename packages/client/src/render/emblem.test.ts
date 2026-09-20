import { CanvasTexture } from 'three';
import { describe, expect, it } from 'vitest';

import {
  EMBLEM_INTENSITY_BASE,
  EMBLEM_INTENSITY_SPAN,
  EMBLEM_SMOOTHING_TAU_S,
  EMBLEM_TEXTURE_SIZE,
  applyEmblemShaderPatch,
  emblemIntensity,
  emblemSmoothingStep,
  getEmblemTexture,
  resetEmblemTexture,
} from './emblem.ts';
import { createCanvasStub, getCanvasStubStats, resetCanvasStubStats } from '../test/canvas.ts';

describe('额标纹理', () => {
  it('生成 128×128 的非空 CanvasTexture 并命中单例缓存', () => {
    resetCanvasStubStats();
    resetEmblemTexture();
    const texture = getEmblemTexture();
    expect(texture).toBeInstanceOf(CanvasTexture);
    expect(texture.image.width).toBe(EMBLEM_TEXTURE_SIZE);
    expect(texture.image.height).toBe(EMBLEM_TEXTURE_SIZE);
    expect(getCanvasStubStats().ops).toBeGreaterThan(20);
    expect(getEmblemTexture()).toBe(texture);
  });

  it('OffscreenCanvas 缺失时回退到 document.createElement', () => {
    resetCanvasStubStats();
    resetEmblemTexture();
    expect(typeof OffscreenCanvas).toBe('undefined');
    const texture = getEmblemTexture();
    expect(texture.image.width).toBe(EMBLEM_TEXTURE_SIZE);
    expect(getCanvasStubStats().created).toBeGreaterThan(0);
  });

  it('OffscreenCanvas 存在时优先使用它', () => {
    let calls = 0;
    class FakeOffscreenCanvas {
      width: number;
      height: number;
      constructor(width: number, height: number) {
        calls += 1;
        this.width = width;
        this.height = height;
      }
      getContext(kind: string) {
        return kind === '2d' ? createCanvasStub().getContext('2d') : null;
      }
    }
    Object.defineProperty(globalThis, 'OffscreenCanvas', {
      configurable: true,
      writable: true,
      value: FakeOffscreenCanvas,
    });
    try {
      resetEmblemTexture();
      const texture = getEmblemTexture();
      expect(calls).toBe(1);
      expect(texture.image.width).toBe(EMBLEM_TEXTURE_SIZE);
    } finally {
      Reflect.deleteProperty(globalThis, 'OffscreenCanvas');
      resetEmblemTexture();
    }
  });
});

describe('额标自发光规则', () => {
  it('满血 0.2、空血 1.0 且随血量下降单调变亮', () => {
    expect(emblemIntensity(1)).toBeCloseTo(EMBLEM_INTENSITY_BASE, 10);
    expect(emblemIntensity(0)).toBeCloseTo(EMBLEM_INTENSITY_BASE + EMBLEM_INTENSITY_SPAN, 10);
    let previous = -1;
    for (let step = 10; step >= 0; step -= 1) {
      const value = emblemIntensity(step / 10);
      expect(value).toBeGreaterThan(previous);
      previous = value;
    }
  });

  it('越界血量被夹紧', () => {
    expect(emblemIntensity(-3)).toBeCloseTo(emblemIntensity(0), 10);
    expect(emblemIntensity(4)).toBeCloseTo(emblemIntensity(1), 10);
  });

  it('0.3 秒时间常数平滑且不会过冲', () => {
    const tauMs = EMBLEM_SMOOTHING_TAU_S * 1000;
    const afterTau = emblemSmoothingStep(EMBLEM_INTENSITY_BASE, 1, tauMs);
    const expected = EMBLEM_INTENSITY_BASE + (1 - EMBLEM_INTENSITY_BASE) * (1 - Math.exp(-1));
    expect(afterTau).toBeCloseTo(expected, 6);
    expect(afterTau).toBeLessThan(1);
    expect(emblemSmoothingStep(0.5, 1, 0)).toBe(0.5);
    let current = 0.2;
    for (let i = 0; i < 200; i += 1) current = emblemSmoothingStep(current, 1, 16.7);
    expect(current).toBeGreaterThan(0.99);
    expect(current).toBeLessThanOrEqual(1);
  });
});

describe('额标着色器补丁', () => {
  it('把逐实例自发光强度接入 three 的 lambert 着色器锚点', () => {
    const shader = {
      vertexShader: 'void main() {\n#include <common>\n#include <begin_vertex>\n}',
      fragmentShader: 'void main() {\n#include <common>\n#include <emissivemap_fragment>\n}',
    };
    applyEmblemShaderPatch(shader);
    expect(shader.vertexShader).toContain('instanceEmissiveIntensity');
    expect(shader.vertexShader).toContain('vEmissiveIntensity = instanceEmissiveIntensity');
    expect(shader.fragmentShader).toContain(
      'totalEmissiveRadiance *= vEmissiveTint * vEmissiveIntensity',
    );
  });
});
