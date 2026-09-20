import { describe, expect, it } from 'vitest';

import { buildHealthPayload, buildWelcomeFrame } from './index.ts';

describe('服务端载荷构造', () => {
  it('健康检查载荷携带协议版本与 tick', () => {
    expect(buildHealthPayload(1000, 2500)).toEqual({
      ok: true,
      protocolVersion: 1,
      tickMs: 50,
      uptimeMs: 1500,
    });
  });

  it('运行时长不会被算成负数', () => {
    expect(buildHealthPayload(5000, 1000).uptimeMs).toBe(0);
  });

  it('欢迎帧可被 JSON 序列化且字段完整', () => {
    const frame = buildWelcomeFrame(1234.6);
    expect(JSON.parse(JSON.stringify(frame))).toEqual({
      type: 'welcome',
      protocolVersion: 1,
      tickMs: 50,
      serverTimeMs: 1235,
    });
  });
});
