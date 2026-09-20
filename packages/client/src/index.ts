import { PROTOCOL_VERSION, SERVER_TICK_MS } from '@ac/shared';

export function formatVersionLine(): string {
  return 'protocolVersion ' + PROTOCOL_VERSION + ' / tickMs ' + SERVER_TICK_MS;
}
