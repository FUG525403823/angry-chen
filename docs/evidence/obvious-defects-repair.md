# 明显缺陷修复记录

## 边界

用户已批准静态扫描后的修复计划。禁止启动 Unity/Tuanjie，包括 batchmode、自检、客户端构建和游戏窗口。客户端引擎验收必须等待用户解除限制；不能把静态检查算作运行通过。

保留原有未跟踪内容：`client/.codely-cli/`、`client/.codely.packages/`、`tools/failed.png`。不自动提交或推送。

## 计划与发布分界

1. 协议 1：修复 UDP 来源绑定、Resume 幂等及拒绝不删除身份、网络/房间断线同步。
2. 协议 1：HTTP 固定容量非阻塞状态机，读取/写出各 2 秒绝对截止，可信代理身份与业务/监控限流分开，默认回环 HTTP，部署端口使用环境配置。
3. 客户端聊天：同帧 Return 只消费一次，提交后不重新聚焦；补公共帧输入入口回归。
4. 先运行和记录协议 1 服务端回归，保留此阶段发布证据。
5. 独立协议 2：在单播 MatchState 载荷尾部增加 localPid:uint16，服务端填接收会话权威 pid，客户端不再按昵称认领；同步双端 codec、大小常量、fixture、bot、版本断言与 ADR。协议 2 不得在新版客户端获准验证之前对外替换服务。
6. 服务器 43.143.120.65 尚无已配置 SSH 主机；未连接、未部署。配置安全连接后先只读检查系统/架构/端口/既有服务，非 root 账号运行；备份二进制、配置和数据，失败回滚代码配置，不删除战绩。凭据不进入仓库、脚本或命令行。

## 静态发现

- UDP 会话分派没有在更新心跳/ACK及处理命令前校验来源端点。
- 重复 Resume 或错误令牌会删除原有会话，且 Runtime/房间清理不统一。
- 网络层进入宽限期只计数，没有通知房间清空旧移动/开火输入。
- 默认同名玩家的 LocalIdentity 总选同名最小 pid，视图/预测可能串线。
- HTTP 在 Runtime 主循环同步等待完整请求、发送与清理，慢连接阻塞游戏。
- HTTP 反代请求按同一个回环地址计限流，业务与监控共用额度。
- 聊天提交关闭后，同帧 Return 又重新聚焦并暂停游戏输入。

以上为本次只读扫描得出的调用链结论，扫描阶段未构建或运行复现。

## 执行状态

- 服务端会话修复：协议 1 阶段已完成且全量回归通过。
- HTTP 修复：协议 1 阶段已完成且全量回归通过。
- 聊天修复：已添加 `Chat.ApplyInputFrame` 公共同帧入口，生产驱动与回归用例共用；Return 提交后不再重开。静态 diff 检查通过；Unity 测试禁止运行，运行验收仍待办。
- 协议 2 身份升级：双端代码与测试/fixture迁移已完成；服务端545/545通过，客户端仅静态检查。尾部 localPid=0 表示未绑定，非0必须唯一出现在玩家表中。生产不再按昵称回退认领；断开/新会话清空身份，同会话Resume保留。
- 部署：尚未开始，安全 SSH 连接缺失。

## 协议 1 验收快照（2026-09-30）

