import { HIT_FLAG, SHEEP_STATE, type SimEvent } from '@ac/shared';

import { createMixer, type ListenerPose, type Mixer, type Vec3Like } from './mixer.ts';
import { bleatNameForForm, playSfx, sfxNameForWeaponSlot, type SfxName } from './sfx.ts';
import {
  createSubtitleLog,
  createSubtitleOverlay,
  subtitleTextFor,
  type SubtitleCue,
  type SubtitleLog,
  type SubtitleOverlay,
} from './subtitles.ts';
import { createAudioContext } from './synth.ts';

export const SHEEP_ENTITY_KIND = 1;
export const LOCAL_TEAM = 0;
export const BLEAT_INTERVAL_MIN_MS = 3600;
export const BLEAT_INTERVAL_SPAN_MS = 3400;
export const BLEAT_NEAR_DISTANCE_M = 4;
export const BLEAT_FADE_DISTANCE_M = 26;
export const BLEAT_MIN_GAIN = 0.45;
export const DYING_BLEAT_GAIN = 0.4;
export const DOWNED_HIT_GAIN = 0.5;
export const ACTOR_STALE_MS = 2000;
export const CHARGE_WARNING_COOLDOWN_MS = 1600;

export const AUDIO_KING_STATES: readonly number[] = Object.freeze([
  SHEEP_STATE.kingPhase1,
  SHEEP_STATE.kingPhase2,
  SHEEP_STATE.kingPhase3,
  SHEEP_STATE.kingSummoning,
]);

export interface ActorEntity {
  readonly id: number;
  readonly kind: number;
  readonly pos: Vec3Like;
  readonly state: number;
  readonly hpRatio: number;
}

export interface ActorSource {
  forEachVisible(cb: (entity: ActorEntity) => void): void;
}

export interface AudioWorldView {
  readonly source: ActorSource;
  readonly localPlayerId: number;
  readonly localTeam?: number | undefined;
}

export interface AudioLayerOptions {
  readonly host?: HTMLElement | undefined;
  readonly onSubtitle?: ((text: string) => void) | undefined;
}

export interface AudioLayer {
  readonly mixer: Mixer;
  readonly attached: boolean;
  readonly subtitles: readonly string[];
  attach(): boolean;
  update(dtMs: number, listener: ListenerPose, world: AudioWorldView): void;
  handleEvents(events: readonly SimEvent[]): void;
  notifyFire(slot: number): void;
  notifyReload(): void;
  notifyUi(): void;
  notifyChargeWarning(position?: Vec3Like): void;
  setVolumes(masterVolume: number, sfxVolume: number): void;
  dispose(): void;
}

interface ActorRecord {
  lastState: number;
  form: number;
  nextBleatMs: number;
  lastSeenMs: number;
  warnedAtMs: number;
}

export function bleatFormFromState(state: number): number {
  if (AUDIO_KING_STATES.indexOf(state) >= 0) return 3;
  if (state === SHEEP_STATE.ranged) return 2;
  if (state === SHEEP_STATE.windup || state === SHEEP_STATE.charge) return 1;
  return 0;
}

export function isChargeWindup(state: number): boolean {
  return state === SHEEP_STATE.windup;
}

export function outcomeCueForWinnerTeam(winnerTeam: number, localTeam = LOCAL_TEAM): SubtitleCue {
  return winnerTeam === localTeam ? 'victory' : 'defeat';
}

export function bleatIntervalMs(id: number, salt: number): number {
  const mixed = (Math.imul(id + 0x9e37, 0x85ebca6b) ^ Math.imul(salt + 1, 0xc2b2ae35)) >>> 0;
  return BLEAT_INTERVAL_MIN_MS + (mixed % BLEAT_INTERVAL_SPAN_MS);
}

export function bleatGainForDistance(distanceM: number): number {
  if (!Number.isFinite(distanceM) || distanceM <= BLEAT_NEAR_DISTANCE_M) return 1;
  if (distanceM >= BLEAT_FADE_DISTANCE_M) return BLEAT_MIN_GAIN;
  const span = BLEAT_FADE_DISTANCE_M - BLEAT_NEAR_DISTANCE_M;
  const ratio = (distanceM - BLEAT_NEAR_DISTANCE_M) / span;
  return 1 - ratio * (1 - BLEAT_MIN_GAIN);
}

