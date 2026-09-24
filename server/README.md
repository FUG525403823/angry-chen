# server —— C++ 权威服务器

本目录是 v2 重构后的**服务端工程根**。它与 `client/`（Unity 客户端）**完全分离**，两者只通过 [ADR-009](../docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md) 冻结的 UDP 协议通信，不共享源码。

| 入口                                 | 位置                                                         |
| ------------------------------------ | ------------------------------------------------------------ |
| 线性制作计划（S01 → S15）            | [docs/plans-v2/server/](../docs/plans-v2/server/)            |
| 计划索引、跨链依赖与推荐执行顺序     | [docs/plans-v2/README.md](../docs/plans-v2/README.md)        |
| 技术前提（语言/版本/依赖白名单）     | [docs/03-技术选型-v2.md](../docs/03-技术选型-v2.md)          |
| 协议与传输契约                       | [ADR-009](../docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md)  |
| 跨语言确定性契约                     | [ADR-010](../docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md) |
| 旧 TypeScript 服务器（冻结对照实现） | `packages/server/`                      |

## 1. 构建与测试（S01 冻结的三条命令）

第一条是**唯一入口**（用 vswhere 定位 VS 自带的 cmake + ninja，配置失败才回退 mingw g++ 直编）：

```powershell
powershell -NoProfile -File server/build.ps1 -Config Release
```

第二条是它内部的 CMake 分支（绝对路径随 VS 安装位置变化，本机为 D:\tools\Visualstudio\software）：

```powershell
& 'D:\tools\Visualstudio\software\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S server -B server/build -G Ninja -DCMAKE_BUILD_TYPE=Release
& 'D:\tools\Visualstudio\software\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build server/build
ctest --test-dir server/build --output-on-failure
```

> `AC_WERROR` 默认 `ON`；CI 显式再加 `-DAC_WERROR=ON`（[ci.yml](../.github/workflows/ci.yml)），本地不必传。

第三条是 mingw g++ 直编兜底（不依赖 cmake）：

```powershell
g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror -Iserver/src server/src/main.cpp server/src/core/log.cpp -o server/build/ac_server.exe
```

> 该行只编 `ac_server.exe`（与 S01 §5.3 字面量一致）。测试程序要**另起一条命令**，不要把 `server/src/main.cpp` 带进去（双 `main()` 链接失败）：

```powershell
g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror -Iserver/src -Iserver/tests -o server/build/ac_tests_fallback.exe server/tests/*.cpp server/src/core/log.cpp server/src/net/*.cpp -lws2_32
```

> 兜底版 fixture 走相对路径（见 §4），必须在仓库根运行；S04 起需要 `-lws2_32`（WinSock2）与 `server/src/net/*.cpp`。

| 产物 | 路径 | 说明 |
| --- | --- | --- |
| `ac_server.exe` | `server/build/ac_server.exe` | ServerProcess 入口（`--version` / `--help` / `--selftest-log`） |
| `ac_tests.exe` | `server/build/ac_tests.exe` | 自研断言框架的全部用例；`--filter=<子串>` 只跑匹配用例 |
| `libac_core.a` | `server/build/libac_core.a` | `ac_core` 静态库（后续 `sim/net/ai/combat/room/persist` 都并进它） |
| ctest 用例 | 名 `unit` | `add_test(NAME unit COMMAND ac_tests)` |

后续计划的测试文件只需 `#include "tiny_test.hpp"` 并写 `AC_TEST(...)`，统一由 `ac_tests` 执行；**不得**另建第二套断言宏或第二个构建入口。CMake 用 `tests/*.cpp` 的 glob（`CONFIGURE_DEPENDS`）全量纳入 `ac_tests`，所以**新增测试文件不用改任何清单，也不要把测试 `.cpp` `#include` 进 `main_test.cpp`**（入口 `main()` 只在 `main_test.cpp`，重复纳入会撞符号）。

## 2. 结构化日志（S01 §5.4 冻结）

`server/src/core/log.hpp` 提供 `ac::log::write(Level, evt, fields)`、纯序列化 `ac::log::formatLine(epochMs, ...)`，
sink 切换 `useStderr()` / `useStdout()` / `useFile(path)` / `close()`（默认 stderr）。

- 字段顺序固定 `ts,level,evt,tick,room,<附加键按插入序>`；把 `tick` / `room` 当普通字段传入即可，序列化时自动提到第 4、5 位。
- `ts` 是定长 UTC ISO8601 毫秒（`YYYY-MM-DDTHH:MM:SS.mmmZ`，纯整数换算，不用 libc 时区接口）；double 用 `%.17g`，非有限值输出 `null`。
- 字符串转义：双引号写成 `\"`、反斜杠写成 `\\`、控制字符写成大写十六进制 `\u00XX`（例：换行 → `\u000A`）。
- 单行 ≤ 4096 字节：从第一个放不下的字段起其后字段全部丢弃（不留空洞），并在末尾追加 `"truncated":true`。
- [工程约定](../docs/00-共识/工程约定.md) §7 里 v1 的日志形状写法 `{ts, level, room, tick, evt}` 以本份 §5.4 的 `ts,level,evt,tick,room` 为准（§1 已把 §7 的 v1 细则降级为参考）。

