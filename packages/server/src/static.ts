import { createReadStream, statSync, type Stats } from 'node:fs';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { extname, isAbsolute, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createGzip } from 'node:zlib';

/**
 * ADR-007：单进程静态托管。
 *
 * 客户端静态产物由 Node 进程自己提供（兑现 ADR-004 §1 的单进程形态），反代降级为可选。
 * 契约（冻结）：只接管 GET/HEAD；路径必须落在 `root` 内；`/assets/*` 一年 immutable，
 * index.html 不缓存；不发 ETag/304、不支持 Range；文本类型在客户端接受 gzip 且体积达阈值时流式压缩。
 */
export const CLIENT_DIST_ENV = 'CLIENT_DIST';
export const IMMUTABLE_PREFIX = '/assets/';
export const IMMUTABLE_MAX_AGE_SECONDS = 31536000;
export const GZIP_MIN_BYTES = 1024;
export const CLIENT_DIST_OFF = 'off';
export const CLIENT_DIST_NONE = 'none';
/** 默认 dist 位置：`packages/server/src/static.ts` → `packages/client/dist/`。 */
export const DEFAULT_CLIENT_DIST_URL = '../../client/dist/';

const CONTENT_TYPES: Readonly<Record<string, string>> = Object.freeze({
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.webmanifest': 'application/manifest+json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.txt': 'text/plain; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.woff2': 'font/woff2',
  '.ico': 'image/x-icon',
  '.png': 'image/png',
});

const COMPRESSIBLE_EXTENSIONS: readonly string[] = Object.freeze([
  '.html',
  '.js',
  '.mjs',
  '.css',
  '.json',
  '.webmanifest',
  '.svg',
  '.txt',
  '.map',
]);

export function contentTypeFor(filePath: string): string {
  return CONTENT_TYPES[extname(filePath).toLowerCase()] ?? 'application/octet-stream';
}

export function isCompressibleType(filePath: string): boolean {
  return COMPRESSIBLE_EXTENSIONS.includes(extname(filePath).toLowerCase());
}

/**
 * 把 URL 路径解析为 `root` 内的绝对文件路径；任何可疑输入返回 `null`（调用方回落 404）。
 * 以 `/` 结尾的路径按目录处理，追加 `index.html`。
 */
export function resolveStaticPath(root: string, urlPath: string): string | null {
  let decoded: string;
  try {
    decoded = decodeURIComponent(urlPath);
  } catch {
    return null;
  }
  if (decoded.includes('\u0000') || decoded.includes('\\')) return null;
  const directoryStyle = decoded.endsWith('/');
  const segments: string[] = [];
  for (const segment of decoded.split('/')) {
    if (segment === '' || segment === '.') continue;
    if (segment === '..') return null;
    segments.push(segment);
  }
  if (directoryStyle) segments.push('index.html');
  const relativePart = segments.join('/');
  if (relativePart === '') return null;
  const absolute = resolve(root, relativePart);
  const inside = relative(root, absolute);
  if (inside === '' || inside.startsWith('..') || isAbsolute(inside)) return null;
  return absolute;
}

export interface ClientDistResolution {
  /** 可用的 dist 目录；`null` = 关闭静态托管。 */
  readonly root: string | null;
  /** 是否由 `CLIENT_DIST` 显式指定（用于区分「没构建」与「配错了」）。 */
  readonly explicit: boolean;
  /** 显式值原文，仅用于日志。 */
  readonly requested: string | null;
}

export function resolveClientDist(
  envValue: string | undefined,
  moduleUrl: string,
): ClientDistResolution {
  const raw = envValue === undefined ? '' : envValue.trim();
  if (raw !== '') {
    if (raw === CLIENT_DIST_OFF || raw === CLIENT_DIST_NONE) {
      return { root: null, explicit: true, requested: raw };
    }
    const absolute = isAbsolute(raw) ? raw : resolve(process.cwd(), raw);
    return { root: isDirectory(absolute) ? absolute : null, explicit: true, requested: raw };
  }
  const fallback = resolve(fileURLToPath(new URL(DEFAULT_CLIENT_DIST_URL, moduleUrl)));
  return { root: isDirectory(fallback) ? fallback : null, explicit: false, requested: null };
}

export interface StaticServerOptions {
  readonly root: string;
  readonly immutablePrefix?: string;
  readonly immutableMaxAgeSeconds?: number;
  readonly gzipMinBytes?: number;
}

