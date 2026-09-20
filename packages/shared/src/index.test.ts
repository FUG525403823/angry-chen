import { describe, expect, it } from 'vitest';

import {
  CONFIG,
  MAX_ENTITIES,
  PROTOCOL_VERSION,
  SERVER_TICK_MS,
  SNAPSHOT_MAX_ENTITIES,
} from './index.ts';

describe('@ac/shared 冻结常量', () => {
  it('协议版本为 1', () => {
    expect(PROTOCOL_VERSION).toBe(1);
  });

  it('服务器 tick 为 50ms，即 20Hz', () => {
    expect(SERVER_TICK_MS).toBe(50);
    expect(1000 / SERVER_TICK_MS).toBe(20);
  });

  it('实体与快照容量常量与配置表一致', () => {
    expect(MAX_ENTITIES).toBe(1024);
    expect(SNAPSHOT_MAX_ENTITIES).toBe(256);
    expect(CONFIG.entity.maxEntities).toBe(MAX_ENTITIES);
    expect(CONFIG.net.snapshotMaxEntities).toBe(SNAPSHOT_MAX_ENTITIES);
  });
});