## 3. 确定性内核（S02 §5 冻结）

`server/src/core/` 下五个**纯头文件**（全部 `inline`，无 `.cpp`）构成跨语言逐位可复现的运算底座：

| 头文件 | 内容 | 冻结点 |
| --- | --- | --- |
| `math.hpp` | `Vec3`（24B）/`Aabb`（48B）、`addScaled`/`length(Sq)`/`normalize`/`clamp01`/`wrapAngle`、`forwardFromYaw`/`rightFromYaw`/`yawPitchToDirection`、AABB 相交与包含、§5.5 时间常量 | `rightFromYaw = (cos yaw, 0, -sin yaw)` 是**镜像基**，禁止翻转；角度一律走查表 |
| `rng.hpp` | mulberry32 与 `ai`/`spawn`/`fx` 三流派生、`nextU32`/`nextDouble`/`rngInt`/`rngRange` | 位级锚点写在 `tests/rng_test.cpp`；模拟只读 `ai`/`spawn`，`fx` 只归表现层 |
| `quantize.hpp` | 位置（cm/`int16`）、角度（角度单位/`uint16`）、输入轴（`int8`）、血量比例（`uint8`）四组量化与反量化 | 舍入恒为 `floor(x + 0.5)`；非有限输入归 0（与 `wrapAngle` 同规则） |
| `hash.hpp` | CRC32C（反射多项式 `0x82F63B78`）：单次 `crc32c` 与分段 `crc32cExtend` | fixture 与角度表校验的唯一散列 |
| `trig_table.hpp` | `kSinQ30[65536]`、`kAtanUnits[1025]`、`kAsinUnits[1025]` 与 `sinUnits`/`cosUnits`/`radiansFromUnits`/`angleUnitsFromVector`/`angleUnitsFromRatio` | **生成物，禁止手改**（生成链见下） |

角度表生成链（第一步与客户端链共用，C02 §5.6 读同一份 JSON）：

```powershell
node tools/export-trig-table.mjs        # 写 docs/evidence/fixtures/trig-table.json（--check 只校验不写盘）
node server/tools/gen-trig-table.mjs    # 读该 JSON 生成 server/src/core/trig_table.hpp
```

采样规则、CRC32C 实现与三个冻结值集中在 `tools/lib/trig-table.mjs`，上面两个脚本都从它取——**两侧脚本比对的是同一组冻结 CRC，重新生成时任何采样口径漂移都会直接失败且不写盘**（S02 §8 的对策）。三个 CRC32C 是跨语言一致性的机器门禁（`server/build/ac_tests.exe --filter=trig` 复算）：`sin` `0x8BD9F737`、`atanUnits` `0x197C3A8D`、`asinUnits` `0xAD2BD35E`。

- **与 v1 的受控差异有两类**，对拍前必须对齐：
  1. **区间**：`wrapAngle` 按 S02 §5.1 归一到 `(-PI, PI]`，所以 `wrapAngle(-PI)` 返回 `+PI`（v1 `math.ts` 是 `[-PI, PI]`，返回 `-PI`）。量化后两者相同（都落 32768 单位），但把返回值当角度继续做浮点运算时会分叉。
  2. **角度消费函数走查表**：`forwardFromYaw`/`rightFromYaw`/`yawPitchToDirection`/`quantizeAngle` 先把输入折到 65536 角度单位，和**未打补丁**的 v1 `Math.sin/cos` 最大偏差约 `4.8e-5`。S07 对拍的 v1 侧必须按 [ADR-010](../docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md) §3 换成同一批整数表（S02 §5.4 已写明"两侧表内容逐字相同是逐位一致的前提"）。
- `wrapAngle` 只用 `+ - * /` 与 `floor`（不用 `std::fmod` 这类库函数，ADR-010 §2 的运算子集）；在 `|rad| <= 64` 上与 v1 的 JS `%` 逐位一致（1280 万采样零差异），并保留 v1 在负的 `2*PI` 整数倍上返回 `-0` 的符号位。`|rad|` 很大（≳100）时两者可能差 1 ulp，而 v1 唯一的模拟侧调用点是命令解码（`packages/server/src/command.ts:47`，输入是客户端 yaw/pitch，量级 ≤ PI），碰不到该区间。
- `angleUnitsFromRatio` 的精确规则（**C02 必须逐字复现**）：`r <= -1` → 49152、`r >= 1` → 16384、非有限输入 → 0；否则取 `i = clamp(floor(|r| * 1024 + 0.5), 0, 1024)` 对应的 `kAsinUnits[i]`，`r < 0` 时返回 `(65536 - u) & 0xFFFF`。误差带（实测，逐步细化到 0.0002）：`|r| <= 0.5` ≤ 6 单位、`<= 0.9` ≤ 12、`<= 0.99` ≤ 36、`<= 0.999` ≤ 104、`|r| < 1` 最坏 326（`r = 0.999512`）——**S02 §5.4 的"同量级（≤6 单位）"只对 `|r| <= 0.5` 成立**，要收紧须先按 §8 改契约（2049 点 + 两侧同一插值规则）。`angleUnitsFromVector` 相对 `atan2` 的上界 6 单位则实测成立（最坏 5）。
- 测试可以用 `std::sin`/`std::cos`/`std::atan2`/`std::asin` 当预言机（§6 的扫描只覆盖 `server/src/core`）；`server/src/**` 一律禁止超越函数调用（ADR-010 §2）。
- **需回写计划的字面差异（三处，实现按"保留既有冻结契约"处理）**：
  1. §7 DoD 要求 `server/src/core/**` 无 `.cpp`，但 `src/core/` 保留 S01 交付的 `log.cpp`：S01 §5.1 把日志归入 `core/` 并要求"头文件与实现同目录"，`ac_core` 静态库也至少需要一个实现文件（S02 的五个头文件本身是纯头文件）。日志不属于本份的确定性内核，未改动。
  2. `ac_tests` 的组成由 S01 §5.3 的 `ac_core + server/tests/main_test.cpp` 改为 `tests/*.cpp` 的 glob：后续每份计划都新增测试文件，逐个改冻结的构建清单会造成 15 次改动；`main()` 仍只在 `main_test.cpp`。**S06/S10 等计划里"注册进 `main_test.cpp`"一律按本文件的 glob 约定理解，不要把测试 `.cpp` `#include` 进去。**
  3. `quantize*` 对非有限输入（NaN/Inf）返回 0 是新增的防御规则（§5.3 未定义）——整数转换 NaN 是未定义行为，所以按 `wrapAngle` 的同一条规则归 0，并有 `quantize_non_finite_inputs` 用例钉住。