export function createAudioLayer(options?: AudioLayerOptions): AudioLayer {
  const mixer = createMixer(undefined);
  const log: SubtitleLog = createSubtitleLog();
  const overlay: SubtitleOverlay | undefined =
    options?.host === undefined ? undefined : createSubtitleOverlay(options.host, log);
  const onSubtitle = options?.onSubtitle;
  const actors = new Map<number, ActorRecord>();
  let elapsedMs = 0;
  let bleatSalt = 0;
  let localPlayerId = 0;
  let localTeam = LOCAL_TEAM;

  function cue(name: SubtitleCue, wave = 0): void {
    const text = subtitleTextFor(name, wave);
    log.push(text);
    overlay?.render();
    if (onSubtitle !== undefined) onSubtitle(text);
  }

  function play(name: SfxName, position?: Vec3Like, gain?: number): boolean {
    return playSfx(mixer, name, { position, gain });
  }

  function handleSheep(entity: ActorEntity, record: ActorRecord): void {
    const form = bleatFormFromState(entity.state);
    record.form = form;
    if (isChargeWindup(entity.state) && record.lastState !== entity.state) {
      if (elapsedMs - record.warnedAtMs >= CHARGE_WARNING_COOLDOWN_MS) {
        record.warnedAtMs = elapsedMs;
        cue('chargeWarn');
      }
    }
    record.lastState = entity.state;
    if (entity.state === SHEEP_STATE.windup || entity.state === SHEEP_STATE.charge) return;
    if (entity.state === SHEEP_STATE.dead) return;
    if (elapsedMs < record.nextBleatMs) return;
    bleatSalt += 1;
    record.nextBleatMs = elapsedMs + bleatIntervalMs(entity.id, bleatSalt);
    play(bleatNameForForm(form), entity.pos, bleatGainForDistance(mixer.distanceTo(entity.pos)));
  }

  return {
    mixer,
    get attached(): boolean {
      return mixer.attached;
    },
    get subtitles(): readonly string[] {
      return log.lines;
    },
    attach(): boolean {
      if (mixer.attached) return true;
      const context = createAudioContext();
      if (context === undefined) return false;
      mixer.attachContext(context);
      return true;
    },
    update(dtMs: number, listener: ListenerPose, world: AudioWorldView): void {
      if (Number.isFinite(dtMs) && dtMs > 0) elapsedMs += dtMs;
      mixer.updateListener(listener);
      mixer.update(dtMs);
      if (overlay === undefined) log.update(dtMs);
      else overlay.update(dtMs);
      localPlayerId = world.localPlayerId;
      localTeam = world.localTeam ?? LOCAL_TEAM;
      world.source.forEachVisible((entity) => {
        if (entity.kind !== SHEEP_ENTITY_KIND) return;
        let record = actors.get(entity.id);
        if (record === undefined) {
          record = {
            lastState: entity.state,
            form: bleatFormFromState(entity.state),
            nextBleatMs: elapsedMs + bleatIntervalMs(entity.id, 0),
            lastSeenMs: elapsedMs,
            warnedAtMs: elapsedMs - CHARGE_WARNING_COOLDOWN_MS,
          };
          actors.set(entity.id, record);
          return;
        }
        record.lastSeenMs = elapsedMs;
        handleSheep(entity, record);
      });
      if (actors.size === 0) return;
      for (const [id, record] of actors) {
        if (elapsedMs - record.lastSeenMs < ACTOR_STALE_MS) continue;
        actors.delete(id);
      }
    },
    handleEvents(events: readonly SimEvent[]): void {
      for (let i = 0; i < events.length; i += 1) {
        const event = events[i];
        if (event === undefined) continue;
        if (event.type === 'playerHit') {
          const headshot = (event.flags & HIT_FLAG.headshot) !== 0;
          if (event.subjectId === localPlayerId && event.targetId !== localPlayerId) {
            play(headshot ? 'headshot' : 'hit', { x: event.x, y: event.y, z: event.z });
          } else if (event.targetId === localPlayerId) {
            play('hit', { x: event.x, y: event.y, z: event.z });
          }
          continue;
        }
        if (event.type === 'sheepKilled') {
          const record = actors.get(event.targetId);
          play(
            bleatNameForForm(record?.form ?? 0),
            { x: event.x, y: event.y, z: event.z },
            DYING_BLEAT_GAIN,
          );
          continue;
        }
        if (event.type === 'waveStart') {
          cue('waveStart', event.value);
          continue;
        }
        if (event.type === 'playerDowned') {
          if (event.subjectId !== localPlayerId) continue;
          cue('downed');
          play('hit', undefined, DOWNED_HIT_GAIN);
          continue;
        }
        if (event.type === 'rageActivated') {
          if (event.subjectId !== localPlayerId) continue;
          play('berserk');
          continue;
        }
        if (event.type === 'matchEnded') {
          const name = outcomeCueForWinnerTeam(event.subjectId, localTeam);
          play(name === 'victory' ? 'victory' : 'defeat');
          cue(name);
        }
      }
    },
    notifyFire(slot: number): void {
      play(sfxNameForWeaponSlot(slot));
    },
    notifyReload(): void {
      play('reload');
    },
    notifyUi(): void {
      play('uiClick');
    },
    notifyChargeWarning(position?: Vec3Like): void {
      play('chargeWarn', position);
    },
    setVolumes(masterVolume: number, sfxVolume: number): void {
      mixer.setVolume('master', masterVolume);
      mixer.setVolume('sfx', sfxVolume);
    },
    dispose(): void {
      actors.clear();
      log.clear();
      overlay?.dispose();
      mixer.dispose();
    },
  };
}
