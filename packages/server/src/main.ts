import { createServer, type ServerResponse } from 'node:http';

import { WebSocketServer } from 'ws';

import { buildHealthPayload, buildWelcomeFrame } from './index.ts';

const PORT = Number(process.env.PORT ?? 8787);
const HOST = process.env.HOST ?? '127.0.0.1';
const startedAtMs = Date.now();

function sendJson(res: ServerResponse, status: number, body: unknown): void {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(body));
}

const httpServer = createServer((req, res) => {
  if (req.method === 'GET' && req.url === '/health') {
    sendJson(res, 200, buildHealthPayload(startedAtMs, Date.now()));
    return;
  }
  sendJson(res, 404, { ok: false, error: 'not-found' });
});

const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', (socket) => {
  socket.send(JSON.stringify(buildWelcomeFrame(Date.now())));
});

httpServer.listen(PORT, HOST, () => {
  process.stdout.write(
    JSON.stringify({ level: 'info', evt: 'server.listen', host: HOST, port: PORT }) + '\n',
  );
});

function shutdown(): void {
  wss.close();
  httpServer.close(() => {
    process.exit(0);
  });
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
