export const SUBTITLE_MS = 2600;
export const SUBTITLE_MAX_LINES = 3;

export type SubtitleCue = 'chargeWarn' | 'waveStart' | 'downed' | 'victory' | 'defeat';

export const SUBTITLE_CUES: readonly SubtitleCue[] = Object.freeze([
  'chargeWarn',
  'waveStart',
  'downed',
  'victory',
  'defeat',
]);

export const SUBTITLE_TEXT: Readonly<Record<SubtitleCue, string>> = Object.freeze({
  chargeWarn: '冲锋预警：侧移躲避',
  waveStart: '波次开始',
  downed: '你已倒地，等待救援',
  victory: '胜利：守住牧场',
  defeat: '失败：羊群获胜',
});

export function subtitleTextFor(cue: SubtitleCue, wave = 0): string {
  if (cue !== 'waveStart') return SUBTITLE_TEXT[cue];
  if (!Number.isFinite(wave) || wave <= 0) return SUBTITLE_TEXT.waveStart;
  return '第 ' + String(Math.round(wave)) + ' 波开始';
}

interface SubtitleEntry {
  text: string;
  leftMs: number;
}

export interface SubtitleLog {
  push(text: string): void;
  pushCue(cue: SubtitleCue, wave?: number): void;
  update(dtMs: number): void;
  readonly lines: readonly string[];
  clear(): void;
}

export function createSubtitleLog(timeoutMs = SUBTITLE_MS): SubtitleLog {
  const entries: SubtitleEntry[] = [];
  const lines: string[] = [];

  function rebuild(): void {
    lines.length = 0;
    for (let i = 0; i < entries.length; i += 1) {
      const entry = entries[i];
      if (entry === undefined) continue;
      lines.push(entry.text);
    }
  }

  function push(text: string): void {
    if (text === '') return;
    for (let i = 0; i < entries.length; i += 1) {
      const entry = entries[i];
      if (entry === undefined) continue;
      if (entry.text !== text) continue;
      entry.leftMs = timeoutMs;
      rebuild();
      return;
    }
    entries.unshift({ text, leftMs: timeoutMs });
    if (entries.length > SUBTITLE_MAX_LINES) entries.length = SUBTITLE_MAX_LINES;
    rebuild();
  }

  return {
    push,
    pushCue(cue: SubtitleCue, wave = 0): void {
      push(subtitleTextFor(cue, wave));
    },
    update(dtMs: number): void {
      if (!Number.isFinite(dtMs) || dtMs <= 0) return;
      let changed = false;
      for (let i = entries.length - 1; i >= 0; i -= 1) {
        const entry = entries[i];
        if (entry === undefined) continue;
        entry.leftMs -= dtMs;
        if (entry.leftMs > 0) continue;
        entries.splice(i, 1);
        changed = true;
      }
      if (changed) rebuild();
    },
    get lines(): readonly string[] {
      return lines;
    },
    clear(): void {
      entries.length = 0;
      rebuild();
    },
  };
}

export interface SubtitleOverlay {
  readonly element: HTMLDivElement;
  render(): void;
  update(dtMs: number): void;
  dispose(): void;
}

export function createSubtitleOverlay(host: HTMLElement, log: SubtitleLog): SubtitleOverlay {
  const element = document.createElement('div');
  element.className = 'subtitle-layer';
  const lineNodes: HTMLDivElement[] = [];
  const lastText: string[] = [];
  for (let i = 0; i < SUBTITLE_MAX_LINES; i += 1) {
    const line = document.createElement('div');
    line.className = 'subtitle-line';
    element.append(line);
    lineNodes.push(line);
    lastText.push('');
  }
  host.append(element);

  function render(): void {
    let visible = false;
    for (let i = 0; i < lineNodes.length; i += 1) {
      const node = lineNodes[i];
      if (node === undefined) continue;
      const text = log.lines[i] ?? '';
      if (lastText[i] !== text) {
        lastText[i] = text;
        node.textContent = text;
        node.classList.toggle('subtitle-line-visible', text !== '');
      }
      if (text !== '') visible = true;
    }
    element.classList.toggle('subtitle-layer-active', visible);
  }

  render();

  return {
    element,
    render,
    update(dtMs: number): void {
      const before = log.lines.join('|');
      log.update(dtMs);
      if (log.lines.join('|') !== before) render();
    },
    dispose(): void {
      log.clear();
      element.remove();
    },
  };
}
