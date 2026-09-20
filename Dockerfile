# syntax=docker/dockerfile:1

# ============================================================================
# 验证状态：静态审查（构建环境缺失：本机无 docker，docker --version 失败）
#
# 本文件从未执行过 `docker build` / `docker run`，镜像未构建、容器未启动。
# 请不要把本文件当作"已跑通的部署方案"；首次真实构建前，
# 按 docs/运维手册.md §6「Docker 首次验证清单」逐条核对并回填证据。
# ============================================================================
#
# 形态（ADR-004 §1）：单进程、单端口 Node 进程承载 HTTP（/health、/metrics、/api/*）
# 与 WebSocket（游戏）。服务端零构建产物，直接用 tsx 跑 packages/server/src/main.ts。
#
# 构建期（build 阶段）：pnpm install + pnpm --filter @ac/client run build，
#   得到 packages/client/dist（客户端静态产物，含 gzip 体积预算断言）。
# 运行期（runtime 阶段）：只装 @ac/server 的生产依赖，用 tsx 跑服务端源码。
#   客户端 dist 一并复制进镜像（/app/packages/client/dist），但注意：
#   **当前 packages/server/src/http.ts 不提供静态文件服务**，只处理
#   /health、/metrics、/api/*；静态资源必须由 deploy/Caddyfile 或
#   deploy/nginx.conf 从同一份 dist 提供（见 docs/运维手册.md §3）。

FROM node:24-bookworm-slim AS base
ENV PNPM_HOME=/pnpm
ENV PATH=/pnpm:$PATH
RUN corepack enable && corepack prepare pnpm@11.22.0 --activate
WORKDIR /app

# ---- 依赖层：只复制清单与锁文件，最大化层缓存 ----
FROM base AS deps
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY packages/shared/package.json packages/shared/
COPY packages/server/package.json packages/server/
COPY packages/client/package.json packages/client/
# 不使用 --ignore-scripts：tsx/esbuild 需要安装脚本准备平台二进制。
RUN pnpm install --frozen-lockfile

# ---- 构建层：编译客户端静态产物 ----
FROM deps AS build
COPY packages/shared packages/shared
COPY packages/server packages/server
COPY packages/client packages/client
COPY tsconfig.base.json tsconfig.json ./
RUN pnpm --filter @ac/client run build

# ---- 运行层：只保留服务端运行所需内容 ----
FROM base AS runtime
ENV NODE_ENV=production
ENV PORT=8787
# 容器内必须监听 0.0.0.0，宿主只发布到 127.0.0.1，由反代对外（ADR-004 §1）。
ENV HOST=0.0.0.0
ENV DATA_DIR=/app/data
ENV LOG_LEVEL=info
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY packages/shared/package.json packages/shared/
COPY packages/server/package.json packages/server/
COPY packages/client/package.json packages/client/
RUN pnpm install --frozen-lockfile --prod --filter @ac/server
COPY --from=build /app/packages/shared/src packages/shared/src
COPY --from=build /app/packages/server/src packages/server/src
COPY --from=build /app/packages/client/dist packages/client/dist
COPY --from=build /app/tsconfig.base.json ./
# 对战记录与诊断报告落盘位置；compose 里挂成命名卷。
VOLUME ["/app/data"]
EXPOSE 8787
# 用 Node 自身的 fetch 做健康检查，避免在镜像里额外安装 curl/wget。
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
  CMD ["node", "-e", "fetch('http://127.0.0.1:'+(process.env.PORT||8787)+'/health').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"]
CMD ["pnpm", "--filter", "@ac/server", "run", "start"]
