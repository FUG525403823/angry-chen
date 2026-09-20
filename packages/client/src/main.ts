import { Vector3 } from 'three';

import { formatVersionLine } from './index.ts';

import {
  ARENA,
  BUTTON,
  CONFIG,
  type Command,
  HIT_FLAG,
  type SimEvent,
  WEAPONS,
  WEAPON_SLOT_ORDER,
  createCommand,
} from '@ac/shared';
import { createCombatFx } from './combat/fx.ts';
import { createViewModel } from './combat/viewmodel.ts';
import { createPointerInput } from './input/pointerLock.ts';
import { createCommandBuffer } from './prediction/commandBuffer.ts';
import { createPredictor } from './prediction/predictor.ts';
import { createLocalAuthority, createReconciler } from './prediction/reconciler.ts';
import { createErrorSmoother } from './render/errorSmoother.ts';
import { createInputSampler } from './input/sampler.ts';
import { ERROR_MESSAGES, createGameConnection } from './net/connection.ts';
import { createSnapshotView } from './net/state.ts';
import { createArena, disposeArena, type ArenaParams } from './render/arena.ts';
import { COLORBLIND_PALETTE, DEFAULT_PALETTE, createEntityViews } from './render/entityViews.ts';
import { createRenderer } from './render/renderer.ts';
import { applyFov, createScene, updateCamera } from './render/scene.ts';
import { createSettingsStore, type SettingsStorage } from './settings/store.ts';
import { createDebugPanel } from './ui/debugPanel.ts';
import { createCombatHud, createHud } from './ui/hud.ts';

