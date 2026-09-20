import { createServer } from 'node:http';

import { describe, expect, it } from 'vitest';

import { PROTOCOL_VERSION } from '@ac/shared';

import { createHttpHandler } from './http.ts';
import { createHarness } from './testing/harness.ts';

async function startHttp(
  harness: ReturnType<typeof createHarness>,
): Promise<{ port: number; close(): Promise<void> }> {
  const server = createServer(createHttpHandler(harness.game, Date.now()));
  await new Promise<void>((resolveListen) => {
    server.listen(0, '127.0.0.1', () => resolveListen());
  });
  const address = server.address();
  const port = address !== null && typeof address !== 'string' ? address.port : 0;
  return {
    port,
    close(): Promise<void> {
      return new Promise<void>((resolveClose) => {
        server.close(() => resolveClose());
      });
    },
  };
}

describe('运维端点', () => {
  it('/health 返回 200 与房间/连接状态', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const alice = harness.connect();
    alice.join('alice', '0000');

    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/health');
    expect(response.status).toBe(200);
    const body = (await response.json()) as Record<string, unknown>;
    expect(body['status']).toBe('ok');
    expect(body['protocolVersion']).toBe(PROTOCOL_VERSION);
    expect(body['rooms']).toBe(1);
    expect(body['connections']).toBe(1);
    expect(body['players']).toBe(1);

    const query = await fetch('http://127.0.0.1:' + String(http.port) + '/health?verbose=1');
    expect(query.status).toBe(200);

    await http.close();
    await harness.close();
  });

  it('/metrics 输出 Prometheus 文本且反映实时计数', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const alice = harness.connect();
    alice.join('alice', '0000');
    alice.client.send(new Uint8Array([0x7f]));
    harness.tick(5);

    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/metrics');
    expect(response.status).toBe(200);
    expect(response.headers.get('content-type')).toContain('text/plain');
    const text = await response.text();
    expect(text).toContain('ac_tick_jitter_ms_p95');
    expect(text).toContain('ac_snapshot_bytes_avg');
    expect(text).toContain('ac_malformed_frames_total 1');
    expect(text).toContain('ac_rooms 1');
    expect(text).toContain('ac_connections 1');
    expect(text).toContain('ac_players 1');
    expect(text).toContain('ac_joins_total 1');

    await http.close();
    await harness.close();
  });

  it('未知路径返回 404', async () => {
    const harness = createHarness();
    const http = await startHttp(harness);
    const response = await fetch('http://127.0.0.1:' + String(http.port) + '/nope');
    expect(response.status).toBe(404);
    const body = (await response.json()) as Record<string, unknown>;
    expect(body['error']).toBe('not-found');
    await http.close();
    await harness.close();
  });
});