- CMake Release / Werror 构建成功；直接运行全量 `ac_tests` 为 **541/541**，退出码 0。
- CTest：**1/1 unit 通过**（其内部运行全量用例），5.47 秒，退出码 0。
- HTTP listener 分组 **20/20**，CLI 分组 **12/12**；断线后旧移动/开火停止、恢复同一 pid、错误令牌不删身份、重复恢复幂等与来源校验均通过。
- 报告测试修正后两局 jitterP95/scheduleP95 均 **40ms**，保持原阈值；未更改生产报告墙钟时长。
- `build/repair-stage1/` 保存未提交差异 `changes.patch`、Windows 服务端/机器人/测试/门禁可执行文件与构建/全量日志。版本验证：`ac_server 0.1.0 protocol=1 tick=50ms`。
- 协议 1 `ac_server.exe` SHA-256：`A6872278F2C233CDB81D27B640F0142C2A225FA7F84ACA5B497405AA779DC8E3`。
- 协议 1 四人＋60羊、120秒无头门禁已运行：`verdict=pass exit=0`，CPU均值0.566%、P95 3.119%，每客户端最大21.351KB/s，快照P95 1033B/最大1130B，事件丢弃/跳tick/未捕获异常均0，HTTP health/metrics均200。产物 `build/repair-stage1/gate-4p2min.json` 与同名 `.log`。
- 门禁限定：G6 严格调度P95实测15ms，未达到2ms严格值；按仓库既有Windows替代判据（前后P95差0ms，simDrift最大22ms）判通过，未修改门禁阈值。G7 RSS增长斜率为not-measured；此短测不代表长时间泄漏验证，也不等同Unity客户端硬纠正验证。
- 上述为本机 Windows 回归，不代表 Linux 产物或已部署。

## 验证记录