## 4. 二进制协议与编解码（S03 §5 冻结）

`server/src/net/` 把 ADR-009 的包头与载荷落成字节。**fixture 是双端唯一的字节真相**：客户端链的协议解码必须解同一批 `.hex` 并逐字节复现。

| 文件 | 内容 |
| --- | --- |
| `wire.hpp` | 小端定长读写（u8/i8/u16/i16/u32/i32/字节切片）、`PacketHeader`/`ReliableExt`/`FragmentHeader`、`payloadOffset = 8 + 12×reliable + 4×moreFragments`（可靠扩展头恒在前） |
| `codec.hpp` | `DecodeFailure`（9 值，数值与 C02 §5.7 的枚举同序）、`DecodeResult<T>`/`EncodeResult`、命令/快照/事件/握手/MatchState 的结构与入口 |
| `codec.cpp` | 全部 encode/decode 实现：只返回值、绝不抛异常、绝不越界 |

- 读取纪律：`ByteReader` 越界只置 `isTruncated` 并返回 0，**不推进游标**；判据只有该标志，调用方不得信任半读出来的值。写入纪律：`ByteWriter` 容量不足只置 `isOverflow`，只写到容量边界。
- 帧尾严格消费：快照/事件/MatchState 解码后必须正好用尽载荷，多余字节按 `kBadLength` 拒收；任何长度不足一律失败，不"补 0 继续"。
- 编码入口先校验包头自洽（版本、类型、flags 与 §5.1 的通道映射一致，分片包必须置 `moreFragments`），不一致直接 `isOk=false`；`encodeSnapshot` 另要求记录按 `EntityId` 升序唯一、单帧记录 ≤128、事件 ≤64、总长 ≤2048（超出由 S04 分片）。
- 差分判据（§5.3）：基线不存在该 id、或该 id 的 15 字节记录与基线不同 → 进实体块；**移除列表由编码器自行推导**（"基线存在且本帧不再存在"，升序），调用方不传移除列表。
- `eventId` 幂等键：`EventIdTracker` 单调去重，重复或倒退**静默丢弃**并累加 `duplicateCount`；`kDuplicateEventId` 只留给选择拒收的调用方（S04 通道层）。
- fixture 格式：`#` 注释；`# expect: key=value`（值用 `0x` 前缀大写十六进制，**键名一律小写**）；其余行是大写十六进制、单空格分组、按行序拼接。§6 的字符门禁只允许 `[0-9A-F #a-z=_,.:()-]`，所以注释与 expect 行里不能出现大写字母、`+` 或中文。
- 握手令牌：`reconnectToken` 的值是 u32（S04 §5.5：`salt ^ clientNonce`），**线上是 8 位小写十六进制 ASCII**（ADR-009「握手时序」+ C03 §5.5）；解码遇到大写或非十六进制字符按 `kBadValue` 拒收。所以 Hello 载荷 12 字节、Resume 载荷 8 字节。
- KeepAlive 更严：ADR-009 只说“每 500ms 一次、`flags.ackOnly` 时无载荷”，本实现要求 flags **恰好** `reliable|ackOnly`（0x5）且载荷 0 字节（§5.1 的类型映射），多置一位即 `kBadValue`。
- `ac_tests` 通过编译期宏 `AC_FIXTURE_DIR` 拿 fixture 绝对路径（CMake 注入；直编兜底时回退到相对路径 `server/tests/fixtures`，需在仓库根运行）。

### 4.1 fixture 清单（10 个，`server/tests/fixtures/`）

