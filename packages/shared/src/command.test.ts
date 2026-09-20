import { describe, expect, it } from 'vitest';

import { BUTTON } from './config/index.ts';
import { createCommand, sanitizeCommand, WEAPON_RIFLE, WEAPON_SHOTGUN } from './command.ts';

describe('command 校验', () => {
  it('越界的值被夹取、角度取模、未知按键位清零', () => {
    const out = createCommand();
    sanitizeCommand(
      {
        seq: 70000,
        tick: -5,
        moveX: 9,
        moveY: -9,
        yaw: Math.PI * 4 + 0.5,
        pitch: 3,
        buttons: 0b1111111111111,
        switchTo: 9,
      },
      out,
    );

    expect(out.seq).toBe(70000 % 65536);
    expect(out.tick).toBe(0);
    expect(out.moveX).toBe(1);
    expect(out.moveY).toBe(-1);
    expect(out.yaw).toBeCloseTo(0.5, 12);
    expect(out.pitch).toBeCloseTo(Math.PI / 2, 12);
    expect(out.buttons).toBe(127);
    expect(out.switchTo).toBe(0);
  });

  it('NaN / Infinity / 缺失字段一律归零', () => {
    const out = createCommand();
    sanitizeCommand(
      {
        moveX: Number.NaN,
        moveY: Number.POSITIVE_INFINITY,
        yaw: Number.NaN,
        pitch: Number.NEGATIVE_INFINITY,
        buttons: Number.NaN,
        seq: Number.NaN,
        tick: Number.NaN,
      },
      out,
    );

    expect(out.moveX).toBe(0);
    expect(out.moveY).toBe(0);
    expect(out.yaw).toBe(0);
    expect(out.pitch).toBe(0);
    expect(out.buttons).toBe(0);
    expect(out.seq).toBe(0);
    expect(out.tick).toBe(0);
  });

  it('seq 按 uint16 回绕，合法输入原样保留', () => {
    const out = createCommand();
    sanitizeCommand({ seq: 65536, tick: 12, moveX: 0.5, moveY: -0.25, yaw: -1, pitch: 0.2 }, out);
    expect(out.seq).toBe(0);
    expect(out.tick).toBe(12);
    expect(out.moveX).toBe(0.5);
    expect(out.moveY).toBe(-0.25);
    expect(out.yaw).toBeCloseTo(-1, 12);
    expect(out.pitch).toBeCloseTo(0.2, 12);

    sanitizeCommand({ switchTo: WEAPON_RIFLE, buttons: BUTTON.switchWeapon }, out);
    expect(out.switchTo).toBe(WEAPON_RIFLE);
    sanitizeCommand({ switchTo: WEAPON_SHOTGUN, buttons: BUTTON.switchWeapon }, out);
    expect(out.switchTo).toBe(WEAPON_SHOTGUN);
    sanitizeCommand({ switchTo: WEAPON_SHOTGUN }, out);
    expect(out.switchTo).toBe(0);
  });
});