- `node tools/check-docs.mjs`：修复前通过，30 份计划、80 个文档扫描。
- `node tools/check-assets.mjs`：修复前因运行环境 `spawnSync git EPERM` 未完成。不是素材规则失败；尚无通过结果。未绕过沙箱限制。
- C++ 首个红测：`grace_wrong_token_rejected` 在原生产代码上 `TESTS 0/1`，失败断言为原 session 应仍存在；删除错误令牌处理中的会话释放后，增量构建成功，`TESTS 1/1`。
- HTTP 首个红测：`listener_partial_request_does_not_block_poll` 在原生产代码上 `elapsed=2073.0ms completed=1`，`elapsed < 100.0` 断言失败，`TESTS 0/1`。
- 素材门禁重跑：用户切换本会话权限后再次执行 `node tools/check-assets.mjs`，635 个受控文件、34 个官方依赖，通过。
- 构建权限：首次 Ninja `.ninja_lock` 写入拒绝；经授权运行权限诊断，为工作区 `server` 与 `server/build` 补充当前用户权限并读回验证成功，未改所有者/文件内容。报告与恢复脚本在 `build/permission-recovery/`，报告名 `acl-report-dab9e6cfdefa4adca8448b15ecd0685d.jsonl`。随后用户切换主会话完全权限，CMake 构建成功；不能仅归因于权限修复。恢复次序为报告中的 server（c712e72adf2c42ebb1d086dc5ffef4f6）→ server/build（4691432d77584c039b1296ebc5229761），未执行恢复。
- `grace_resume_duplicate_is_idempotent`：原实现失败于 `duplicate.isAccepted`；最小实现后 `grace_` 组 7/7 通过。
- `runtime_session_rejects_other_endpoint_before_ack_and_liveness`：跨端点输入原实现失败；补普通包来源校验后 1/1 通过。追加非零 session Hello 回归后再次变红，正在补入口绕过修复。
- `runtime_hello_nonce_is_bound_to_endpoint`：原实现返回相同 session（红），增加端点作用域后 1/1 通过。
- `runtime_grace_disconnect_and_same_client_resume`：实测原实现失联后 players 不为 0（红），正在实现房间同步。
- HTTP 非阻塞：`listener_partial_request_does_not_block_poll` 修后约 0.2ms，completed=0；独立响应绝对期限用例先红后绿；分片/EAGAIN并发、读取不因进度续期、提前EOF、写入不因进度续期及巨头回归已通过。当前 listener 14/15，唯一失败是尚未实现的可信代理独立限流回归。
- HTTP 监控限流回归先红后绿：监控不再消耗业务额度，原30次业务限流仍保留。
- `serve_cli_`：12/12 通过，覆盖 HTTP bind/可信代理选项、合法边界、非法/重复/缺值、环境默认/成功/失败及CLI覆盖。环境无效时不能被CLI掩盖。
- 首次全量回归：`TESTS 536/541`，未通过5项已逐项分派处理：旧Runtime静默测试2项、新断线/重连集成2项、启用代理后本机无头健康请求1项。此数字不是最终验收结果。
- 后续分组复验：`listener_` 20/20 全部通过（含缺头本机健康、真实非回环来源伪造头拒绝）；`runtime_grace_stops_old_movement_and_fire` 与 `runtime_advances_round_and_replicates_snapshots` 通过。重连已修实际 token 赋值顺序：`roomJoin` 会清零旧令牌，现在准入后再赋权威令牌；剩余身份广播回归等待固定步长测试修正后复验。
- 发现并修正测试前提：旧持续在线用例没有发心跳；moveX 在 yaw=0 时沿 z 轴，断线停止回归改为同时验证 x/z；单次跳1000ms受追帧上限限制不能等待完整广播周期，改按50ms连续推进而不删除pid/name断言。
- 报告时序回归曾红，修测试时钟与比较口径后已绿：HTTP测试helper推进时间却未回传调用者，造成时间回退/重复tick；全员宽限期现在实际暂停，报告墙钟时长包含暂停，tick时长与墙钟减去观察到的暂停比较。保留60ms/500ms阈值，不修改生产报告时长来掩盖。
- 协议 2 首个红测：`match_local_pid_tail_contract` 在协议 1 生产实现上失败于 `decoded.isOk`（67字节含权威pid的载荷无法解析），`TESTS 0/1`；随后开始双端 codec 与身份接线。
- 协议 2 服务端首批实现后 `TESTS 542/542`；附加共享fixture磁盘逐字节往返、尾部缺失/半尾/多余/非法成员、同名双socket权威pid与Ready归属后，最终 **545/545**，退出码0。CTest **1/1**（4.76秒）通过，`ac_server --version` 为 `0.1.0 protocol=2 tick=50ms`。
- 协议 2 四人＋60羊 **12秒冒烟** `verdict=pass exit=0`，HTTP health/metrics200、事件丢弃/跳tick/异常0。仅12秒，不代替120秒或长期门禁；独立CPU采样阶段被短跑跳过，输出CPU=0不能当CPU实测结论；G7未测，G6仍用既有Windows替代判据。记录在 `build/repair-stage2-gate-smoke.json`/`.log`。
- 独立限定只读复核未发现新的高置信来源绕过/localPid注入错误。全员grace暂停后恢复的scheduler epoch指标补偿未专门验证，列后续验证候选而非已确认缺陷。
- 客户端协议2静态迁移已完成：生产/测试版本常量为2，GameLoop独立昵称只用于Join；MatchState尾部唯一成员校验和权威身份/连接状态清理已接线。FrameBench、Boot、Codec、Identity、Presentation、Render builders同步尾字段；JointSuite按权威pid查行；ReleaseSuite显式拒绝旧协议1。新增共享fixture读取断言、同名/改名/未绑定/重连生命周期回归。全库有界搜索无旧MatchPid/Identity.Name/Resolve(Players)生产残留，`git diff --check` 为0。
- 客户端测试只补写，未执行编译、构建或测试，不能据服务端测试宣布双端运行验收。
- 最终文档门禁、素材/官方依赖门禁与差异空白检查均退出码0。素材门禁仅检查已受控文件；新增文本fixture另由服务端测试实读。未自动提交或推送；原3项未跟踪内容保留。
- HTTP代理：可信代理下不同客户端独立限流、禁用时伪造头忽略、无效/重复头拒绝、IPv6规范化及非回环来源忽略头均已在首次全量通过；无头本机健康请求先红，已最小修改fallback，待全量复验。
- HTTP额外验证缺口：未实际压满OS发送缓冲触发send-EAGAIN，未单测32槽全部占满；已覆盖60000字节跨轮响应及写出绝对期限。上述缺口不冒称已测。
- Linux 验证：本机 `wsl --list --quiet` 返回未安装WSL信息，未安装系统组件；Linux编译/运行（含SIGPIPE）及Nginx/Caddy/systemd语法与启动仍未验证，未部署。
- 客户端构建、测试、交互：未运行，受禁止启动 Unity 的用户约束。