export const DEFAULT_SERVER_URL = 'ws://127.0.0.1:8787';
export const DEFAULT_PLAYER_NAME = '陈sir';
export const PHASE_NAMES: readonly string[] = ['大厅', '间歇', '战斗中', '结算'];
export const FALLBACK_CAMERA = { x: 0, y: 3, z: 18 };

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

  const params = new URLSearchParams(window.location.search);
  const roomCode = (params.get('room') ?? '').trim().toUpperCase();
  const playerName = params.get('name') ?? DEFAULT_PLAYER_NAME;
  const serverUrl = params.get('server') ?? DEFAULT_SERVER_URL;

  const settings = createSettingsStore(safeLocalStorage());
  const { scene, camera } = createScene(settings.get().fov);
  const arenaParams: ArenaParams = {
    halfSize: ARENA.halfSize,
    fenceHeight: ARENA.fence.height,
    fenceThickness: ARENA.fence.thickness,
    barn: ARENA.barn,
  };
  const markers = [
    ...ARENA.playerSpawnPoints.map((point) => ({ x: point.x, z: point.z, color: 0x9fd2ff })),
    ...ARENA.enemySpawnPoints.map((point) => ({ x: point.x, z: point.z, color: 0xffb4a2 })),
  ];
  const arena = createArena(arenaParams, markers);
  scene.add(arena);

  const renderer = createRenderer();
  app.replaceChildren(renderer.domElement);

  const view = createSnapshotView();
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
  const viewModel = createViewModel(camera);
  const fx = createCombatFx(scene, app);
  const combatHud = createCombatHud(hudRoot);
  const MAG_SIZES = [WEAPONS.pistol.mag, WEAPONS.rifle.mag, WEAPONS.shotgun.mag];
  const WEAPON_NAMES = ['手枪', '步枪', '霰弹枪'];
  const combatState = {
    mag: WEAPONS.pistol.mag,
    magSize: WEAPONS.pistol.mag,
    reserve: 120,
    reloadRatio: 0,
    rage: 0,
    rageLeftMs: 0,
    downed: false,
    reviveRatio: 0,
    weaponName: '手枪',
  };
  let reloadLeft10Ms = 0;
  let weaponSlot: 0 | 1 | 2 = 0;
  let fireHeld = false;
  let fireSeq = 0;
  const tracerEnd = { x: 0, y: 0, z: 0 };
  const forwardScratch = new Vector3();
  let lastFrameMs = 0;
  const views = createEntityViews(scene, view, () => view.localPlayerId);
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
        fireSeq += 1;
        viewModel.triggerFire(fireSeq, 4);
        camera.getWorldDirection(forwardScratch);
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

  const connection = createGameConnection({
    url: serverUrl,
    view,
    name: playerName,
    roomCode,
    onStatus: (status) => {
      hud.setStatus(status);
      if (status === 'error') hud.showBanner('连接错误，正在重连…');
      else if (status === 'closed') hud.showBanner('连接断开，正在重连…');
    },
    onWelcome: (welcome) => {
      hud.setRoomCode(welcome.roomCode);
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
      const mine = view.localPlayerId;
      for (let i = 0; i < state.players.length; i += 1) {
        const player = state.players[i];
        if (player === undefined || player.pid !== mine) continue;
        weaponSlot = player.weapon === 1 ? 1 : player.weapon === 2 ? 2 : 0;
        combatState.mag = player.mag;
        combatState.magSize = MAG_SIZES[weaponSlot] ?? WEAPONS.pistol.mag;
        combatState.reserve = player.reserve;
        combatState.rage = player.rage;
        combatState.rageLeftMs = player.rageLeft100Ms * 100;
        combatState.downed = player.downed;
        combatState.reviveRatio = player.reviveRatio255 / 255;
        combatState.weaponName = WEAPON_NAMES[weaponSlot] ?? '手枪';
        reloadLeft10Ms = player.reloadLeft10Ms;
        viewModel.setWeapon(weaponSlot);
      }
    },
    onEvents: (events) => {
      hud.pushEvents(events.map((event) => event.type));
      for (let i = 0; i < events.length; i += 1) handleCombatEvent(events[i]);
    },
    onServerError: (code, message) => {
      hud.showBanner('错误 ' + String(code) + '：' + (ERROR_MESSAGES[code] ?? message));
    },
  });

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
    if (event.type === 'playerHit' && event.subjectId === mine && event.targetId !== mine) {
      const headshot = (event.flags & HIT_FLAG.headshot) !== 0;
      fx.showHitMarker(headshot);
      fx.showDamageNumber(event.x, event.y, event.z, event.value, headshot);
      fx.spawnImpact(event.x, event.y, event.z);
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
    if (event.type === 'playerHit' && event.targetId === mine) {
      combatHud.pushKill('你受到 ' + String(Math.round(event.value)) + ' 点伤害');
      return;
    }
    if (event.type === 'sheepKilled' && event.subjectId === mine) {
      combatHud.pushKill('击杀咩咩兵');
      return;
    }
    if (event.type === 'playerDowned' && event.subjectId === mine) {
      combatHud.pushKill('你已倒地，等待救援');
      return;
    }
    if (event.type === 'reviveDone' && event.targetId === mine) {
      combatHud.pushKill('已被救起');
      return;
    }
    if (event.type === 'rageActivated' && event.subjectId === mine) {
      combatHud.pushKill('狂暴已激活');
    }
  }

  function applySettings(): void {
    const current = settings.get();
    applyFov(camera, current.fov);
    sampler.setSensitivity(current.sensitivity);
    views.setPalette(current.colorblindSafe ? COLORBLIND_PALETTE : DEFAULT_PALETTE);
    document.body.classList.toggle('reduce-motion', current.reduceMotion);
  }
  applySettings();
  settings.subscribe(applySettings);

  function resize(): void {
    const width = window.innerWidth;
    const height = window.innerHeight;
    renderer.setSize(width, height);
    camera.aspect = height === 0 ? 1 : width / height;
    camera.updateProjectionMatrix();
  }
  resize();
  window.addEventListener('resize', resize);

  renderer.domElement.addEventListener('mousedown', (event) => {
    if (event.button === 0) fireHeld = true;
  });
  window.addEventListener('mouseup', (event) => {
    if (event.button === 0) fireHeld = false;
  });

  renderer.domElement.addEventListener('click', () => {
    if (!pointer.locked) pointer.request();
  });

  window.addEventListener('keydown', (event) => {
    if (event.code === 'F3') {
      event.preventDefault();
      debug.toggle();
    }
  });

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
    views.sync();

    const dtMs = lastFrameMs === 0 ? 0 : nowMs - lastFrameMs;
    lastFrameMs = nowMs;
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
      updateCamera(camera, renderPos, sampler.state.yaw, sampler.state.pitch);
    } else if (local === undefined) {
      updateCamera(camera, FALLBACK_CAMERA, sampler.state.yaw, sampler.state.pitch);
    } else {
      updateCamera(camera, local.pos, local.yaw, local.pitch);
    }
    viewModel.update(dtMs);
    fx.update(dtMs);
    const reloadMs = WEAPONS[WEAPON_SLOT_ORDER[weaponSlot] ?? 'pistol'].reloadMs;
    combatState.reloadRatio = reloadLeft10Ms <= 0 ? 0 : 1 - (reloadLeft10Ms * 10) / reloadMs;
    combatHud.set(combatState);
    if (combatState.downed) {
      predictCommand.moveX = 0;
      predictCommand.moveY = 0;
    }
    renderer.draw(scene, camera, nowMs);
    const stats = renderer.getStats();
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
      roomCode,
      predictionErrorM: reconciler.lastErrorM,
      predictionMaxErrorM: reconciler.maxErrorM,
      hardCorrects: reconciler.hardCorrectCount,
      pendingCommands: commands.size,
      versionLine: formatVersionLine(),
    });
  }
  window.requestAnimationFrame(frame);

  window.addEventListener('pagehide', () => {
    window.removeEventListener('resize', resize);
    pointer.dispose();
    connection.dispose();
    views.dispose();
    hud.dispose();
    debug.dispose();
    disposeArena(arena);
    renderer.dispose();
  });
}

boot();
