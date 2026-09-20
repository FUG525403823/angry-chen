export interface CanvasStubStats {
  created: number;
  ops: number;
}

export interface CanvasStubContext {
  canvas: { width: number; height: number };
  fillStyle: string | CanvasGradient;
  strokeStyle: string | CanvasGradient;
  lineWidth: number;
  lineJoin: string;
  lineCap: string;
  globalAlpha: number;
  font: string;
  textAlign: string;
  clearRect(x: number, y: number, width: number, height: number): void;
  fillRect(x: number, y: number, width: number, height: number): void;
  strokeRect(x: number, y: number, width: number, height: number): void;
  beginPath(): void;
  closePath(): void;
  moveTo(x: number, y: number): void;
  lineTo(x: number, y: number): void;
  arc(x: number, y: number, radius: number, start: number, end: number): void;
  quadraticCurveTo(cpx: number, cpy: number, x: number, y: number): void;
  fill(): void;
  stroke(): void;
  save(): void;
  restore(): void;
  translate(x: number, y: number): void;
  rotate(angle: number): void;
  scale(x: number, y: number): void;
  setLineDash(segments: readonly number[]): void;
  fillText(text: string, x: number, y: number): void;
  createRadialGradient(
    x0: number,
    y0: number,
    r0: number,
    x1: number,
    y1: number,
    r1: number,
  ): { addColorStop(offset: number, color: string): void };
  createLinearGradient(
    x0: number,
    y0: number,
    x1: number,
    y1: number,
  ): {
    addColorStop(offset: number, color: string): void;
  };
}

export interface CanvasStub {
  width: number;
  height: number;
  getContext(kind: string): CanvasStubContext | null;
}

const stats: CanvasStubStats = { created: 0, ops: 0 };

export function getCanvasStubStats(): CanvasStubStats {
  return stats;
}

export function resetCanvasStubStats(): void {
  stats.created = 0;
  stats.ops = 0;
}

function count(): void {
  stats.ops += 1;
}

function createStubContext(canvas: { width: number; height: number }): CanvasStubContext {
  return {
    canvas,
    fillStyle: '#000000',
    strokeStyle: '#000000',
    lineWidth: 1,
    lineJoin: 'miter',
    lineCap: 'butt',
    globalAlpha: 1,
    font: '10px sans-serif',
    textAlign: 'start',
    clearRect: count,
    fillRect: count,
    strokeRect: count,
    beginPath: count,
    closePath: count,
    moveTo: count,
    lineTo: count,
    arc: count,
    quadraticCurveTo: count,
    fill: count,
    stroke: count,
    save: count,
    restore: count,
    translate: count,
    rotate: count,
    scale: count,
    setLineDash: count,
    fillText: count,
    createRadialGradient: () => {
      count();
      return { addColorStop: count };
    },
    createLinearGradient: () => {
      count();
      return { addColorStop: count };
    },
  };
}

export function createCanvasStub(width = 300, height = 150): CanvasStub {
  stats.created += 1;
  const canvas: CanvasStub = {
    width,
    height,
    getContext(kind: string): CanvasStubContext | null {
      if (kind !== '2d') return null;
      return createStubContext(canvas);
    },
  };
  return canvas;
}

export interface ElementStub {
  className: string;
  textContent: string;
  readonly style: Record<string, unknown>;
  readonly classList: {
    add(...names: string[]): void;
    remove(...names: string[]): void;
    toggle(name: string, force?: boolean): boolean;
    contains(name: string): boolean;
  };
  readonly attributes: Record<string, string>;
  readonly children: ElementStub[];
  readonly classes: Set<string>;
  parent: ElementStub | null;
  append(...nodes: ElementStub[]): void;
  remove(): void;
  setAttribute(name: string, value: string): void;
  getAttribute(name: string): string | null;
}

export function createElementStub(): ElementStub {
  const classes = new Set<string>();
  const style: Record<string, unknown> = {};
  style.setProperty = (name: string, value: string): void => {
    style[name] = value;
  };
  const element: ElementStub = {
    className: '',
    textContent: '',
    style,
    classes,
    attributes: {},
    children: [],
    parent: null,
    classList: {
      add(...names: string[]): void {
        for (const name of names) classes.add(name);
      },
      remove(...names: string[]): void {
        for (const name of names) classes.delete(name);
      },
      toggle(name: string, force?: boolean): boolean {
        const next = force ?? !classes.has(name);
        if (next) classes.add(name);
        else classes.delete(name);
        return next;
      },
      contains(name: string): boolean {
        return classes.has(name);
      },
    },
    append(...nodes: ElementStub[]): void {
      for (const node of nodes) {
        node.parent = element;
        element.children.push(node);
      }
    },
    remove(): void {
      const parent = element.parent;
      if (parent === null) return;
      const index = parent.children.indexOf(element);
      if (index >= 0) parent.children.splice(index, 1);
      element.parent = null;
    },
    setAttribute(name: string, value: string): void {
      element.attributes[name] = value;
    },
    getAttribute(name: string): string | null {
      return element.attributes[name] ?? null;
    },
  };
  return element;
}

export function installCanvasStub(): void {
  Object.defineProperty(globalThis, 'document', {
    configurable: true,
    writable: true,
    value: {
      createElement(tag: string): unknown {
        return tag === 'canvas' ? createCanvasStub() : createElementStub();
      },
    },
  });
}

export function removeCanvasStub(): void {
  Reflect.deleteProperty(globalThis, 'document');
}

installCanvasStub();
