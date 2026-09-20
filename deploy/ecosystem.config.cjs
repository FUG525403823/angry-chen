// ============================================================================
// PM2 守护样例（ADR-004 §1）
//
// 验证状态：静态审查（本机未安装 PM2，未执行过 `pm2 start`）。
// 人类必须验证的点：
//   1) 依赖与路径：本样例假设仓库在 /srv/angry-chen、pnpm 在 /usr/bin/pnpm、
//      且已 `pm2 startup` 配置开机自启
//   2) 数据目录权限：sudo install -d -o <运行用户> -g <运行用户> -m 0750 /var/lib/angry-chen
//      日志目录同理：/var/log/angry-chen
//   3) 启动：pm2 start deploy/ecosystem.config.cjs && pm2 save
//   4) 冒烟：curl -s http://127.0.0.1:8787/health 返回 {"status":"ok",...}
//   5) 日志：pm2 logs angry-chen-server --lines 50
//
// 说明：PM2 没有 systemd 那样的 `EnvironmentFile=`，不会自动读 .env。
//   - 方式 A（本样例）：在下面的 env 块里写全部变量；
//   - 方式 B：启动前 shell 加载，`set -a && . ./.env && set +a && pm2 start ...`
// 本文件必须是 .cjs：根 package.json 是 "type": "module"，PM2 只支持 CJS 配置。
// ============================================================================

module.exports = {
  apps: [
    {
      name: 'angry-chen-server',
      cwd: '/srv/angry-chen',
      script: '/usr/bin/pnpm',
      args: '--filter @ac/server run start',
      // pnpm 是可执行文件而不是 JS 入口，必须关掉 PM2 的脚本解释器
      interpreter: 'none',
      exec_mode: 'fork',
      instances: 1,

      autorestart: true,
      restart_delay: 3000,
      max_restarts: 20,
      kill_timeout: 5000,
      max_memory_restart: '512M',

      env: {
        NODE_ENV: 'production',
        PATH: '/usr/local/bin:/usr/bin:/bin',
        PORT: '8787',
        HOST: '127.0.0.1',
        MAX_ROOMS: '64',
        MAX_PLAYERS_PER_ROOM: '4',
        DATA_DIR: '/var/lib/angry-chen',
        LOG_LEVEL: 'info',
        ALLOWED_ORIGINS: 'https://game.example.com',
      },

      out_file: '/var/log/angry-chen/server.out.log',
      error_file: '/var/log/angry-chen/server.err.log',
      merge_logs: true,
      time: true,
    },
  ],
};
