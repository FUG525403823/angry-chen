import { Vector3 } from 'three';

import { formatVersionLine } from './index.ts';

import {
  ARENA,
  BUTTON,
  CONFIG,
  ERROR_CODE,
  type Command,
  HIT_FLAG,
  MATCH_PHASE,
  type MatchStatePlayer,
  type SimEvent,
  WEAPONS,
  WEAPON_SLOT_ORDER,
  createCommand,
  yawPitchToDirection,
} from '@ac/shared';
import { createAudioLayer, outcomeCueForWinnerTeam } from './audio/layer.ts';
import type { ListenerPose } from './audio/mixer.ts';
import { createLocalWeapon } from './combat/localWeapon.ts';
import { createLocalTimers } from './combat/localTimers.ts';
import { createAmmoLedger } from './prediction/ammoLedger.ts';
import { createPointerInput } from './input/pointerLock.ts';
import { createCommandBuffer } from './prediction/commandBuffer.ts';
import { createPredictor } from './prediction/predictor.ts';
import { createLocalAuthority, createReconciler } from './prediction/reconciler.ts';
import { createErrorSmoother } from './render/errorSmoother.ts';
import { createInputSampler } from './input/sampler.ts';
import { createGameConnection, readStoredCredentials } from './net/connection.ts';
import { createSnapshotView } from './net/state.ts';
import { createSheepVisualTracker } from './net/sheepVisual.ts';
import { DEFAULT_SERVER_URL, resolveServerUrl } from './net/serverUrl.ts';
import { type ArenaArtParams, createArenaArt } from './render/arenaArt.ts';
import { createEffects } from './render/effects.ts';
import { createFrameProfiler } from './render/frameProfiler.ts';
import { type SheepVisual, createEntityViews } from './render/entityViews.ts';
import {
  type ArtPalette,
  COLORBLIND_PALETTE,
  DEFAULT_PALETTE,
  HIT_FLASH_TOTAL_MS,
  createMaterials,
  hitFlashAmount,
} from './render/materials.ts';
import { PARTICLE_EMITTER, createParticles } from './render/particles.ts';
import { createRenderer } from './render/renderer.ts';
import { applyFov, createScene, muzzleOrigin, updateCamera } from './render/scene.ts';
import { createViewModelModel } from './render/viewmodelModel.ts';
import { createSettingsStore, type SettingsStorage } from './settings/store.ts';
import { createChat } from './ui/chat.ts';
import { type DebugSample, createDebugPanel } from './ui/debugPanel.ts';
import { type HudUpdate, createCombatHud, createHud } from './ui/hud.ts';
import { createIntermission } from './ui/intermission.ts';
import { createLobby, errorMessageFor, filterRoomCodeInput } from './ui/lobby.ts';
import { createResults, type ResultsSummary } from './ui/results.ts';
import { applyAccessibilityClasses, createSettingsPanel } from './ui/settings.ts';

export const DEFAULT_PLAYER_NAME = '陈sir';
export const PHASE_NAMES: readonly string[] = ['大厅', '加载中', '战斗中', '波间备战', '结算'];
export const SHEEP_LABELS: readonly string[] = ['咩咩兵', '冲撞羊', '问界羊', '羊王'];
export const FALLBACK_CAMERA = { x: 0, y: 3, z: 18 };
/** 本地曳光可视化长度（米）。命中后另有「枪口 → 命中点」的真实曳光。 */
export const TRACER_RANGE_M = 30;
const projectScratch = new Vector3();
const fireOriginScratch = new Vector3();
const listenerForward = new Vector3();
const listenerPose = {
  x: 0,
  y: 0,
  z: 0,
  forwardX: 0,
  forwardY: 0,
  forwardZ: -1,
  upX: 0,
  upY: 1,
  upZ: 0,
} satisfies ListenerPose;

function safeLocalStorage(): SettingsStorage | undefined {
  try {
    const storage = window.localStorage;
    if (storage === null) return undefined;
    return storage;
  } catch {
    return undefined;
  }
}

function requireElement(id: string): HTMLElement {
  const element = document.getElementById(id);
  if (element === null) throw new Error('missing DOM root #' + id);
  return element;
}

