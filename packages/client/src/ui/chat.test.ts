import { describe, expect, it } from 'vitest';

import { CHAT_MAX_LINES, createChatBuffer } from './chat.ts';

describe('聊天滚动缓冲', () => {
  it('最多保留 5 行，新的在末尾', () => {
    const buffer = createChatBuffer();
    expect(CHAT_MAX_LINES).toBe(5);
    for (let i = 1; i <= 7; i += 1) buffer.push('line' + String(i));
    expect(buffer.size()).toBe(5);
    expect(buffer.lines()).toEqual(['line3', 'line4', 'line5', 'line6', 'line7']);
  });

  it('清空后没有残留行', () => {
    const buffer = createChatBuffer();
    buffer.push('只有一行');
    buffer.clear();
    expect(buffer.size()).toBe(0);
    expect(buffer.lines()).toEqual([]);
  });

  it('非法行数上限回退为 1', () => {
    const buffer = createChatBuffer(0);
    buffer.push('a');
    buffer.push('b');
    expect(buffer.lines()).toEqual(['b']);
  });
});
