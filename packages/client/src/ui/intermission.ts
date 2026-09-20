import { MATCH_PHASE, type MatchState, type MatchStatePlayer } from '@ac/shared';

import { playerCountOf, readyCountOf, selfPlayerOf } from './roster.ts';
import { createWeaponPicker, type WeaponPicker } from './weaponPicker.ts';

export const SKIP_MIN_SECONDS = 5;

export function formatCountdownMs(ms: number): string {
  const clamped = ms > 0 ? ms : 0;
  return (clamped / 1000).toFixed(1) + 's';
}

export function readySummaryText(ready: number, total: number): string {
  return '已准备 ' + String(ready) + '/' + String(total);
}

export function intermissionTitleOf(wave: number): string {
  return wave > 0 ? '第 ' + String(wave) + ' 波已清空 · 波间备战' : '波间备战';
}

export interface IntermissionOptions {
  onReadyChange(ready: boolean, weapon: number): void;
}

export interface Intermission {
  open(): void;
  close(): void;
  applyMatchState(state: MatchState, selfPid: number): void;
  readonly visible: boolean;
  dispose(): void;
}

function button(label: string): HTMLButtonElement {
  const node = document.createElement('button');
  node.type = 'button';
  node.className = 'button';
  node.textContent = label;
  return node;
}

export function createIntermission(root: HTMLElement, options: IntermissionOptions): Intermission {
  root.classList.add('screen', 'screen-hidden');
  const panel = document.createElement('div');
  panel.className = 'panel';
  const title = document.createElement('h2');
  title.className = 'panel-title';
  const countdown = document.createElement('div');
  countdown.className = 'countdown';
  const stats = document.createElement('ul');
  stats.className = 'player-list';
  const picker: WeaponPicker = createWeaponPicker({
    onChange: (slot) => {
      selectedWeapon = slot;
      options.onReadyChange(selfReady, slot);
    },
  });
  const readyButton = button('准备跳过');
  const hint = document.createElement('div');
  hint.className = 'hint';
  hint.textContent = '倒计时结束或全员准备后立即开波（最早 ' + String(SKIP_MIN_SECONDS) + ' 秒）';
  const weaponRow = document.createElement('div');
  weaponRow.className = 'lobby-row';
  const weaponLabel = document.createElement('span');
  weaponLabel.className = 'lobby-label';
  weaponLabel.textContent = '武器';
  weaponRow.append(weaponLabel, picker.element);
  panel.append(title, countdown, stats, weaponRow, readyButton, hint);
  root.append(panel);

  let visible = false;
  let players: readonly MatchStatePlayer[] = [];
  let selfReady = false;
  let selectedWeapon = 0;
  let wave = 0;
  let intermissionMs = 0;

  function render(): void {
    title.textContent = intermissionTitleOf(wave);
    const total = playerCountOf(players);
    const ready = readyCountOf(players);
    countdown.textContent = formatCountdownMs(intermissionMs);
    stats.replaceChildren();
    for (let i = 0; i < players.length; i += 1) {
      const player = players[i];
      if (player === undefined || player.pid <= 0) continue;
      const item = document.createElement('li');
      item.className = 'player-row';
      const name = document.createElement('span');
      name.className = 'player-name';
      name.textContent = player.name;
      const kills = document.createElement('span');
      kills.className = 'player-weapon';
      kills.textContent = '击杀 ' + String(player.kills);
      const status = document.createElement('span');
      status.className = player.ready ? 'player-ready' : 'player-waiting';
      status.textContent = player.downed ? '倒地' : player.ready ? '已准备' : '未准备';
      item.append(name, kills, status);
      stats.append(item);
    }
    picker.setSelected(selectedWeapon);
    readyButton.textContent =
      (selfReady ? '取消准备' : '准备跳过') + '（' + readySummaryText(ready, total) + '）';
    readyButton.classList.toggle('selected', selfReady);
  }

  readyButton.addEventListener('click', () => {
    selfReady = !selfReady;
    options.onReadyChange(selfReady, selectedWeapon);
    render();
  });

  return {
    open(): void {
      visible = true;
      title.textContent = intermissionTitleOf(wave);
      root.classList.remove('screen-hidden');
    },
    close(): void {
      visible = false;
      root.classList.add('screen-hidden');
    },
    get visible(): boolean {
      return visible;
    },
    applyMatchState(state: MatchState, selfPid: number): void {
      if (state.phase !== MATCH_PHASE.intermission) return;
      wave = state.wave;
      intermissionMs = state.intermissionMs;
      players = state.players.slice();
      const mine = selfPlayerOf(players, selfPid);
      if (mine !== undefined) {
        selfReady = mine.ready;
        if (mine.weapon === 1 || mine.weapon === 2) selectedWeapon = mine.weapon;
      }
      render();
    },
    dispose(): void {
      picker.dispose();
      root.replaceChildren();
    },
  };
}
