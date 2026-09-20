import { describe, expect, it } from 'vitest';

import { PROTOCOL_VERSION, SERVER_TICK_MS } from './index.ts';

describe('@ac/shared 冻结常量', () => {
  it('协议版本为 1', () => {
    expect(PROTOCOL_VERSION).toBe(1);
  });

  it('服务器 tick 为 50ms，即 20Hz', () => {
    expect(SERVER_TICK_MS).toBe(50);
    expect(1000 / SERVER_TICK_MS).toBe(20);
  });
});