export interface StaticServer {
  /** `true` = 本请求已被静态层接管；`false` = 交回调用方（既有 404 JSON）。 */
  handle(req: IncomingMessage, res: ServerResponse): boolean;
}

export function createStaticServer(options: StaticServerOptions): StaticServer {
  const root = resolve(options.root);
  const indexFile = resolve(root, 'index.html');
  const immutablePrefix = options.immutablePrefix ?? IMMUTABLE_PREFIX;
  const immutableMaxAgeSeconds = options.immutableMaxAgeSeconds ?? IMMUTABLE_MAX_AGE_SECONDS;
  const gzipMinBytes = options.gzipMinBytes ?? GZIP_MIN_BYTES;

  return {
    handle(req: IncomingMessage, res: ServerResponse): boolean {
      const method = req.method ?? 'GET';
      if (method !== 'GET' && method !== 'HEAD') return false;
      const url = req.url ?? '/';
      const queryIndex = url.indexOf('?');
      const urlPath = queryIndex < 0 ? url : url.slice(0, queryIndex);
      const filePath = resolveStaticPath(root, urlPath);
      if (filePath === null) return false;
      const stats = readFileStats(filePath);
      if (stats !== null) {
        // 缓存策略按 URL 前缀判定（filePath 是绝对路径，前缀匹配不到 /assets/）。
        sendFile(req, res, filePath, stats.size, urlPath.startsWith(immutablePrefix), {
          immutableMaxAgeSeconds,
          gzipMinBytes,
        });
        return true;
      }
      if (!looksLikeNavigation(req, urlPath)) return false;
      const indexStats = readFileStats(indexFile);
      if (indexStats === null) return false;
      sendFile(req, res, indexFile, indexStats.size, false, {
        immutableMaxAgeSeconds,
        gzipMinBytes,
      });
      return true;
    },
  };
}

interface SendOptions {
  readonly immutableMaxAgeSeconds: number;
  readonly gzipMinBytes: number;
}

function sendFile(
  req: IncomingMessage,
  res: ServerResponse,
  filePath: string,
  sizeBytes: number,
  immutable: boolean,
  options: SendOptions,
): void {
  const compressible = isCompressibleType(filePath);
  const gzip = compressible && sizeBytes >= options.gzipMinBytes && acceptsGzip(req);
  const headers: Record<string, string> = {
    'content-type': contentTypeFor(filePath),
    'cache-control': immutable
      ? 'public, max-age=' + String(options.immutableMaxAgeSeconds) + ', immutable'
      : 'no-cache',
  };
  if (compressible) headers['vary'] = 'accept-encoding';
  if (gzip) headers['content-encoding'] = 'gzip';
  else headers['content-length'] = String(sizeBytes);
  res.writeHead(200, headers);
  if (req.method === 'HEAD') {
    res.end();
    return;
  }
  const source = createReadStream(filePath);
  source.on('error', () => failStream(res));
  if (gzip) {
    const zipped = createGzip();
    zipped.on('error', () => failStream(res));
    source.pipe(zipped).pipe(res);
    return;
  }
  source.pipe(res);
}

function failStream(res: ServerResponse): void {
  if (res.writableEnded) return;
  if (res.headersSent) {
    res.destroy();
    return;
  }
  res.writeHead(500, { 'content-type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify({ error: 'static-read-failed' }));
}

function readFileStats(filePath: string): Stats | null {
  try {
    const stats = statSync(filePath);
    return stats.isFile() ? stats : null;
  } catch {
    return null;
  }
}

function isDirectory(path: string): boolean {
  try {
    return statSync(path).isDirectory();
  } catch {
    return false;
  }
}

function acceptsGzip(req: IncomingMessage): boolean {
  const header = req.headers['accept-encoding'];
  if (header === undefined) return false;
  const value = Array.isArray(header) ? header.join(',') : header;
  return value.toLowerCase().includes('gzip');
}

/** 只看导航请求（`accept: text/html` 且末段无扩展名）；API/资源请求不会被 index.html 兜底吃掉。 */
function looksLikeNavigation(req: IncomingMessage, urlPath: string): boolean {
  const accept = req.headers.accept;
  if (accept === undefined || !accept.includes('text/html')) return false;
  const lastSlash = urlPath.lastIndexOf('/');
  return !urlPath.slice(lastSlash + 1).includes('.');
}