| 文件 | 字节 | 内容 | 往返 |
| --- | --- | --- | --- |
| `hello.hex` | 20 | Hello（type 1，不可靠，session 0）：nonce=`0x11223344`、token=`"00000000"`（8 位小写十六进制 ASCII） | ✅ |
| `hello_ack.hex` | 28 | HelloAck（type 2，可靠）：session=`0x123`、serverTick=100、salt=`0xDEADBEEF` | ✅ |
| `resume.hex` | 28 | Resume（type 3，可靠）：token=`"11223344"`=0x11223344（8 位小写十六进制 ASCII） | ✅ |
| `keepalive.hex` | 20 | KeepAlive（type 7，`reliable|ackOnly`，载荷 0） | ✅ |
| `command.hex` | 34 | Command（type 4，可靠）：moveX=127、yaw=`0x4000`(π/2)、buttons=`0x09`(fire|reload)、switchTo=1、seq=7、clientTick=12345 | ✅ |
| `snapshot_full.hex` | 40 | 全量快照（type 5，不可靠）：tick=100、1 个 player(id 3) 位于 (1.50, 0, −2.00)、yaw=π/2、hpRatio=128/255 | ✅ |
| `snapshot_diff.hex` | 27 | 差加快照：baselineTick=99、count=0、removedCount=1(id 3) | ✅（需基线） |
| `event_hit.hex` | 43 | 事件帧（type 6，可靠）：eventId=1、playerHit(命中点 1.20/1.00/−2.50)、value=25、flags=headshot|killed | ✅ |
| `fragment.hex` | 14 | 分片包（type 9，`moreFragments`）：fragId=7、index 0/2、2 字节不透明切片 | ✅ |
| `truncated_command.hex` | 12 | `command.hex` 的前 12 字节：解码必须返回 `kTruncated` | ✗（截断用例） |

### 4.2 计划文本纠正（已回写）与待裁决项

1. **`event_hit.hex` 的字节数与计划文本不一致（37 → 43）**：S03 §5.5 写 37 字节、`playerHit` 条目按 v1 的 12 字节（不含命中点）拼；但同一份计划 §5.4 与 §7 DoD 明确 `playerHit` = `type`+载荷 14 字节（含 `hitX/hitY/hitZ`），[ADR-009](../docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md) L108 冻结点也是 18 字节，C02 §5.2 同。按"§5.4 + ADR 高于 §5.5 的示例字节"实现为 **43 字节**（条目 18 字节）。这是本份唯一改动的计划示例字节，**已回写 S03 §5.5 该行**。
2. **`hello.hex` 16 → 20、`resume.hex` 24 → 28（已回写 S03 §5.5）**：令牌的线上形是 **8 位小写十六进制 ASCII**（ADR-009「握手时序」、C03 §5.5 逐字相同；S04 §5.5 只写字段类型 u32，不与之冲突），计划 §5.5 的示例按 4 字节原始 u32 拼是错的。回写后 Hello 载荷 12 字节、Resume 载荷 8 字节，解码遇到大写/非十六进制字符按 `kBadValue` 拒收。
3. **§6 的 `git -C D:\projects\tmp\angry-chen-bak status --short` 取不到"空"**：备份仓库没有 `.git`（`git status` 报 `fatal: not a git repository`）。改用 mtime 取证：`packages/**` 全量最新改动 2026-09-23 14:23（v1 运行期数据），`packages/shared/src` 最新 2026-09-22 17:26，都早于本份工作；v1 的 `protocol.ts` 只被只读读取。
4. **`# expect:` 的值语法计划未定义**（已回写 §5.5 表头）：本份统一为 `0x` 前缀大写十六进制 + 小写键名（同时满足 §6 的字符门禁）。C02 的 fixture 读取器按同一规则解析即可（键名比对大小写不敏感）。
5. **待裁决：ADR-009 的分片头在场条件写成「`moreFragments=1` **或** `fragCount>1`」**（L49），但末分片通常不置 `moreFragments`，按字面实现则末分片无法携带 `fragId/fragIndex`，S04 无法重组。本实现与 S03 §5.1 的 `payloadOffset` 公式一致，只认 `moreFragments`；**按工程约定 §8 应先改 ADR**。
6. **判断项（已用测试兜住，未改结构）**：事件「类型 → 字节/字段」的知识在 `decodeEventEntry` 的 switch、`writeEventEntry` 的 `if constexpr`、`eventPayloadWithTypeBytes` 与测试断言里各出现一次（§5.4 要求逐字段显式，故未做元表）；`eventTypeOf` 直接取 variant 下标当线号，因此 `codec_event_entry_bytes_all_types` 额外钉住「每个载荷类型 ↔ 线号」的对应，重排 `EventData` 会立刻变红。
7. 顺带修正 S01 的两个用例名：`log_oversized_evt_truncated` → `log_overlong_evt_truncated`、`test_filter_matching` → `test_filter_selection`。原因：`--filter` 是**全局子串**匹配，前者的 `sized` 污染 `--filter=size`、后者的 `matching` 污染 `--filter=match`（S03 §6 与 S10 都按固定条数校验）。**后续计划命名用例时必须避开 `codec`/`hex`/`fuzz`/`size`/`wire`/`match` 这些已占用的门禁子串。**

## 5. 传输子层（S04 §5 冻结）

`server/src/net/` 在编解码之上提供可自证的 UDP 传输：握手、每通道序号与 ack 位图、RTO 重传、分片重组、心跳、断线判定与宽限期重连。生产侧走真实套接字，测试侧走内存总线（同一批常量、同一套时序）。

