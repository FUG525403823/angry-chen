import { BUTTON, BUTTON_MASK_ALL, INPUT } from './config/index.ts';
import { clamp, wrapAngle } from './math.ts';

export interface Command {
  seq: number;
  tick: number;
  moveX: number;
  moveY: number;
  yaw: number;
  pitch: number;
  buttons: number;
  switchTo: 0 | 1 | 2;
}

export const WEAPON_PISTOL = 0;
export const WEAPON_RIFLE = 1;
export const WEAPON_SHOTGUN = 2;

export function createCommand(): Command {
  return {
    seq: 0,
    tick: 0,
    moveX: 0,
    moveY: 0,
    yaw: 0,
    pitch: 0,
    buttons: 0,
    switchTo: WEAPON_PISTOL,
  };
}

function finite(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function uint16(value: number): number {
  return ((Math.floor(value) % 65536) + 65536) % 65536;
}

export type CommandInput = Partial<Record<keyof Command, unknown>>;

export function sanitizeCommand(input: CommandInput, out: Command): Command {
  out.seq = uint16(finite(input.seq));
  out.tick = Math.max(0, Math.floor(finite(input.tick)));
  out.moveX = clamp(finite(input.moveX), -INPUT.moveAxisLimit, INPUT.moveAxisLimit);
  out.moveY = clamp(finite(input.moveY), -INPUT.moveAxisLimit, INPUT.moveAxisLimit);
  out.yaw = wrapAngle(finite(input.yaw));
  out.pitch = clamp(wrapAngle(finite(input.pitch)), -INPUT.pitchLimitRad, INPUT.pitchLimitRad);
  out.buttons = Math.floor(finite(input.buttons)) & BUTTON_MASK_ALL;
  const weapon = Math.floor(finite(input.switchTo));
  const requested =
    weapon === WEAPON_RIFLE
      ? WEAPON_RIFLE
      : weapon === WEAPON_SHOTGUN
        ? WEAPON_SHOTGUN
        : WEAPON_PISTOL;
  out.switchTo = (out.buttons & BUTTON.switchWeapon) !== 0 ? requested : WEAPON_PISTOL;
  return out;
}
