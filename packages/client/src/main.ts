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
} from '@ac/shared';
import { createAudioLayer, outcomeCueForWinnerTeam } from './audio/layer.ts';
import type { ListenerPose } from './audio/mixer.ts';
import { createLocalWeapon } from './combat/localWeapon.ts';
import { createPointerInput } from './input/pointerLock.ts';
import { createCommandBuffer } from './prediction/commandBuffer.ts';
import { createPredictor } from './prediction/predictor.ts';
import { createLocalAuthority, createReconciler } from './prediction/reconciler.ts';
import { createErrorSmoother } from './render/errorSmoother.ts';
import { createInputSampler } from './input/sampler.ts';
import { createGameConnection, readStoredCredentials } from './net/connection.ts';
import { createSnapshotView } from './net/state.ts';
import { createSheepVisualTracker } from './net/sheepVisual.ts';
import { type ArenaArtParams, createArenaArt } from './render/arenaArt.ts';
import { createEffects } from './render/effects.ts';
import { createEntityViews } from './render/entityViews.ts';
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
import { applyFov, createScene, updateCamera } from './render/scene.ts';
import { createViewModelModel } from './render/viewmodelModel.ts';
import { createSettingsStore, type SettingsStorage } from './settings/store.ts';
import { createChat } from './ui/chat.ts';
import { createDebugPanel } from './ui/debugPanel.ts';
import { createCombatHud, createHud } from './ui/hud.ts';
import { createIntermission } from './ui/intermission.ts';
import { createLobby, errorMessageFor, filterRoomCodeInput } from './ui/lobby.ts';
import { createResults, type ResultsSummary } from './ui/results.ts';
import { applyAccessibilityClasses, createSettingsPanel } from './ui/settings.ts';