export function boot(): void {
  const app = requireElement('app');
  const hudRoot = requireElement('hud');
  const bannerRoot = requireElement('banner');
  const debugRoot = requireElement('debug');
  const pauseRoot = requireElement('pause-overlay');
  const lobbyRoot = requireElement('lobby');
  const intermissionRoot = requireElement('intermission');
  const resultsRoot = requireElement('results');
  const chatRoot = requireElement('chat');

  const params = new URLSearchParams(window.location.search);
  const serverUrl = resolveServerUrl({
    search: window.location.search,
    protocol: window.location.protocol,
    host: window.location.host,
    isDev: import.meta.env.DEV,
    fallback: DEFAULT_SERVER_URL,
  });
  const storage = safeLocalStorage();
  const stored = readStoredCredentials(storage, params.get('name') ?? DEFAULT_PLAYER_NAME);
  const initialRoomCode = filterRoomCodeInput(params.get('room') ?? stored.roomCode);

  const settings = createSettingsStore(storage);
  const { scene, camera } = createScene(settings.get().fov);
  let palette: ArtPalette = settings.get().colorblindSafe ? COLORBLIND_PALETTE : DEFAULT_PALETTE;
  const materials = createMaterials(palette);
  const arenaParams: ArenaArtParams = {
    halfSize: ARENA.halfSize,
    fenceHeight: ARENA.fence.height,
    fenceThickness: ARENA.fence.thickness,
    barn: ARENA.barn,
  };
  const markers = [
    ...ARENA.playerSpawnPoints.map((point) => ({ x: point.x, z: point.z, team: 0 })),
    ...ARENA.enemySpawnPoints.map((point) => ({ x: point.x, z: point.z, team: 1 })),
  ];
  const arenaArt = createArenaArt(arenaParams, markers, materials, palette);
  scene.add(arenaArt.root);

  const renderer = createRenderer();
  app.replaceChildren(renderer.domElement);

  const particles = createParticles(scene, camera, materials, {
    isReduceMotion: () => settings.get().reduceMotion,
  });

  const view = createSnapshotView();
  const audioWorld = { source: view, localPlayerId: 0 };
  const moveConfig = {
    player: CONFIG.player,
    arena: CONFIG.arena,
    radius: CONFIG.entity.radiusByKind.player,
  };
  const predictor = createPredictor(moveConfig);
  const commands = createCommandBuffer();
  const reconciler = createReconciler();
  const smoother = createErrorSmoother();
  const authority = createLocalAuthority();
  const predictCommand: Command = createCommand();
  const renderPos = { x: 0, y: 0, z: 0 };
  let predictionReady = false;
  const viewModel = createViewModelModel(materials);
  const localWeapon = createLocalWeapon();
  const sheepVisual = createSheepVisualTracker();

  /** O06：提升为模块级函数，避免每帧新建闭包。 */
  function resolveSheepVisual(id: number, state: number): SheepVisual {
    return sheepVisual.resolve(id, state);
  }
  const combatHud = createCombatHud(hudRoot);
  const audioUi = document.createElement('div');
  audioUi.className = 'audio-ui';
  hudRoot.append(audioUi);
  const audioLayer = createAudioLayer({
    onSubtitle: (text) => combatHud.pushSubtitle(text),
  });
  const fx = createEffects(scene, particles, materials, hudRoot, {
    isReduceMotion: () => settings.get().reduceMotion,
    onChargeWindup: (id) => {
      combatHud.showChargeWarning();
      const warnings = views.chargeWarnings;
      for (let i = 0; i < warnings.length; i += 1) {
        const warning = warnings[i];
        if (warning === undefined || warning.id !== id) continue;
        audioLayer.notifyChargeWarning({ x: warning.x, y: 0, z: warning.z });
        return;
      }
      audioLayer.notifyChargeWarning();
    },
  });
  const settingsPanel = createSettingsPanel(audioUi, {
    store: settings,
    accessibilityRoot: document.body,
    onUiSound: () => audioLayer.notifyUi(),
  });
  const MAG_SIZES = [WEAPONS.pistol.mag, WEAPONS.rifle.mag, WEAPONS.shotgun.mag];
  const WEAPON_NAMES = ['手枪', '步枪', '霰弹枪'];
  const combatState = {
    spreadDeg: 0,
    hpRatio: 1,
    armorRatio: undefined as number | undefined,
    mag: WEAPONS.pistol.mag,
    magSize: WEAPONS.pistol.mag,
    reserve: 120,
    reloadRatio: 0,
    rage: 0,
    rageLeftMs: 0,
    downed: false,
    reviveRatio: undefined as number | undefined,
    weaponName: '手枪',
    wave: 0,
    bossHpRatio: undefined as number | undefined,
  };
  let reloadLeft10Ms = 0;
  const ammoLedger = createAmmoLedger();
  const localTimers = createLocalTimers();
  let authorityMag = -1;
  /** 本地"真的打出一发"的累计次数（调试读数：与服务端消耗对拍）。 */
  let localShotCount = 0;
  let authorityReserve = 0;
  let ammoPendingShots = 0;
  let ammoDivergence = 0;
  let ammoResyncCount = 0;
  let meshFlashLeftMs = 0;
  let weaponSlot: 0 | 1 | 2 = 0;
  let fireHeld = false;
  const tracerEnd = { x: 0, y: 0, z: 0 };
  const forwardScratch = new Vector3();
  let lastFrameMs = 0;
  const views = createEntityViews(scene, view, () => view.localPlayerId, materials, palette);
  combatHud.setProjector((x, y, z) => {
    projectScratch.set(x, y, z).project(camera);
    if (projectScratch.z > 1) return undefined;
    return {
      x: (projectScratch.x * 0.5 + 0.5) * window.innerWidth,
      y: (-projectScratch.y * 0.5 + 0.5) * window.innerHeight,
    };
  });
  const hud = createHud(hudRoot, bannerRoot);
  const debugEnabled = params.get('debug') === '1';
  const debug = createDebugPanel(debugRoot, debugEnabled);
  // 每帧分阶段计时：F3 面板展示，「画面卡顿」排查与 tools/e2e-cdp.mjs 采集用。
  const profiler = createFrameProfiler();

  const sampler = createInputSampler({
    now: () => performance.now(),
    getActiveSlot: () => weaponSlot,
    emit: (command) => {
      predictCommand.seq = command.seq;
      predictCommand.tick = command.tick;
      predictCommand.moveX = command.moveX;
      predictCommand.moveY = command.moveY;
      predictCommand.yaw = command.yaw;
      predictCommand.pitch = command.pitch;
      // 倒地时不再扣扳机：服务器本来就会忽略倒地玩家的开火命令（resolveCombat 直接 continue），
      // 而本地若继续走开火路径就会出现"有枪口火焰/曳光/扣弹、但打不出伤害"的假象。
      predictCommand.buttons =
        command.buttons | (fireHeld && pointer.locked && !combatState.downed ? BUTTON.fire : 0);
      predictCommand.switchTo = command.switchTo;
      if (!pointer.locked || !predictionReady) return;
      commands.push(predictCommand);
      connection.sendCommand(predictCommand);
      if ((predictCommand.buttons & BUTTON.fire) !== 0) {
        // 与服务器射线共用同一函数：客户端准星方向 = 权威命中方向。
        yawPitchToDirection(forwardScratch, sampler.state.yaw, sampler.state.pitch);
        // 曳光与枪口火焰同门：只有真的打出一发（onFire 为真）才产生弹道。
        // 空弹匣 / 换弹中 / 射速节流内点左键都不该有弹道（真人试玩反馈「没子弹还有弹道」）。
        if (localWeapon.onFire(performance.now())) {
          ammoLedger.noteLocalShot(predictCommand.seq, weaponSlot, performance.now());
          localShotCount += 1;
          viewModel.triggerFire();
          audioLayer.notifyFire(weaponSlot);
          muzzleOrigin(fireOriginScratch, camera, forwardScratch);
          fx.spawnMuzzleFlash(
            fireOriginScratch.x,
            fireOriginScratch.y,
            fireOriginScratch.z,
            forwardScratch.x,
            forwardScratch.y,
            forwardScratch.z,
          );
          fx.triggerShake(0.03);
          tracerEnd.x = fireOriginScratch.x + forwardScratch.x * TRACER_RANGE_M;
          tracerEnd.y = fireOriginScratch.y + forwardScratch.y * TRACER_RANGE_M;
          tracerEnd.z = fireOriginScratch.z + forwardScratch.z * TRACER_RANGE_M;
          fx.spawnTracer(
            fireOriginScratch.x,
            fireOriginScratch.y,
            fireOriginScratch.z,
            tracerEnd.x,
            tracerEnd.y,
            tracerEnd.z,
            true,
          );
        }
      }
    },
    getTick: () => view.getAppliedTick(),
  });

  let inRoom = false;
  let lastPlayers: readonly MatchStatePlayer[] = [];
  let matchEndWinnerTeam = 0;
  let matchEndWave = 0;
  let matchEndDurationMs = 0;

  function backToLobby(message: string): void {
    inRoom = false;
    hud.showBanner(message);
    lobby.open(message);
    chat.setVisible(false);
    intermission.close();
    results.hide();
  }

  function publishResults(players: readonly MatchStatePlayer[]): void {
    const rows: { name: string; kills: number }[] = [];
    for (const player of players) {
      if (player.pid <= 0) continue;
      rows.push({ name: player.name, kills: player.kills });
    }
    const summary: ResultsSummary = {
      winnerTeam: matchEndWinnerTeam,
      waveReached: matchEndWave,
      durationMs: matchEndDurationMs,
      players: rows,
    };
    results.setSummary(summary);
    results.show();
  }

  function showPhase(phase: number): void {
    if (!inRoom) return;
    if (phase === MATCH_PHASE.lobby) {
      lobby.openRoom();
      intermission.close();
      results.hide();
      chat.setVisible(true);
      return;
    }
    if (phase === MATCH_PHASE.intermission) {
      lobby.close();
      intermission.open();
      results.hide();
      chat.setVisible(true);
      return;
    }
    if (phase === MATCH_PHASE.ended) {
      lobby.close();
      intermission.close();
      chat.setVisible(true);
      results.show();
      return;
    }
    lobby.close();
    intermission.close();
    results.hide();
    chat.setVisible(true);
  }

  /** O06：每帧复用的事件名数组（避免 `events.map(...)` 每次新建）。 */
  const eventNames: string[] = [];

  const connection = createGameConnection({
    url: serverUrl,
    view,
    storage,
    onStatus: (status) => {
      hud.setStatus(status);
      if (!inRoom) return;
      if (status === 'error') hud.showBanner('连接错误，正在重连…');
      else if (status === 'closed') hud.showBanner('连接断开，正在重连…');
    },
    onWelcome: (welcome) => {
      hud.setRoomCode(welcome.roomCode);
      inRoom = true;
      lobby.setRoom(welcome.roomCode, welcome.pid);
      chat.clear();
      chat.setVisible(true);
      intermission.close();
      results.hide();
      commands.clear();
      smoother.reset();
      reconciler.reset();
      ammoLedger.reset();
      authorityMag = -1;
      predictionReady = false;
    },
    onSnapshotApplied: () => {
      view.getLocalAuthority(authority);
      if (!authority.found) return;
      predictionReady = true;
      reconciler.reconcile(authority, commands, predictor, smoother);
      ammoLedger.noteServerAck(authority.lastAckedSeq);
    },
    onMatchState: (state) => {
      hud.setPhase(PHASE_NAMES[state.phase] ?? '未知');
      combatState.wave = state.wave;
      const mine = view.localPlayerId;
      for (let i = 0; i < state.players.length; i += 1) {
        const player = state.players[i];
        if (player === undefined || player.pid !== mine) continue;
        weaponSlot = player.weapon === 1 ? 1 : player.weapon === 2 ? 2 : 0;
        combatState.hpRatio = player.hpRatio;
        combatState.magSize = MAG_SIZES[weaponSlot] ?? WEAPONS.pistol.mag;
        combatState.rage = player.rage;
        combatState.rageLeftMs = player.rageLeft100Ms * 100;
        combatState.downed = player.downed;
        combatState.weaponName = WEAPON_NAMES[weaponSlot] ?? '手枪';
        const wasReloading = reloadLeft10Ms > 0;
        reloadLeft10Ms = player.reloadLeft10Ms;
        if (!wasReloading && reloadLeft10Ms > 0) audioLayer.notifyReload();
        authorityMag = player.mag;
        authorityReserve = player.reserve;
        localTimers.resyncReload(
          player.reloadLeft10Ms,
          WEAPONS[WEAPON_SLOT_ORDER[weaponSlot] ?? 'pistol'].reloadMs,
        );
        localTimers.resyncRage(player.rageLeft100Ms);
        ammoResyncCount += 1;
        applyAmmo();
        localWeapon.setSlot(weaponSlot, performance.now());
        viewModel.setWeapon(weaponSlot);
      }
      lastPlayers = state.players;
      chat.setPlayers(state.players);
      lobby.applyMatchState(state, mine);
      intermission.applyMatchState(state, mine);
      if (state.phase === MATCH_PHASE.ended) publishResults(state.players);
      showPhase(state.phase);
    },
    onEvents: (events) => {
      eventNames.length = 0;
      for (let i = 0; i < events.length; i += 1) {
        const event = events[i];
        if (event !== undefined) eventNames.push(event.type);
      }
      hud.pushEvents(eventNames);
      audioLayer.handleEvents(events);
      for (let i = 0; i < events.length; i += 1) {
        const event = events[i];
        if (event === undefined) continue;
        if (event.type === 'matchEnded') {
          matchEndWinnerTeam = event.subjectId === 1 ? 1 : 0;
          matchEndWave = event.value;
          matchEndDurationMs = event.flags;
          combatHud.pushSubtitleCue(outcomeCueForWinnerTeam(matchEndWinnerTeam));
          publishResults(lastPlayers);
        }
        handleCombatEvent(event);
      }
    },
    onChat: (message) => chat.push(message.pid, message.text),
    onReturnToLobby: (message) => backToLobby(message),
    onServerError: (code, message) => {
      const text = errorMessageFor(code, message);
      if (
        code === ERROR_CODE.roomNotFound ||
        code === ERROR_CODE.roomFull ||
        code === ERROR_CODE.matchInProgress ||
        code === ERROR_CODE.invalidName
      ) {
        backToLobby('错误 ' + String(code) + '：' + text);
        return;
      }
      if (code === ERROR_CODE.notHost) lobby.setNotice(text);
      hud.showBanner('错误 ' + String(code) + '：' + text);
    },
  });

  const chat = createChat({ onSend: (text) => connection.sendChat(text) });
  chatRoot.append(chat.element);
  chat.setVisible(false);
  const lobby = createLobby(lobbyRoot, {
    storage,
    initialNickname: stored.nickname,
    initialRoomCode,
    onJoin: (roomCode, nickname) => {
      connection.joinRoom(roomCode, nickname);
    },
    onReadyChange: (ready, weapon) => connection.sendReady(ready, weapon),
    onStartMatch: () => connection.sendStartMatch(),
    onLeave: () => {
      connection.leaveRoom();
      inRoom = false;
      chat.clear();
      chat.setVisible(false);
      lobby.open('已离开房间');
    },
  });
  const intermission = createIntermission(intermissionRoot, {
    onReadyChange: (ready, weapon) => connection.sendReady(ready, weapon),
  });
  const results = createResults(resultsRoot, {
    onReturnToLobby: () => {
      results.hide();
      lobby.openRoom();
      chat.setVisible(true);
    },
  });
  lobby.open();
  intermission.close();
  results.hide();

  const pointer = createPointerInput({
    target: renderer.domElement,
    sampler,
    onLockChange: (locked) => {
      pauseRoot.classList.toggle('hidden', locked);
      if (locked) connection.sendReady(true);
    },
  });

  function handleCombatEvent(event: SimEvent | undefined): void {
    if (event === undefined) return;
    const mine = view.localPlayerId;
    if (event.type === 'playerHit') {
      const headshot = (event.flags & HIT_FLAG.headshot) !== 0;
      if (event.subjectId === mine && event.targetId !== mine) {
        fx.showHitMarker(headshot);
        combatHud.pushDamage(event.x, event.y, event.z, event.value, headshot);
        fx.spawnImpact(event.x, event.y, event.z);
        views.flashHit(event.targetId);
        // 一次开火只画一条曳光（弹道由开火路径的本地预测曳光负责）。此处曾再补一条权威命中曳光，
        // 于是同一发子弹出现两条线：起点用事件到达时的相机重算、终点是模拟命中体入射点（比渲染体小），
        // 再叠加弹丸散布，看起来就是「一条正常一条随机」（真人试玩反馈）。
        // 命中反馈交给命中标记 / 伤害数字 / 弹着点，不再补画曳光。
        return;
      }
      if (event.targetId === mine) {
        meshFlashLeftMs = HIT_FLASH_TOTAL_MS;
        fx.pulseHitFlash();
        fx.triggerShake(0.08);
        combatHud.pushKill('你受到 ' + String(Math.round(event.value)) + ' 点伤害', false);
      }
      return;
    }
    if (event.type === 'sheepKilled') {
      fx.spawnDeathDebris(event.x, event.y, event.z);
      particles.emit(PARTICLE_EMITTER.wool, event.x, event.y + 0.6, event.z, 10, 0, 0.6, 0);
      const form = sheepVisual.formOf(event.targetId) ?? 0;
      const who = event.subjectId === mine ? '你击杀 ' : '队友击杀 ';
      combatHud.pushKill(
        who + (SHEEP_LABELS[form] ?? '咩咩兵'),
        (event.flags & HIT_FLAG.headshot) !== 0,
      );
      sheepVisual.forget(event.targetId);
      return;
    }
    if (event.type === 'playerDowned' && event.subjectId === mine) {
      combatHud.pushKill('你已倒地，等待救援', false);
      return;
    }
    if (event.type === 'reviveProgress') {
      combatState.reviveRatio = event.subjectId === mine ? event.value / 1000 : undefined;
      return;
    }
    if (event.type === 'reviveDone') {
      if (event.subjectId === mine || event.targetId === mine) combatState.reviveRatio = undefined;
      if (event.targetId === mine) combatHud.pushKill('已被救起', false);
      return;
    }
    if (event.type === 'rageActivated' && event.subjectId === mine) {
      combatHud.pushKill('狂暴已激活', false);
    }
  }

  function applySettings(): void {
    const current = settings.get();
    applyFov(camera, current.fov);
    viewModel.setFov(current.fov);
    sampler.setSensitivity(current.sensitivity);
    palette = current.colorblindSafe ? COLORBLIND_PALETTE : DEFAULT_PALETTE;
    materials.setPalette(palette);
    views.setPalette(palette);
    arenaArt.setPalette(palette);
    applyAccessibilityClasses(document.body, current);
    audioLayer.setVolumes(current.masterVolume, current.sfxVolume);
  }
  applySettings();
  settings.subscribe(applySettings);

  const listeners = new AbortController();
  const { signal } = listeners;

  function resize(): void {
    const width = window.innerWidth;
    const height = window.innerHeight;
    renderer.setSize(width, height);
    camera.aspect = height === 0 ? 1 : width / height;
    camera.updateProjectionMatrix();
    viewModel.setViewport(width, height);
  }
  resize();
  window.addEventListener('resize', resize, { signal });

  renderer.domElement.addEventListener(
    'mousedown',
    (event) => {
      if (event.button === 0) fireHeld = true;
    },
    { signal },
  );
  window.addEventListener(
    'mouseup',
    (event) => {
      if (event.button === 0) fireHeld = false;
    },
    { signal },
  );

  renderer.domElement.addEventListener(
    'click',
    () => {
      if (pointer.locked) return;
      unlockAudio();
      pointer.request();
    },
    { signal },
  );

  let audioUnlocked = false;
  function unlockAudio(): void {
    if (audioUnlocked) return;
    if (!audioLayer.attach()) return;
    audioUnlocked = true;
    window.removeEventListener('pointerdown', unlockAudio);
    window.removeEventListener('keydown', unlockAudio);
  }
  window.addEventListener('pointerdown', unlockAudio, { signal });
  window.addEventListener('keydown', unlockAudio, { signal });

  window.addEventListener(
    'keydown',
    (event) => {
      if (event.code === 'F3') {
        event.preventDefault();
        debug.toggle();
        return;
      }
      if (event.code === 'KeyO') {
        event.preventDefault();
        settingsPanel.toggle();
        audioLayer.notifyUi();
      }
    },
    { signal },
  );

  pauseRoot.classList.remove('hidden');
  pauseRoot.textContent = '点击画面锁定鼠标（F3 调试面板）';

  type Mutable<T> = { -readonly [K in keyof T]: T[K] };

  /** O06：每帧复用入参对象（字段逐个覆写，不再新建字面量）。 */
  const hudUpdateScratch: Mutable<HudUpdate> = {
    local: undefined,
    stats: view.getStats(),
    fps: 0,
    liveEntities: 0,
    localPos: undefined,
  };
  const debugUpdateScratch: Mutable<DebugSample> = {
    pendingShots: 0,
    rejectedShots: 0,
    ammoDivergence: 0,
    resyncCount: 0,
    fps: 0,
    p95IntervalMs: 0,
    p95WorkMs: 0,
    tick: 0,
    snapshotsPerSec: 0,
    inboundBytesPerSec: 0,
    rttMs: 0,
    serverRateX10: 0,
    liveEntities: 0,
    pooledEntities: 0,
    localPos: undefined,
    status: '',
    roomCode: '',
    versionLine: '',
    predictionErrorM: 0,
    predictionMaxErrorM: 0,
    hardCorrects: 0,
    pendingCommands: 0,
    drawCalls: 0,
    triangles: 0,
    particles: 0,
    materialCount: 0,
  };

  /** O07：用账本裁决结果刷新本地弹药显示（每帧调用，ack 到达后立刻反映）。 */
  function applyAmmo(nowMs: number = performance.now()): void {
    if (authorityMag < 0) return;
    const view = ammoLedger.reconcile(authorityMag, authorityReserve, weaponSlot, nowMs);
    localWeapon.reconcileAmmo(weaponSlot, authorityMag, authorityReserve, view);
    combatState.mag = view.mag < combatState.magSize ? view.mag : combatState.magSize;
    combatState.reserve = view.reserve;
    ammoPendingShots = view.pending;
    ammoDivergence = view.mag - authorityMag;
  }

  let frameErrors = 0;
  function frame(nowMs: number): void {
    try {
      frameBody(nowMs);
    } catch (error) {
      frameErrors += 1;
      if (frameErrors === 1) hud.showBanner('渲染异常：' + String(error));
    }
    window.requestAnimationFrame(frame);
  }

  function frameBody(nowMs: number): void {
    profiler.begin();
    sampler.update();
    profiler.mark('input');

    const dtMs = lastFrameMs === 0 ? 0 : nowMs - lastFrameMs;
    lastFrameMs = nowMs;
    localWeapon.update(nowMs, dtMs);
    views.sync(resolveSheepVisual, dtMs);
    fx.syncEliteBolts(view);
    profiler.mark('sync');
    if (predictionReady) {
      predictor.advance(dtMs, predictCommand);
      predictor.renderPosition(renderPos);
      view.setLocalPrediction(
        renderPos.x,
        renderPos.y,
        renderPos.z,
        sampler.state.yaw,
        sampler.state.pitch,
      );
    }
    smoother.decay(dtMs);

    const local = view.getLocalPlayer();
    if (predictionReady) {
      renderPos.x += smoother.x;
      renderPos.y += smoother.y;
      renderPos.z += smoother.z;
      updateCamera(camera, renderPos, sampler.state.yaw, sampler.state.pitch, fx.shakeX, fx.shakeY);
    } else if (local === undefined) {
      updateCamera(
        camera,
        FALLBACK_CAMERA,
        sampler.state.yaw,
        sampler.state.pitch,
        fx.shakeX,
        fx.shakeY,
      );
    } else {
      updateCamera(camera, local.pos, local.yaw, local.pitch, fx.shakeX, fx.shakeY);
    }
    profiler.mark('predict');
    const timers = localTimers.advance(dtMs);
    const reloadMs = WEAPONS[WEAPON_SLOT_ORDER[weaponSlot] ?? 'pistol'].reloadMs;
    combatState.reloadRatio = timers.reloadLeftMs <= 0 ? 0 : 1 - timers.reloadLeftMs / reloadMs;
    combatState.rageLeftMs = timers.rageLeftMs;
    applyAmmo(nowMs);
    combatState.spreadDeg = localWeapon.spreadDeg;
    combatState.bossHpRatio = views.bossHpRatio;
    viewModel.setMoveAmount(
      Math.min(1, Math.sqrt(sampler.state.moveX ** 2 + sampler.state.moveY ** 2)),
    );
    viewModel.setReloadRatio(combatState.reloadRatio);
    viewModel.setSpreadDeg(combatState.spreadDeg);
    viewModel.setDowned(combatState.downed);
    viewModel.update(dtMs);
    if (meshFlashLeftMs > 0) meshFlashLeftMs = Math.max(0, meshFlashLeftMs - dtMs);
    materials.setHitFlash(hitFlashAmount(meshFlashLeftMs));
    fx.setBlood(1 - combatState.hpRatio);
    fx.setBerserk(combatState.rageLeftMs > 0);
    fx.setChargeWarnings(views.chargeWarnings);
    fx.update(dtMs);
    profiler.mark('fx');
    camera.getWorldDirection(listenerForward);
    listenerPose.x = camera.position.x;
    listenerPose.y = camera.position.y;
    listenerPose.z = camera.position.z;
    listenerPose.forwardX = listenerForward.x;
    listenerPose.forwardY = listenerForward.y;
    listenerPose.forwardZ = listenerForward.z;
    audioWorld.localPlayerId = view.localPlayerId;
    audioLayer.update(dtMs, listenerPose, audioWorld);
    profiler.mark('audio');
    combatHud.set(combatState);
    if (combatState.downed) {
      predictCommand.moveX = 0;
      predictCommand.moveY = 0;
    }
    renderer.draw(scene, camera, nowMs);
    profiler.mark('draw');
    renderer.drawOverlay(viewModel.scene, viewModel.camera);
    profiler.mark('overlay');
    const stats = renderer.getStats();
    const renderStats = renderer.getRenderStats();
    const viewStats = view.getStats();
    const localPos = local === undefined ? undefined : local.pos;
    hudUpdateScratch.local = local;
    hudUpdateScratch.stats = viewStats;
    hudUpdateScratch.fps = stats.fps;
    hudUpdateScratch.liveEntities = views.liveCount;
    hudUpdateScratch.localPos = localPos;
    hud.update(hudUpdateScratch);
    debugUpdateScratch.fps = stats.fps;
    debugUpdateScratch.p95IntervalMs = stats.p95IntervalMs;
    debugUpdateScratch.p95WorkMs = stats.p95WorkMs;
    debugUpdateScratch.tick = view.getAppliedTick();
    debugUpdateScratch.snapshotsPerSec = viewStats.snapshotsPerSec;
    debugUpdateScratch.inboundBytesPerSec = viewStats.inboundBytesPerSec;
    debugUpdateScratch.rttMs = viewStats.rttMs;
    debugUpdateScratch.serverRateX10 = connection.lastServerSnapshotRateX10;
    debugUpdateScratch.liveEntities = views.liveCount;
    debugUpdateScratch.pooledEntities = views.meshCount;
    debugUpdateScratch.localPos = localPos;
    debugUpdateScratch.status = connection.status;
    debugUpdateScratch.roomCode = connection.roomCode;
    debugUpdateScratch.predictionErrorM = reconciler.lastErrorM;
    debugUpdateScratch.predictionMaxErrorM = reconciler.maxErrorM;
    debugUpdateScratch.hardCorrects = reconciler.hardCorrectCount;
    debugUpdateScratch.pendingCommands = commands.size;
    debugUpdateScratch.drawCalls = renderStats.drawCalls;
    debugUpdateScratch.triangles = renderStats.triangles;
    debugUpdateScratch.particles = particles.activeCount;
    debugUpdateScratch.materialCount = materials.count;
    debugUpdateScratch.versionLine = formatVersionLine();
    debugUpdateScratch.pendingShots = ammoPendingShots;
    debugUpdateScratch.rejectedShots = ammoLedger.rejectedTotal;
    debugUpdateScratch.ammoDivergence = ammoDivergence;
    debugUpdateScratch.resyncCount = ammoResyncCount;
    // 分位数只在面板可见时才算（8 段插入排序不便宜），E2E 工具直接读 profiler.summary()。
    debugUpdateScratch.stages = debug.visible ? profiler.summary() : '';
    debug.update(debugUpdateScratch);
    profiler.mark('hud');
    profiler.end();
  }

  if (debugEnabled) {
    // 自动化与手工排查的观测入口（仅在 ?debug=1 时挂载）。
    (window as unknown as { __ac?: Record<string, unknown> }).__ac = {
      sampler,
      view,
      predictor,
      profiler,
      views,
      renderer,
      camera,
      scene,
      connection,
      // 弹药的三个口径（显示值 / 权威值 / 未 ack 开火数）：排查"显示与实际不符"用。
      ammo: () => ({
        name: combatState.weaponName,
        displayed: combatState.mag,
        authority: authorityMag,
        reserve: authorityReserve,
        pending: ammoPendingShots,
        divergence: ammoDivergence,
        rejected: ammoLedger.rejectedTotal,
        localShots: localShotCount,
        downed: combatState.downed,
      }),
    };
  }
  window.requestAnimationFrame(frame);

  window.addEventListener(
    'pagehide',
    () => {
      listeners.abort();
      settingsPanel.dispose();
      audioLayer.dispose();
      pointer.dispose();
      connection.dispose();
      views.dispose();
      hud.dispose();
      debug.dispose();
      lobby.dispose();
      intermission.dispose();
      results.dispose();
      chat.dispose();
      viewModel.dispose();
      fx.dispose();
      particles.dispose();
      arenaArt.dispose();
      materials.dispose();
      renderer.dispose();
    },
    { signal },
  );
}

boot();
