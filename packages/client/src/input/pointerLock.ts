import type { InputSampler } from './sampler.ts';

export const MOVEMENT_KEYS = new Set(['KeyW', 'KeyS', 'KeyA', 'KeyD', 'Space']);

export interface PointerInputOptions {
  readonly target: HTMLElement;
  readonly sampler: InputSampler;
  onLockChange?(locked: boolean): void;
}

export interface PointerInput {
  readonly locked: boolean;
  request(): void;
  exit(): void;
  dispose(): void;
}

export function createPointerInput(options: PointerInputOptions): PointerInput {
  const { target, sampler } = options;
  let locked = false;

  function setLocked(next: boolean): void {
    if (locked === next) return;
    locked = next;
    options.onLockChange?.(locked);
  }

  function onPointerLockChange(): void {
    setLocked(document.pointerLockElement === target);
  }

  function onPointerLockError(): void {
    setLocked(false);
  }

  function onMouseMove(event: MouseEvent): void {
    if (!locked) return;
    sampler.addMouse(event.movementX ?? 0, event.movementY ?? 0);
  }

  function onKeyDown(event: KeyboardEvent): void {
    if (event.repeat) return;
    if (MOVEMENT_KEYS.has(event.code)) event.preventDefault();
    sampler.setKey(event.code, true);
  }

  function onKeyUp(event: KeyboardEvent): void {
    if (MOVEMENT_KEYS.has(event.code)) event.preventDefault();
    sampler.setKey(event.code, false);
  }

  function onContextMenu(event: Event): void {
    event.preventDefault();
  }

  function onBlur(): void {
    setLocked(false);
  }

  document.addEventListener('pointerlockchange', onPointerLockChange);
  document.addEventListener('pointerlockerror', onPointerLockError);
  document.addEventListener('mousemove', onMouseMove);
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('keyup', onKeyUp);
  window.addEventListener('blur', onBlur);
  target.addEventListener('contextmenu', onContextMenu);

  return {
    get locked(): boolean {
      return locked;
    },
    request(): void {
      const request = target.requestPointerLock();
      if (request !== undefined) void request.catch(() => setLocked(false));
    },
    exit(): void {
      if (document.pointerLockElement === target) document.exitPointerLock();
      setLocked(false);
    },
    dispose(): void {
      document.removeEventListener('pointerlockchange', onPointerLockChange);
      document.removeEventListener('pointerlockerror', onPointerLockError);
      document.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', onBlur);
      target.removeEventListener('contextmenu', onContextMenu);
    },
  };
}