| 文件 | 内容 |
| --- | --- |
| `udp_socket.hpp/.cpp` | 非阻塞 UDP 缝：`bind`/`sendTo`/`recvFrom`/`poll`/`close`，WinSock2 与 POSIX 双实现，错误码先映射到 `SocketError` 再判断 |
| `reliability.hpp/.cpp` | `ReliableState`（ack 位图）、RTO 表与重传时间线、重传表、失联判定 |
| `fragment.hpp/.cpp` | 分片发送（≤8 片、每片载荷 ≤1176B）与按 `(session, fragId)` 重组、60 tick 超时回收（计划 §5.4 写的 `channelType` 上不了线，见 §5.3.3） |
| `handshake.hpp/.cpp` | 会话短 ID 分配与校验、Hello/HelloAck/Resume/Disconnect 状态机、重连令牌 `salt ^ clientNonce` |
| `keepalive.hpp/.cpp` | 500ms 心跳、3s 断线判定、30s 宽限期计时、出站预算常量 |
| `memory_transport.hpp` | 测试用内存总线：丢包率、单向延迟与抖动、乱序、`pump(nowMs, onDeliver)` |

### 5.1 传输常量表（客户端链必须逐字对照实现）

| 常量 | 值 | 定义处 |
| --- | --- | --- |
| `kInitialRtoMs` | 200 | `reliability.hpp` |
| `kRtoBackoff` | 1.5（仅文档意义，表由整数递推得出） | `reliability.hpp` |
| `kMaxRtoMs` | 1000 | `reliability.hpp` |
| `kMaxRetransmits` | 5（第 6 次重传之前判失联） | `reliability.hpp` |
| `kRtoTableMs` | `{200,300,450,675,1000}` | `reliability.hpp` |
| `kKeepAliveMs` | 500 | `keepalive.hpp` |
| `kDisconnectMs` | 3000 | `keepalive.hpp` |
| `kGraceMs` | 30000 | `keepalive.hpp` |
| `kMaxFragmentPayload` | 1176（= 1200 − 8 − 12 − 4） | `fragment.hpp` |
| `kFragmentTimeoutTicks` | 60（3s） | `fragment.hpp` |
| `kOutboundBacklogBytes` | 65536 | `keepalive.hpp` |
| `kSnapshotBudgetBytes` | 1228（= `wire.hpp kSteadySnapshotBytes`） | `keepalive.hpp` |
| `kClientBandwidthBytesPerSec` | 40960 | `keepalive.hpp` |
| `kSessions` | 256（含宽限期会话；计划 §5.1 逐字用此名） | `handshake.hpp` |
| `kHelloDedupMs` | 5000（同一 `clientNonce` 的 Hello 重发去重窗口） | `handshake.hpp` |
| `kMaxPacketBytes` / `kMaxFragments` / `kMaxLogicalMessageBytes` | 1200 / 8 / 9408 | S03 的 `wire.hpp`（此处不重复定义） |

### 5.2 行为要点（与客户端逐条对齐）

- **ack 位图**（§5.3 逐字）：收到 `msgId = m` 时 `k = m - ackBase`；`k >= 1` 时 `ackBits = (k >= 32 ? 0 : ackBits << k) | (1 << (k-1))` 且 `ackBase = m`；否则 `d = ackBase - m`（1..32）置位 `1 << (d-1)`。bit i 表示 `msgId == ackBase - 1 - i` 已收到；重复或窗口外的 id 直接丢弃、不计错误。
  - ⚠️ 公式的副产物：**首次收包会把不存在的 `msgId 0` 记进 ackBits**（因为初始 `ackBase = 0`）。两端都会算出同一个位图，所以这不是 bug，**不要单方面"修正"**，否则线上字节不一致。
- **重传时间线**：发送时刻 0 → 第 1..5 次重传在 200 / 500 / 950 / 1625 / 2625ms（等待 `kRtoTableMs` 逐项）；第 5 次之后**再等一个表尾 1000ms**（t = 3625ms）才判失联，不产生第 6 次重传 → 进入宽限期 + `Disconnect(reason=3)`。判失联时点计划未冻结，见 §5.3.5。
- **序号与去重是两套**：`seq` 是包头字段、每通道一条发送/接收序号（`ChannelSeq`，u16 回绕，Snapshot 用它丢弃过期包）；`msgId` 只管去重与 ack。两者互不干涉。
- **分片**：逻辑消息 >1176B 才切；5000B → 5 片（1176×4 = 4704 < 5000）。`fragCount > 8`、`fragIndex >= fragCount`、同一组片数前后不一致 → `kBadValue` 并丢弃整组；60 tick 未收齐 → 丢弃整组并累加 `Reassembler::timedOutCount()`（`kFragmentTimeout` 不是 `DecodeFailure` 的取值；迟到切片会开启新组）。
- **握手**：`Hello`（`session = 0`、不可靠）→ 分配会话 ID + 派生 `salt` + 回 `HelloAck`；`salt` 由会话层独立计数器派生（**不消费 ai/spawn/fx 任何 RNG 流**）。令牌 `reconnectToken = salt ^ clientNonce`，线上是 8 位小写十六进制 ASCII（见 §4）。`Resume` 仅在宽限期内有效，成功后服务器补一次 `baselineTick = 0` 的全量快照。
- **会话与宽限期**：`Alloc → Connected →（3s 无任何合法包）→ GracePeriod →（+30s）→ Released`；释放后 ID 可复用、旧令牌失效，令牌不符/超期一律 `Disconnect(reason=2)`。非 `Hello` 包要求 `session != 0` 且在册（**宽限期也算在册**），否则丢弃并计 `kBadSession`（S03 的 `DecodeFailure::kBadSession` 就是给这里用的）；校验结果用 `SessionValidation::isGracePeriod` 告诉调用方，是否按 C03 §5.3「Zombie 收到本会话任何包即回 Connected」复活由调用方决定。
- **名额**：在册会话打满 `kSessions` 时按 §8 风险表**先驱逐最早进入宽限期的会话**；一个宽限期会话都没有（全在 `Connected`）才算真打满，回 `Disconnect(reason=6)`（§5.5 无专门取值，见 §5.3.4）。
- **Hello 去重**：客户端按 §5.5 以 1s × 5 次重发 Hello，服务器对同一 `clientNonce` 在 `kHelloDedupMs` 内复用同一会话并重发同一个 `HelloAck`（幂等），不占新名额。
- **心跳**：每 500ms 一个 `flags = reliable|ackOnly`、载荷 0 的包；`ackOnly` 包携带 ack 字段但**不进重传表**。

