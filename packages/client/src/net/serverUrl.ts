/**
 * ADR-007：客户端服务器寻址。
 *
 * 优先级（冻结）：`?server=` > Vite dev 回退 127.0.0.1:8787 > 页面同源 `ws(s)://<host>/ws` > 兜底。
 * 生产形态是「页面由游戏服务器自己提供」，所以同源推导让「一个地址就能玩」成立；
 * `/ws` 同时匹配 `deploy/Caddyfile` 的 `handle /ws`，裸跑与反代共用同一条规则。
 */
export const DEFAULT_SERVER_URL = 'ws://127.0.0.1:8787';
export const SAME_ORIGIN_WS_PATH = '/ws';

export interface ServerUrlInput {
  /** `window.location.search`（可带前导 `?`）。 */
  readonly search: string;
  /** `window.location.protocol`，如 `http:` / `https:` / `file:`。 */
  readonly protocol: string;
  /** `window.location.host`，如 `192.168.1.5:8787`；`file://` 下为空串。 */
  readonly host: string;
  /** `import.meta.env.DEV`：是否由 Vite dev server 提供页面。 */
  readonly isDev: boolean;
  /** 兜底地址，通常是 `DEFAULT_SERVER_URL`。 */
  readonly fallback: string;
}

/** 归一化 `?server=`：接受 ws/wss/http/https，http→ws、https→wss，去掉结尾斜杠；非法返回 `null`。 */
export function normalizeServerUrl(raw: string): string | null {
  const trimmed = raw.trim();
  const marker = trimmed.indexOf('://');
  if (marker <= 0) return null;
  const scheme = trimmed.slice(0, marker).toLowerCase();
  const rest = trimmed.slice(marker + 3).replace(/\/+$/, '');
  if (rest === '') return null;
  if (scheme === 'wss' || scheme === 'https') return 'wss://' + rest;
  if (scheme === 'ws' || scheme === 'http') return 'ws://' + rest;
  return null;
}

/** 页面同源地址；没有 host（`file://`）时返回 `null`。 */
export function serverUrlFromOrigin(protocol: string, host: string): string | null {
  const trimmed = host.trim();
  if (trimmed === '') return null;
  const scheme = protocol === 'https:' || protocol === 'wss:' ? 'wss' : 'ws';
  return scheme + '://' + trimmed + SAME_ORIGIN_WS_PATH;
}

export function resolveServerUrl(input: ServerUrlInput): string {
  const fromParam = normalizeServerUrl(new URLSearchParams(input.search).get('server') ?? '');
  if (fromParam !== null) return fromParam;
  if (input.isDev) return input.fallback;
  return serverUrlFromOrigin(input.protocol, input.host) ?? input.fallback;
}
