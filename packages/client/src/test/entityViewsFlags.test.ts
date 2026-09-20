import { SNAPSHOT_FLAG } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { SNAPSHOT_FLAG_DOWNED } from '../render/entityViews.ts';

describe('实体视图镜像的快照标志位', () => {
  it('downed 与 @ac/shared 的 SNAPSHOT_FLAG 保持一致（不得漂移成 rageMode）', () => {
    expect(SNAPSHOT_FLAG_DOWNED).toBe(SNAPSHOT_FLAG.downed);
    expect(SNAPSHOT_FLAG_DOWNED).not.toBe(SNAPSHOT_FLAG.rageMode);
  });
});