### 5.3 需回写计划/ADR 的差异（S04）

1. **§5.3 伪代码在 `k >= 33` 时移位越界**：`ackBits = (k >= 32 ? 0 : ackBits << k)` 只护住了左移，`1u << (k - 1)` 在 `k >= 33` 时是未定义行为。本实现在 `k >= 33` 时把位图显式清零（`k == 32` 仍与伪代码一致：`0x80000000`），`reliability_ack_bitmap_advance` 钉住。**建议回写 §5.3 伪代码**，客户端必须与之一致。
2. **沿用 S03 记录的 ADR-009 L49 歧义**：分片头在场条件写的是「`moreFragments=1` 或 `fragCount>1`」，实现只认 `moreFragments`（`payloadOffset` 同）。
3. **§5.4 的重组键含 `channelType`，线上拿不到**：分片包头 `type` 恒为 9（`kFragment`），原通道身份不上线（末分片还不置 `moreFragments`，见上条），所以重组只能按 `(session, fragId)` 分组，计划里的 `channelType` 无法从分片还原。**需裁决**：要么规定 `fragId` 在会话内全局唯一（本实现如此：快照/事件共用同一命名空间），要么在分片头加通道字段（要改 ADR + 两端编解码）。C03 必须与裁决一致。
4. **§5.5 未规定的三处**：`Hello` 带非 0 令牌时的语义（本实现记账但不使用，重连只走 `Resume`）；在册会话打满时的 reason（本实现取 6 `rateLimited`，且先按 §8 驱逐宽限期会话）；令牌的线上形是 8 位小写十六进制 ASCII（ADR-009 + C03 §5.5 已冻结，S04 §5.5 的表格只写 u32 类型，应补一句指向 ADR）。
5. **判失联时点未冻结**：§5.3 只说“累计 5 次重传仍无 ack 即判失联”，没写第 5 次之后是否再等一个 `kRtoTableMs` 尾项。本实现在 t = 3625ms 判（五连等 200/300/450/675/1000 之后再等 1000），另一种读法是 t = 2625ms 立即判。C03 同样沉默，**需裁决**。
6. **宽限期会话的包校验**：§5.2 只要求 `session != 0` 且在册，本实现据此让宽限期会话通过校验（不计 `kBadSession`），把“是否复活”留给调用方（C03 §5.3 的 Zombie 语义）。若计划要求“宽限期一律丢弃”，需回写 §5.2。
7. **用例名冲突修正（7 个，含 3 个 S03/S02 用例）**：`hex_fragment` → `hex_split_message`、`size_single_packet_needs_fragments` → `size_single_packet_needs_slices`、`size_full_single_entity_snapshot_is_40` → `size_full_single_record_snapshot_is_40`、`match_truncated_and_trailing_rejected` → `match_truncated_and_extra_bytes_rejected`、`math_aabb_overlaps_and_contains` → `math_aabb_overlaps_and_covers`、`transport_handshake_allocates_session` → `transport_handshake_assigns_session`、`transport_loss_triggers_retransmit` → `transport_loss_causes_resend`。原因同 §4.2.7：`--filter` 是**全局子串**匹配，会污染 `--filter=fragment` / `alloc`（S05）/ `entity`（S05）/ `ai`（S09）/ `trig`（S02、S09）的固定条数门禁。**已占用门禁子串全表**（命名新用例前先对照）：`ai alloc codec combat entity fixture fragment fuzz grace grid hex http malicious match matchstate math memory pose quantize reliability replication rewind rng schedule security size step store threshold transport trig waves wire world`。
8. **遗留项（本份未动）**：S02 的 `rng_ai_stream_bits` 含 `ai`，会让 S09 §6 的 `--filter=ai` 从 `TESTS 22/22` 变 23 条——名字本身没错（它就是 ai 流），**S09 立项时要先改名或改门禁**；另 S09 §7 第 9 条写的 `--filter=fixture` 在本套测试里是 `TESTS 0/0`（疑为 `--filter=codec` 之误，codec 恰为 14/14），S09 落地前需澄清。
9. **`udp_socket_loopback_roundtrip` 不在 §6 的五组内**：那五组按固定条数（8/5/4/4/2）校验，套接字缝的真实回环收发单独一条用例覆盖（真 UDP、非阻塞、`poll` 超时）。POSIX 分支本机（Windows）跑不到，只在 WinSock2 分支上验证过。