export const DEFAULT_SERVER_URL = 'ws://127.0.0.1:8787';
export const DEFAULT_PLAYER_NAME = '陈sir';
export const PHASE_NAMES: readonly string[] = ['大厅', '加载中', '战斗中', '波间备战', '结算'];
export const SHEEP_LABELS: readonly string[] = ['咩咩兵', '冲撞羊', '问界羊', '羊王'];
export const FALLBACK_CAMERA = { x: 0, y: 3, z: 18 };
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
  const serverUrl = params.get('server') ?? DEFAULT_SERVER_URL;
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
  const debug = createDebugPanel(debugRoot, params.get('debug') === '1');

  const sampler = createInputSampler({
    now: () => performance.now(),
    emit: (command) => {
      predictCommand.seq = command.seq;
      predictCommand.tick = command.tick;
      predictCommand.moveX = command.moveX;
      predictCommand.moveY = command.moveY;
      predictCommand.yaw = command.yaw;
      predictCommand.pitch = command.pitch;
      predictCommand.buttons = command.buttons | (fireHeld && pointer.locked ? BUTTON.fire : 0);
      predictCommand.switchTo = command.switchTo;
      if (!pointer.locked || !predictionReady) return;
      commands.push(predictCommand);
      connection.sendCommand(predictCommand);
      if ((predictCommand.buttons & BUTTON.fire) !== 0) {
        camera.getWorldDirection(forwardScratch);
        if (localWeapon.onFire(performance.now())) {
          viewModel.triggerFire();
          audioLayer.notifyFire(weaponSlot);
          fireOriginScratch.copy(camera.position).addScaledVector(forwardScratch, 0.45);
          fx.spawnMuzzleFlash(
            fireOriginScratch.x,
            fireOriginScratch.y,
            fireOriginScratch.z,
            forwardScratch.x,
            forwardScratch.y,
            forwardScratch.z,
          );
          fx.triggerShake(0.03);
        }
        tracerEnd.x = camera.position.x + forwardScratch.x * 30;
        tracerEnd.y = camera.position.y + forwardScratch.y * 30;
        tracerEnd.z = camera.position.z + forwardScratch.z * 30;
        fx.spawnTracer(
          camera.position.x,
          camera.position.y,
          camera.position.z,
          tracerEnd.x,
          tracerEnd.y,
          tracerEnd.z,
          true,
        );
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
      predictionReady = false;
    },
    onSnapshotApplied: () => {
      view.getLocalAuthority(authority);
      if (!authority.found) return;
      predictionReady = true;
      reconciler.reconcile(authority, commands, predictor, smoother);
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
        combatState.mag = player.mag;
        combatState.magSize = MAG_SIZES[weaponSlot] ?? WEAPONS.pistol.mag;
        combatState.reserve = player.reserve;
        combatState.rage = player.rage;
        combatState.rageLeftMs = player.rageLeft100Ms * 100;
        combatState.downed = player.downed;
        combatState.weaponName = WEAPON_NAMES[weaponSlot] ?? '手枪';
        const wasReloading = reloadLeft10Ms > 0;
        reloadLeft10Ms = player.reloadLeft10Ms;
        if (!wasReloading && reloadLeft10Ms > 0) audioLayer.notifyReload();
        localWeapon.setSlot(weaponSlot, performance.now());
        localWeapon.syncMag(weaponSlot, player.mag, player.reserve);
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
      hud.pushEvents(events.map((event) => event.type));
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
        fx.spawnTracer(
          camera.position.x,
          camera.position.y,
          camera.position.z,
          event.x,
          event.y,
          event.z,
          false,
        );
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
    sampler.update();

    const dtMs = lastFrameMs === 0 ? 0 : nowMs - lastFrameMs;
    lastFrameMs = nowMs;
    localWeapon.update(nowMs, dtMs);
    views.sync((id, state) => sheepVisual.resolve(id, state), dtMs);
    fx.syncEliteBolts(view);
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
    const reloadMs = WEAPONS[WEAPON_SLOT_ORDER[weaponSlot] ?? 'pistol'].reloadMs;
    combatState.reloadRatio = reloadLeft10Ms <= 0 ? 0 : 1 - (reloadLeft10Ms * 10) / reloadMs;
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
    camera.getWorldDirection(listenerForward);
    listenerPose.x = camera.position.x;
    listenerPose.y = camera.position.y;
    listenerPose.z = camera.position.z;
    listenerPose.forwardX = listenerForward.x;
    listenerPose.forwardY = listenerForward.y;
    listenerPose.forwardZ = listenerForward.z;
    audioWorld.localPlayerId = view.localPlayerId;
    audioLayer.update(dtMs, listenerPose, audioWorld);
    combatHud.set(combatState);
    if (combatState.downed) {
      predictCommand.moveX = 0;
      predictCommand.moveY = 0;
    }
    renderer.draw(scene, camera, nowMs);
    renderer.drawOverlay(viewModel.scene, viewModel.camera);
    const stats = renderer.getStats();
    const renderStats = renderer.getRenderStats();
    const localPos = local === undefined ? undefined : local.pos;
    hud.update({
      local,
      stats: view.getStats(),
      fps: stats.fps,
      liveEntities: views.liveCount,
      localPos,
    });
    debug.update({
      fps: stats.fps,
      p95IntervalMs: stats.p95IntervalMs,
      p95WorkMs: stats.p95WorkMs,
      tick: view.getAppliedTick(),
      snapshotsPerSec: view.getStats().snapshotsPerSec,
      inboundBytesPerSec: view.getStats().inboundBytesPerSec,
      rttMs: view.getStats().rttMs,
      serverRateX10: connection.lastServerSnapshotRateX10,
      liveEntities: views.liveCount,
      pooledEntities: views.meshCount,
      localPos,
      status: connection.status,
      roomCode: connection.roomCode,
      predictionErrorM: reconciler.lastErrorM,
      predictionMaxErrorM: reconciler.maxErrorM,
      hardCorrects: reconciler.hardCorrectCount,
      pendingCommands: commands.size,
      drawCalls: renderStats.drawCalls,
      triangles: renderStats.triangles,
      particles: particles.activeCount,
      materialCount: materials.count,
      versionLine: formatVersionLine(),
    });
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
