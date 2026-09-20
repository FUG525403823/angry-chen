import type { MatchStatePlayer } from '@ac/shared';

import { validateChatText } from '../net/connection.ts';

export const CHAT_MAX_LINES = 5;
export const CHAT_HINT = '回车发送 · 最多 64 字节';

export interface ChatBuffer {
  push(line: string): void;
  lines(): readonly string[];
  size(): number;
  clear(): void;
}

export function createChatBuffer(maxLines: number = CHAT_MAX_LINES): ChatBuffer {
  const limit = maxLines > 0 ? maxLines : 1;
  const lines: string[] = [];
  return {
    push(line: string): void {
      lines.push(line);
      while (lines.length > limit) lines.shift();
    },
    lines(): readonly string[] {
      return lines.slice();
    },
    size(): number {
      return lines.length;
    },
    clear(): void {
      lines.length = 0;
    },
  };
}

export interface ChatOptions {
  onSend(text: string): boolean;
}

export interface Chat {
  readonly element: HTMLElement;
  setPlayers(players: readonly MatchStatePlayer[]): void;
  push(pid: number, text: string): void;
  setVisible(visible: boolean): void;
  clear(): void;
  dispose(): void;
}

export function createChat(options: ChatOptions): Chat {
  const element = document.createElement('div');
  element.className = 'chat chat-hidden';
  const lines = document.createElement('div');
  lines.className = 'chat-lines';
  const form = document.createElement('form');
  form.className = 'chat-form';
  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'chat-input';
  input.placeholder = CHAT_HINT;
  input.autocomplete = 'off';
  const hint = document.createElement('div');
  hint.className = 'chat-hint';
  form.append(input);
  element.append(lines, form, hint);
  const buffer = createChatBuffer();
  const names = new Map<number, string>();

  function render(): void {
    lines.textContent = buffer.lines().join(String.fromCharCode(10));
  }

  function nameOf(pid: number): string {
    return names.get(pid) ?? '玩家' + String(pid);
  }

  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const text = input.value;
    const cleaned = validateChatText(text);
    if (cleaned === null) {
      hint.textContent = text.trim().length === 0 ? '请输入内容' : '消息需在 1–64 字节之间';
      return;
    }
    if (!options.onSend(cleaned)) {
      hint.textContent = '未连接到服务器，无法发送';
      return;
    }
    buffer.push('你：' + cleaned);
    render();
    input.value = '';
    hint.textContent = CHAT_HINT;
  });

  const stopKey = (event: KeyboardEvent): void => {
    event.stopPropagation();
  };
  input.addEventListener('keydown', stopKey);
  input.addEventListener('keyup', stopKey);

  return {
    element,
    setPlayers(players: readonly MatchStatePlayer[]): void {
      names.clear();
      for (let i = 0; i < players.length; i += 1) {
        const player = players[i];
        if (player === undefined || player.pid <= 0) continue;
        names.set(player.pid, player.name);
      }
    },
    push(pid: number, text: string): void {
      const cleaned = validateChatText(text);
      if (cleaned === null) return;
      buffer.push(nameOf(pid) + '：' + cleaned);
      render();
    },
    setVisible(visible: boolean): void {
      element.classList.toggle('chat-hidden', !visible);
    },
    clear(): void {
      buffer.clear();
      render();
    },
    dispose(): void {
      input.removeEventListener('keydown', stopKey);
      input.removeEventListener('keyup', stopKey);
      element.remove();
    },
  };
}