## 6. 硬约束（来自 ADR-008 / ADR-009 / ADR-010）

1. C++20；**无第三方运行时库**——UDP 可靠性层、JSON 日志、测试断言框架全部自研（新增依赖需先写 ADR）。
2. 量化、字节序、包头与通道语义一律以 ADR-009 为准，服务端不得单方面扩展字段。
3. 模拟热路径只用 `+ - * / sqrt` 与整数运算；编译禁用 fast-math 与 `-march=native`（ADR-010），Release 固定 `-O2`、`-ffp-contract=off`、`-fno-fast-math`、`-Werror`。
4. 零外部素材：本目录不得出现任何二进制资源文件（`node tools/check-assets.mjs` 会拦）。

## 7. 当前状态

**S01–S04 已完成**：构建链、自研断言框架、结构化日志（S01）、确定性内核（S02）、二进制协议编解码（S03）与 UDP 传输子层（S04：套接字缝、可靠性、分片、握手、心跳/宽限期、内存总线）就位；模拟/AI/房间/持久化由 S05 起的各份计划按"交付物"章节逐份创建，**不预先存在**。

本机实测（2026-09-24，Windows 11 + Windows PowerShell 5.1）：

| 命令 | 实测 |
| --- | --- |
| `g++ --version` | `g++.exe (x86_64-win32-seh-rev1, Built by MinGW-Builds project) 15.2.0` |
| `powershell -NoProfile -File server/build.ps1 -Config Release` | 退出码 0，末行 `[build] ok ac_server.exe`；`[build] toolchain: cmake 4.2 + ninja + C:\\Program Files\\mingw64\\bin\\g++.exe` |
| `server/build/ac_server.exe --version` | `ac_server 0.1.0 protocol=1 tick=50ms` |
| `server/build/ac_tests.exe` | 末行 `TESTS 110/110`，退出码 0（S01 的 18 条 + S02 的 28 条 + S03 的 40 条 + S04 的 24 条） |
| `--filter=transport` / `--filter=reliability` / `--filter=fragment` / `--filter=grace` / `--filter=memory` | 末行依次 `TESTS 8/8`、`TESTS 5/5`、`TESTS 4/4`、`TESTS 4/4`、`TESTS 2/2`，退出码全 0 |
| `server/build/ac_tests.exe --filter=reliability` 的打印行 | `rto=200,300,450,675,1000` |
| 丢包 20% + 延迟 50ms(±10) + 乱序 10% 的 1000 tick 回环 | 50 条可靠消息全部到达、按 `msgId` 严格升序交付、重传表清空、`droppedCount() > 0`（`memory_reliable_delivery_in_order`） |
| 分片切分与本地重组（不过总线） | 5000B → 5 片、每片 ≤1200B、逆序投递逐字节还原；>9408B / 0B 返回空并判不可分（`fragment_split_5000_bytes_reassembles`） |
| 分片过总线（20% 丢包 + 延迟 20ms ±5） | 每 200ms 重发一轮，5 片逐字节还原、`completedCount() == 1`、`timedOutCount() == 0`（`transport_split_logical_message`） |
| 可靠分片（事件通道） | 每片带 12B 可靠扩展头，载荷偏移 = 8 + 12 + 4 = 24，逆序重组同样还原（同上用例） |
| 会话名额（§8 驱逐规则） | 打满 `kSessions` 后：全 `Connected` → `Disconnect(reason=6)`；有宽限期会话 → 驱逐最早进宽限期的那个并收下新会话（`grace_release_after_thirty_seconds`） |
| Hello 重发去重（§5.5） | 同一 `clientNonce` 在 5s 内复用同一会话与同一令牌，超过 `kHelloDedupMs` 才算新握手（`transport_handshake_assigns_session`） |
| 每通道序号（§5.2） | `ChannelSeq` u16 回绕、重复/过期包判旧（`reliability_duplicate_is_ignored`） |
| 心跳与断线实测 | 50s 内恰好 100 次心跳、相邻间隔恒 500ms；最后一个合法包后 3000ms 判断线并进入 30s 宽限期 |
| `Get-ChildItem server/src/net -Recurse -Include *.hpp,*.cpp \| Select-String -Pattern "std::pow","exp\(","\b0\.0[0-9]* \* pow"` | 0 命中 |
| g++ 直编兜底（同上 + `-lws2_32`） | `ac_tests.exe` 兜底版跑出 `TESTS 110/110`（含真实 UDP 回环那条） |
| `server/build/ac_tests.exe --filter=rng` / `--filter=quantize` / `--filter=math` / `--filter=trig` | 末行依次 `TESTS 6/6`、`TESTS 10/10`、`TESTS 8/8`、`TESTS 4/4`，退出码全 0 |
| `server/build/ac_tests.exe --filter=trig` 的打印行 | `sin crc=0x8BD9F737 atan crc=0x197C3A8D asin crc=0xAD2BD35E` |
| `--filter=codec` | 末行 `TESTS 14/14`，退出码 0 |
| `--filter=hex` | 10 行 `hex <name> ok` + 末行 `TESTS 10/10`，退出码 0 |
| `--filter=fuzz` | 末行 `TESTS 3/3`；三条各 1200 次随机损坏（共 3600 ≥ DoD 的 1000）无崩溃、无越界 |
| `--filter=size` | 末行 `TESTS 4/4`（34 / 40 / 1228 / 1200 断言） |
| `--filter=wire` | 末行 `TESTS 3/3`（`writeU16(0x1234)` → `34 12`；越界读写只置标志） |
| `--filter=match` | 末行 `TESTS 6/6`（两玩家往返、名称 1/12 字节边界、`count>4`、空名/超长名、非法 weapon、截断与多余字节） |
| `Get-ChildItem server/tests/fixtures -Filter *.hex \| Select-String -Pattern '[^0-9A-F #a-z=_,.:()\-]'` | 0 命中 |
| fixture 往返（DoD §7） | 10 个 fixture 全部解码 + 9 个往返逐字节相等；`truncated_command.hex` 返回 `kTruncated` |
| 事件表长度断言 | `type`+载荷 = 14/6/4/6/3/7/5/5/7/5，条目总长 = 18/10/8/10/7/11/9/9/11/9 |
| 尺寸预算 | 命令包 34B、单实体全量快照 40B、稳态差分包（40 变化 + 2 事件）≤1228B、128 实体全量 1945B（≤2048B 且 >1200B ⇒ 需 2 分片） |
| `server/build/ac_tests.exe --filter=log_double` | `PASS log_double_format` + 末行 `TESTS 1/1`，退出码 0 |
| `node tools/export-trig-table.mjs --check` | `trig-table.json OK (65536 entries) sin=0x8BD9F737 atan=0x197C3A8D asin=0xAD2BD35E` |
| `node server/tools/gen-trig-table.mjs` | `写入 …\server\src\core\trig_table.hpp（802893 字节）` |
| 角度表体积（S02 §8 阈值 1 MB / 1.5 MB） | `trig-table.json` 709640 字节、`trig_table.hpp` 804068 字节，均未触发 |
| `wrapAngle` 与 v1 JS `%` 的位级等价（|rad| <= 64） | 1280 万采样 0 处差异；`wrapAngle(-2*PI)` 保留 v1 的 `-0` 符号位（用例断言 `signbit`） |
| `angleUnitsFromRatio` 相对 `asin` 的误差带 | ≤6 单位（|r| ≤ 0.5）、≤12（0.9）、≤36（0.99）、≤104（0.999）、最坏 326（`r = 0.999512`） |
| 备份仓库只读（S02/S03 DoD） | `angry-chen-bak` 无 `.git`（`git status` 取证无效），改用 mtime：`packages/shared/src` 最新改动 2026-09-22 17:26、`packages/**` 全量最新 2026-09-23 14:23（v1 运行期数据），均早于本次工作 |
| `angleUnitsFromVector` 相对 `atan2` 的偏差（§5.4 上界 6 单位） | `dx=0.3, dz=0.7` 差 1 单位、`dx=-0.9, dz=0.2` 差 4 单位；遍历 20000 个方向最坏 5 单位 |
| `ctest --test-dir server/build --output-on-failure` | `100% tests passed, 0 tests failed out of 1` |
| `server/build/ac_server.exe --selftest-log \| ConvertFrom-Json` | 解析成功，键序 `ts,level,evt,version,protocol,tickMs` |
| g++ 直编兜底（第三条命令 + 同开关 `-I server/tests`；S03 起还要加 `server/src/net/codec.cpp`） | `ac_server.exe` 与 `ac_tests.exe` 均编译成功、`--version` 与 cmake 分支一致，且兜底版 `ac_tests.exe` 末行同为 `TESTS 86/86`（走相对路径 fixture，需在仓库根运行） |
| `Select-String -Path server/CMakeLists.txt,server/build.ps1 -Pattern 'ffast-math','-march=native','-mfma'` | 0 命中 |
| `Get-ChildItem server/src/core -Recurse -Include *.hpp,*.cpp \| Select-String -Pattern "std::(sin\|cos\|tan\|atan2\|asin\|exp\|log\|pow\|round\|lround\|llround)"` | 0 命中（S02 §7 的门禁；`Select-String server/src` 全目录同规则也是 0） |
| `node tools/check-docs.mjs` | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 50 个文档、154 条相对链接） |
| `node tools/check-assets.mjs` | 退出码 0：158 个受控文件零素材类扩展名（含 `client/` 侧 Unity 工程文件，非本份引入；该计数随客户端链增长，此处是本机时点值）；`C++ 构建清单：未发现第三方依赖引入` |

已知环境边界（不是仓库缺陷）：CMake 在配置阶段用管道捕获编译器输出，受限沙箱（含 workspace-write）会卡在 `Detecting CXX compiler ABI info`；需要完整文件访问才能跑通 cmake 分支与 `ctest`。g++ 直编兜底不受影响。
