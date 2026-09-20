import {
  LIMITS,
  MATCH_PHASE,
  NEW_ROOM_CODE,
  isValidRoomCode,
  sanitizeName,
  truncateUtf8,
  type MatchState,
  type MatchStatePlayer,
} from '@ac/shared';

import {
  ERROR_MESSAGES,
  NICKNAME_STORAGE_KEY,
  type ClientCredentialStorage,
} from '../net/connection.ts';
import { allPlayersReady, hostPidOf, selfPlayerOf } from './roster.ts';
import { createWeaponPicker, weaponLabelOf, type WeaponPicker } from './weaponPicker.ts';

export const NICKNAME_MAX_BYTES = LIMITS.maxNameBytes;
export const ROOM_CODE_LENGTH = LIMITS.roomCodeLength;
export const CREATE_ROOM_CODE = NEW_ROOM_CODE;

export function filterNicknameInput(raw: string): string {
  const stripped = raw.replace(/[\u0000-\u001f\u007f<>&"']/g, '');
  return truncateUtf8(stripped, NICKNAME_MAX_BYTES);
}

export function filterRoomCodeInput(raw: string): string {
  const upper = raw.toUpperCase();
  let result = '';
  for (const char of upper) {
    if (!LIMITS.roomCodeAlphabet.includes(char)) continue;
    result += char;
    if (result.length >= ROOM_CODE_LENGTH) break;
  }
  return result;
}

export function nicknameIsValid(nickname: string): boolean {
  return sanitizeName(nickname) !== null;
}

export function roomCodeIsValid(code: string): boolean {
  return isValidRoomCode(code);
}

export function errorMessageFor(code: number, fallback: string): string {
  return ERROR_MESSAGES[code] ?? fallback;
}

export interface LobbyOptions {
  readonly storage?: ClientCredentialStorage | undefined;
  readonly initialNickname: string;
  readonly initialRoomCode: string;
  onJoin(roomCode: string, nickname: string): void;
  onReadyChange(ready: boolean, weapon: number): void;
  onStartMatch(): void;
  onLeave(): void;
}

export interface Lobby {
  open(message?: string): void;
  openRoom(): void;
  close(): void;
  setRoom(roomCode: string, localPid: number): void;
  setNotice(message: string): void;
  readonly visible: boolean;
  applyMatchState(state: MatchState, selfPid: number): void;
  dispose(): void;
}

function button(label: string, className = 'button'): HTMLButtonElement {
  const node = document.createElement('button');
  node.type = 'button';
  node.className = className;
  node.textContent = label;
  return node;
}

function labelledRow(label: string, control: HTMLElement): HTMLElement {
  const row = document.createElement('div');
  row.className = 'lobby-row';
  const text = document.createElement('span');
  text.className = 'lobby-label';
  text.textContent = label;
  row.append(text, control);
  return row;
}

export function createLobby(root: HTMLElement, options: LobbyOptions): Lobby {
  root.classList.add('screen', 'screen-hidden');
  const panel = document.createElement('div');
  panel.className = 'panel';
  const title = document.createElement('h2');
  title.className = 'panel-title';
  title.textContent = '愤怒的陈sir vs 发疯的羊群';

  const entry = document.createElement('div');
  entry.className = 'lobby-view';
  const nicknameInput = document.createElement('input');
  nicknameInput.type = 'text';
  nicknameInput.className = 'text-input';
  nicknameInput.placeholder = '昵称（1–12 字节，可用中英文）';
  nicknameInput.autocomplete = 'off';
  nicknameInput.value = filterNicknameInput(options.initialNickname);
  const codeInput = document.createElement('input');
  codeInput.type = 'text';
  codeInput.className = 'text-input code-input';
  codeInput.placeholder = '房间码（4 位）';
  codeInput.autocomplete = 'off';
  codeInput.value = filterRoomCodeInput(options.initialRoomCode);
  const createButton = button('创建房间');
  const joinButton = button('加入房间');
  const entryButtons = document.createElement('div');
  entryButtons.className = 'lobby-buttons';
  entryButtons.append(createButton, joinButton);
  entry.append(labelledRow('昵称', nicknameInput), labelledRow('房间码', codeInput), entryButtons);

  const room = document.createElement('div');
  room.className = 'lobby-view screen-hidden';
  const roomTitle = document.createElement('div');
  roomTitle.className = 'lobby-room-code';
  const playerList = document.createElement('ul');
  playerList.className = 'player-list';
  const picker: WeaponPicker = createWeaponPicker({
    onChange: (slot) => {
      selectedWeapon = slot;
      options.onReadyChange(selfReady, slot);
    },
  });
  const readyButton = button('准备');
  const startButton = button('开始对局');
  const leaveButton = button('离开房间', 'button button-ghost');
  const roomHint = document.createElement('div');
  roomHint.className = 'hint';
  const roomButtons = document.createElement('div');
  roomButtons.className = 'lobby-buttons';
  roomButtons.append(readyButton, startButton, leaveButton);
  room.append(roomTitle, playerList, labelledRow('武器', picker.element), roomButtons, roomHint);

  const message = document.createElement('div');
  message.className = 'lobby-message';
  panel.append(title, entry, room, message);
  root.append(panel);

  let visible = false;
  let inRoom = false;
  let roomCode = '';
  let selfPid = 0;
  let hostPid = 0;
  let players: readonly MatchStatePlayer[] = [];
  let selfReady = false;
  let selectedWeapon = 0;

  function persistNickname(): void {
    const storage = options.storage;
    if (storage === undefined) return;
    const value = nicknameInput.value;
    if (!nicknameIsValid(value)) return;
    try {
      storage.setItem(NICKNAME_STORAGE_KEY, value);
    } catch {
      return;
    }
  }

  function refreshEntry(): void {
    const nameOk = nicknameIsValid(nicknameInput.value);
    createButton.disabled = !nameOk;
    joinButton.disabled = !nameOk || !roomCodeIsValid(codeInput.value);
  }

  function renderRoom(): void {
    roomTitle.textContent = '房间码 ' + roomCode + '（等房主开始）';
    playerList.replaceChildren();
    for (let i = 0; i < players.length; i += 1) {
      const player = players[i];
      if (player === undefined || player.pid <= 0) continue;
      const item = document.createElement('li');
      item.className = 'player-row';
      const name = document.createElement('span');
      name.className = 'player-name';
      name.textContent =
        player.name +
        (player.pid === hostPid ? '（房主）' : '') +
        (player.pid === selfPid ? '（你）' : '');
      const weapon = document.createElement('span');
      weapon.className = 'player-weapon';
      weapon.textContent = weaponLabelOf(player.weapon);
      const ready = document.createElement('span');
      ready.className = player.ready ? 'player-ready' : 'player-waiting';
      ready.textContent = player.ready ? '已准备' : '未准备';
      item.append(name, weapon, ready);
      playerList.append(item);
    }
    picker.setSelected(selectedWeapon);
    readyButton.textContent = selfReady ? '取消准备' : '准备';
    readyButton.classList.toggle('selected', selfReady);
    const isHost = hostPid !== 0 && hostPid === selfPid;
    startButton.classList.toggle('screen-hidden', !isHost);
    startButton.disabled = !allPlayersReady(players);
    roomHint.textContent = isHost
      ? allPlayersReady(players)
        ? '全员已准备，可以开始'
        : '等待全员准备后开始对局'
      : '房主准备后即可开始';
  }

  nicknameInput.addEventListener('input', () => {
    const filtered = filterNicknameInput(nicknameInput.value);
    if (filtered !== nicknameInput.value) nicknameInput.value = filtered;
    refreshEntry();
  });
  nicknameInput.addEventListener('blur', persistNickname);
  codeInput.addEventListener('input', () => {
    const filtered = filterRoomCodeInput(codeInput.value);
    if (filtered !== codeInput.value) codeInput.value = filtered;
    refreshEntry();
  });
  codeInput.addEventListener('blur', () => {
    const filtered = filterRoomCodeInput(codeInput.value);
    if (filtered !== codeInput.value) codeInput.value = filtered;
    refreshEntry();
  });

  function submit(roomCodeValue: string): void {
    const nickname = nicknameInput.value;
    if (!nicknameIsValid(nickname)) {
      message.textContent = '昵称需为 1–12 字节（中英文、数字、下划线）';
      return;
    }
    persistNickname();
    message.textContent = '正在连接房间…';
    options.onJoin(roomCodeValue, nickname);
  }

  createButton.addEventListener('click', () => submit(CREATE_ROOM_CODE));
  joinButton.addEventListener('click', () => {
    const code = codeInput.value;
    if (!roomCodeIsValid(code)) {
      message.textContent = '房间码需为 4 位大写字符（不含 0/1/I/L/O）';
      return;
    }
    submit(code);
  });
  readyButton.addEventListener('click', () => {
    selfReady = !selfReady;
    options.onReadyChange(selfReady, selectedWeapon);
    renderRoom();
  });
  startButton.addEventListener('click', () => options.onStartMatch());
  leaveButton.addEventListener('click', () => options.onLeave());
  refreshEntry();

  function setView(showRoom: boolean): void {
    entry.classList.toggle('screen-hidden', showRoom);
    room.classList.toggle('screen-hidden', !showRoom);
  }

  return {
    open(text?: string): void {
      visible = true;
      inRoom = false;
      roomCode = '';
      players = [];
      selfReady = false;
      message.textContent = text ?? '';
      setView(false);
      root.classList.remove('screen-hidden');
    },
    openRoom(): void {
      visible = true;
      setView(inRoom);
      root.classList.remove('screen-hidden');
      renderRoom();
    },
    close(): void {
      visible = false;
      root.classList.add('screen-hidden');
    },
    setRoom(code: string, localPid: number): void {
      inRoom = true;
      roomCode = code;
      players = [];
      selfPid = localPid;
      hostPid = 0;
      selfReady = false;
      selectedWeapon = 0;
      setView(true);
      renderRoom();
    },
    setNotice(text: string): void {
      message.textContent = text;
    },
    get visible(): boolean {
      return visible;
    },
    applyMatchState(state: MatchState, localPid: number): void {
      if (!inRoom || state.phase !== MATCH_PHASE.lobby) return;
      selfPid = localPid;
      players = state.players.slice();
      hostPid = hostPidOf(players);
      const mine = selfPlayerOf(players, selfPid);
      if (mine !== undefined) {
        selfReady = mine.ready;
        if (mine.weapon === 1 || mine.weapon === 2) selectedWeapon = mine.weapon;
      }
      renderRoom();
    },
    dispose(): void {
      picker.dispose();
      root.replaceChildren();
    },
  };
}
