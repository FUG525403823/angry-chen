import { CanvasTexture, Color, SRGBColorSpace } from 'three';

export const EMBLEM_TEXTURE_SIZE = 128;
export const EMBLEM_DIAMOND_STROKE_PX = 6;
export const EMBLEM_RING_STROKE_PX = 4;
export const EMBLEM_PLANE_M = 0.22;
export const EMBLEM_PLANE_KING_M = 0.36;
export const EMBLEM_MAIN_COLOR = 0xe8e8f0;
export const EMBLEM_EMISSIVE_COLOR = 0x7ad1ff;
export const EMBLEM_EMISSIVE_KING_COLOR = 0xff8a4c;
export const EMBLEM_MAIN_CSS = '#e8e8f0';
export const EMBLEM_EMISSIVE_CSS = '#7ad1ff';
export const EMBLEM_EMISSIVE_KING_CSS = '#ff8a4c';
export const EMBLEM_INTENSITY_BASE = 0.2;
export const EMBLEM_INTENSITY_SPAN = 0.8;
export const EMBLEM_SMOOTHING_TAU_S = 0.3;

const mainColor = new Color(EMBLEM_MAIN_COLOR);
const emissiveColor = new Color(EMBLEM_EMISSIVE_COLOR);
const kingEmissiveColor = new Color(EMBLEM_EMISSIVE_KING_COLOR);

export function clamp01(value: number): number {
  if (!Number.isFinite(value)) return 0;
  if (value < 0) return 0;
  if (value > 1) return 1;
  return value;
}

export function emblemIntensity(hpRatio: number): number {
  return EMBLEM_INTENSITY_BASE + EMBLEM_INTENSITY_SPAN * (1 - clamp01(hpRatio));
}

export function emblemSmoothingStep(current: number, target: number, dtMs: number): number {
  if (!Number.isFinite(dtMs) || dtMs <= 0) return current;
  const alpha = 1 - Math.exp(-(dtMs / 1000) / EMBLEM_SMOOTHING_TAU_S);
  return current + (target - current) * alpha;
}

export function emblemEmissiveTint(isKing: boolean, out: Color): Color {
  return out.copy(isKing ? kingEmissiveColor : emissiveColor);
}

export function emblemDiffuseColor(out: Color): Color {
  return out.copy(mainColor);
}

type EmblemCanvas = HTMLCanvasElement | OffscreenCanvas;
type EmblemContext = CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;

interface EmblemCanvasBundle {
  readonly canvas: EmblemCanvas;
  readonly context: EmblemContext | null;
}

function createEmblemCanvas(size: number): EmblemCanvasBundle {
  if (typeof OffscreenCanvas !== 'undefined') {
    const canvas = new OffscreenCanvas(size, size);
    return { canvas, context: canvas.getContext('2d') };
  }
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  return { canvas, context: canvas.getContext('2d') };
}

function drawEmblem(context: EmblemContext, size: number): void {
  const centre = size / 2;
  context.clearRect(0, 0, size, size);
  context.lineJoin = 'round';
  context.lineCap = 'round';

  context.globalAlpha = 0.34;
  context.strokeStyle = EMBLEM_MAIN_CSS;
  context.lineWidth = EMBLEM_RING_STROKE_PX;
  context.beginPath();
  context.arc(centre, centre, size * 0.43, 0, Math.PI * 2);
  context.stroke();

  context.globalAlpha = 1;
  context.lineWidth = EMBLEM_DIAMOND_STROKE_PX;
  context.beginPath();
  context.moveTo(centre, size * 0.09);
  context.lineTo(size * 0.91, centre);
  context.lineTo(centre, size * 0.91);
  context.lineTo(size * 0.09, centre);
  context.closePath();
  context.stroke();

  context.lineWidth = size * 0.078;
  context.beginPath();
  context.arc(centre, size * 0.31, size * 0.12, Math.PI * 0.86, Math.PI * 2.14);
  context.stroke();
  context.beginPath();
  context.moveTo(centre + size * 0.055, size * 0.47);
  context.lineTo(centre, size * 0.61);
  context.stroke();
  context.fillStyle = EMBLEM_MAIN_CSS;
  context.beginPath();
  context.arc(centre, size * 0.755, size * 0.046, 0, Math.PI * 2);
  context.fill();
}

let cachedEmblemTexture: CanvasTexture<HTMLCanvasElement | OffscreenCanvas> | undefined;

export function getEmblemTexture(): CanvasTexture<HTMLCanvasElement | OffscreenCanvas> {
  if (cachedEmblemTexture !== undefined) return cachedEmblemTexture;
  const bundle = createEmblemCanvas(EMBLEM_TEXTURE_SIZE);
  if (bundle.context !== null) drawEmblem(bundle.context, EMBLEM_TEXTURE_SIZE);
  const texture = new CanvasTexture(bundle.canvas);
  texture.colorSpace = SRGBColorSpace;
  texture.needsUpdate = true;
  cachedEmblemTexture = texture;
  return texture;
}

export function resetEmblemTexture(): void {
  if (cachedEmblemTexture === undefined) return;
  cachedEmblemTexture.dispose();
  cachedEmblemTexture = undefined;
}

export const EMBLEM_SHADER_ANCHORS: readonly string[] = Object.freeze([
  '#include <begin_vertex>',
  '#include <emissivemap_fragment>',
]);

export function applyEmblemShaderPatch(shader: {
  vertexShader: string;
  fragmentShader: string;
}): void {
  shader.vertexShader = shader.vertexShader
    .replace(
      '#include <common>',
      '#include <common>\nattribute vec3 instanceEmissiveTint;\nattribute float instanceEmissiveIntensity;\nvarying vec3 vEmissiveTint;\nvarying float vEmissiveIntensity;',
    )
    .replace(
      '#include <begin_vertex>',
      '#include <begin_vertex>\nvEmissiveTint = instanceEmissiveTint;\nvEmissiveIntensity = instanceEmissiveIntensity;',
    );
  shader.fragmentShader = shader.fragmentShader
    .replace(
      '#include <common>',
      '#include <common>\nvarying vec3 vEmissiveTint;\nvarying float vEmissiveIntensity;',
    )
    .replace(
      '#include <emissivemap_fragment>',
      '#include <emissivemap_fragment>\ntotalEmissiveRadiance *= vEmissiveTint * vEmissiveIntensity;',
    );
}
