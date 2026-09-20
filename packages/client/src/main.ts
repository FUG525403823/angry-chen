import { formatVersionLine } from './index.ts';

import { ARENA, CONFIG, type Command, createCommand } from '@ac/shared';
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
import { createHud } from './ui/hud.ts';

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
      predictCommand.buttons = command.buttons;
      predictCommand.switchTo = command.switchTo;
      if (!pointer.locked || !predictionReady) return;
      commands.push(predictCommand);
      connection.sendCommand(predictCommand);
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
    },
    onEvents: (events) => {
      hud.pushEvents(events.map((event) => event.type));
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
