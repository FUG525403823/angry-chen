import { PROTOCOL_VERSION, SERVER_TICK_MS } from '@ac/shared';

export interface HealthPayload {
  ok: boolean;
  protocolVersion: number;
  tickMs: number;
  uptimeMs: number;
}

export function buildHealthPayload(startedAtMs: number, nowMs: number): HealthPayload {
  return {
    ok: true,
    protocolVersion: PROTOCOL_VERSION,
    tickMs: SERVER_TICK_MS,
    uptimeMs: Math.max(0, Math.round(nowMs - startedAtMs)),
  };
}

export interface WelcomeFrame {
  type: 'welcome';
  protocolVersion: number;
  tickMs: number;
  serverTimeMs: number;
}

export function buildWelcomeFrame(nowMs: number): WelcomeFrame {
  return {
    type: 'welcome',
    protocolVersion: PROTOCOL_VERSION,
    tickMs: SERVER_TICK_MS,
    serverTimeMs: Math.round(nowMs),
  };
}
