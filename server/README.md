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
g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror -Iserver/src -Iserver/tests -o server/build/ac_tests_fallback.exe server/tests/*.cpp server/src/core/*.cpp server/src/net/*.cpp server/src/sim/*.cpp -lws2_32
```

> 兜底版 fixture 走相对路径（见 §4），必须在仓库根运行；S04 起需要 `-lws2_32`（WinSock2）与 `server/src/net/*.cpp`；S05 起需要 `server/src/sim/*.cpp`，S06 起另有 `server/src/config/*.hpp`（纯头文件常量，不产生新的编译单元，兜底命令无需改动）。

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

## 6. 模拟数据布局（S05 §5 冻结）

`server/src/sim/` 是纯数据层（只依赖标准库与 `core/**`，不引 `net/**`、`ai/**`），热路径零堆分配：
`World` 由 `createWorld(seed)` 一次性定长预分配（实测 `sizeof(World) = 115840` 字节 ≈ 113 KiB），此后每 tick 只在已分配的数组上做计数与写入；`Entity` 96 字节、`PoseHistory` 7688 字节、`SpatialGrid` 3652 字节、`Event` 8 字节（取证行见 §9 表格）。

### 6.1 World 字段表（类型、顺序、容量不得改）

| 字段 | 类型 | 容量 / 语义 |
| --- | --- | --- |
| `seed` | u32 | 三条 RNG 流的派生源（`createRng(seed, kAi/kSpawn/kFx)`） |
| `tick` | u32 | 0 基；`stepWorld` 阶段 0 先 `++tick`（`timeMs` 见下一行） |
| `entities` | `Entity[1024]` | 下标 = `EntityId - 1`，**永不搬移**（跨 tick 持有的指针/引用仍有效） |
| `activeIds` | u16[1024] + `activeCount` | **严格升序**；下游只准沿它遍历（唯一遍历入口） |
| `freeIds` | u16[1024] + `freeCount` | 空闲栈，LIFO：最近释放的先复用 |
| `highWater` | u16 | 历史最大 `EntityId`；`activeCount + freeCount == highWater` 恒成立 |
| `rng` | `{ai, spawn, fx}` | 三条独立流，禁止跨流借数（ADR-010） |
| `stats` | `{aliveSheep, eventsDropped}` | 前者每 tick 重算，后者只累加、不随 tick 清零 |
| `poseHistory` | 见 §6.3 | 回滚命中用的姿态环 |
| `grid` | 见 §6.4 | 每 tick 重建的派生结构（不是权威状态） |
| `events` | `Event[256]` + `eventCount` | 每 tick 开头清零；满 256 丢**新**事件并 `++eventsDropped` |
| `timeMs` | u32 | S06 §5.1 阶段 0 **追加在末尾**：tick 的毫秒镜像（`+= dtMs`，恒等于 `tick * 50`）；S12 的模拟漂移口径读它 |
| `eventCursor` | u16 | S06 §5.1 阶段 0 **追加在末尾**：事件遍历游标，阶段 0 清零（S12 发事件时消费） |

`eventCount` 的线上形是 u8（编码上限 255，S05 §5.1 原文），但**单帧事件预算是 ≤64**（ADR-009 / S03 §5.4），超出部分走 `EventChannel` 可靠通道补发 —— 256 是本进程的缓冲容量，不是每帧发送量。
`Event` 目前只固定 S03 §5.4 冻结的条目头：`eventId` u32（从 1 起单调递增、全局唯一、幂等去重键）+ `type` u8（1..10，ADR-009）；各类型载荷（`subjectId`/`targetId`/`value`/`flags`/`hitX..` 等，字段名以 S03 字段表为准）由 S06 起**追加**在末尾。

### 6.2 实体表不变量（100k 次 spawn/despawn 压测后仍成立）

1. `activeCount + freeCount == highWater`；
2. `activeIds` 严格升序且无重复；`freeIds[0..freeCount)` 无重复；每个 id 恰好属于二者之一；
3. 分配：`freeCount > 0` 取栈顶，否则 `++highWater`；池与栈都空 → `SpawnResult{isOk=false, reason=kEntityPoolExhausted}` 且**不改任何状态**；
4. 释放：只置 `active = false` 并入栈；**字段在分配时整体重置**（复用的槽不会留下上一轮的 `vel / aliveMs / idle`）；
5. `Entity` 96 字节：`id` u16 + `active` bool + `kind` u8 = 4，+4 填充，`pos` 24、`vel` 24、`yaw` 8、`pitch` 8、`hp`/`maxHp`/`armor` 各 4、`state` 1、`team` 1、`ownerId` 2、`aliveMs` 4、`idle` 1、尾部填充 3 ⇒ 96；分配后 `maxHp = hp`；
6. id 不保证递增（复用是 LIFO），所以 `activeIds` 才是排序视图，`activeIndexOf` 走二分。

### 6.3 姿态环（§5.3）

20 槽 × 8 玩家 × 48 字节（5 double + u16 id + 对齐）= **7680 字节**，只记 `kind == player`（计划 §7 写的 `static_assert(sizeof(PoseHistory)) == 7680` 在本份的冻结签名下不可实现：环必须自带 `newestTick`/`writeCount`，`samplePoseAgo` 才拿得到时间基准，故断言落在槽位区 `sizeof(PoseHistory::slots) == 7680`；实测 `sizeof(PoseHistory) = 7688`，见 §6.5）；
`recordPoseHistory` 在每 tick 末尾写 `slots[tick % 20]`（并重写整槽，避免上一圈残留）。
`samplePoseAgo(const PoseHistory& h, uint32_t ms, uint16_t id, SampledPose& out)`（`ms` 与 §5.3 的签名同型）：`ms > 200` → `isOk = false`（`clamped = true` 仅诊断，**不回滚**）；
否则在 `newestTick - ms/50` 与它的前一个 tick 之间按 `alpha = 1 - (ms % 50) / 50` 插值，
`alpha ∈ {0, 1}` 时直接返回端点（`ms = 0` 因此逐位等于最新槽位，无插值误差）；
历史不足时夹到最老槽（`isOk` 仍为 true），只有该实体在两槽都无记录时才 `found = false`。
插值只用 `+ - * /`（ADR-010）。

### 6.4 空间网格（§5.4）

4m 单元格、20×20 = 400 格，`cellStart[401]` + `cellItems[1024]`；每 tick 两趟计数排序按 `activeIds`
升序重建，因此**每格内 `EntityId` 升序**；`kind == projectile` 不入网格；越界坐标按
`clamp(floor((x + 40) / 4), 0, 19)` 夹到边界格（查询只会多访问、不会漏配对）；
`forEachNeighbor` 的访问顺序是 `cz` 升序 → `cx` 升序 → 格内下标升序（确定性契约）。
若遥测显示单次邻居查询经常超过 64 个实体，按 S05 §8 风险项改成 2m 格。

### 6.5 计划文本纠正与已声明偏差（S05）

1. §7 的 `static_assert(sizeof(PoseHistory)) == 7680` 在上一条的冻结签名下不可实现 → 断言改成 `sizeof(PoseHistory::slots) == 7680`（见 §6.3）。
2. §5.1/§5.2/§9 的行内草图写 `{ok, id}` / `ok = false`，与工程约定 §6「布尔用 `is`/`has`/`can` 前缀」冲突 → 实现取名 `SpawnResult::isOk`、`SampledPose::isOk`（语义与取值同草图）；失败原因另立 `SpawnFailure{kNone, kEntityPoolExhausted}`，取值名沿用 §5.2。
3. §5.1 未定义 `Event` 的条目字段 → 本份只落 S03 §5.4 的条目头（`eventId` u32 + `type` u8），类型载荷留给 S06 起追加（容量 256 与每 tick 清零语义不变）。
4. §5.1「线上单帧事件数由 `u8 eventCount` 编码（硬上限 255）」与 ADR-009 / S03 §5.4 的「单帧事件 ≤64，超出走 `EventChannel`」并列时易误读 → 两者关系写在 §6.1（256 是缓冲容量，64 是每帧发送预算）。
5. `recordPoseHistory(PoseHistory&, const World&)` 与 `buildSpatialGrid(World&)` 的实现放在 `world.cpp`（两个头文件只前置声明 `World`），避免头文件互相包含；签名与 §5.3/§5.4 一字不差。
6. 计划 §4/§7 的 `- [ ]` 复选框按 S01–S04 的既有约定**不勾选**（计划文本冻结、不回收写），完成情况以 §9 表格的实测行为准。
7. CONTEXT §2 的词条把 `stepWorld` 称作"纯函数入口"，而 §5.1 要求全部可变状态都住在 `World` 里、§5.6 又禁止热路径分配 → 实现取**原地推进 `void stepWorld(World&)`**（返回新世界会与零分配约束冲突）；`stepWorld` 这个名字/签名在本份计划里并未出现，**需裁决**的是 CONTEXT 用词（"纯"指"唯一入口 + 无外部副作用"，还是指函数式无副作用）。→ **S06 已裁决**：签名冻为 `bool stepWorld(World&, const Command*, uint32_t, uint32_t)`（原地推进 + 非法 dt 返回 false），CONTEXT 用词按"唯一入口 + 无外部副作用"理解，见 §7。

## 7. 模拟步进（S06 §5 冻结）

权威 tick 入口是 `sim/step.hpp` 的 `bool stepWorld(World&, const Command*, uint32_t commandCount, uint32_t dtMs)`：
`dtMs != 50` 时返回 `false` 且**世界不变**（校验在阶段 0 之前，禁止变长 dt）。`server/src/sim/step.cpp` 的 13 个阶段
与 S06 §5.1 逐条同序，阶段只允许在末尾追加：

| # | 阶段 | 状态 |
| --- | --- | --- |
| 0 | 头部：`++tick`、`timeMs += dtMs`、清事件缓冲与 `eventCursor` | 已实现 |
| 1 | `applyCommands`（按玩家 `EntityId` 升序，见 §7.1） | 已实现 |
| 2 | `collectPlayerIds`（升序、跳过 `idle`，写进调用方栈数组，不分配） | 已实现 |
| 3 | `buildSpatialGrid` | 复用 S05 |
| 4 / 5 | `updateAiIntents` / `applyAiIntents` | 空实现；签名按 S09 §9 冻结 |
| 6 | `applyKnockback` | 空实现；等 S08 |
| 7 | 逐实体 `integrateState(dtMs / 1000.0)` + `aliveMs += dtMs` | 已实现（救援夹取见 §7.5 第 3 条） |
| 8 | `collideStatic`（谷仓推离 → 栅栏夹取，见 §7.2） | 已实现 |
| 9 | 重建网格 + `separateEntities`，每趟分离后重跑 `collideStatic` | 已实现（1 趟） |
| 10 | `resolveCombat` | 空实现；签名按 S08 §9 冻结（`CombatContext*` 前置声明） |
| 11 | `resolveSheepAttacks` / `resolveEliteFire` / `advanceProjectiles` / `updateSheepKing` | 空实现；等 S08 / S09 |
| 12 | `updateWorldStats`（`stats.aliveSheep`） | 复用 S05 |
| 13 | `recordPoseHistory` | S05 §5.3 的"每 tick 末尾"，追加在末尾（见 §7.5 第 2 条） |

### 7.1 命令与玩家游标（§5.2）

`Command` 是**内存侧**命令，字段顺序 = S03 §5.2 的线上载荷顺序：
`{moveX, moveY, yaw, pitch, buttons, switchTo, seq, clientTick}`；线上 i8 轴除 127、u16 角度单位过 `radiansFromUnits`
才是这里的样子（状态与对拍一律 `double` 弧度）。`commands[k]` 属于**第 k 个升序玩家实体**（游标包含 `idle` 玩家，
否则后面的玩家会串位、拿到别人的命令），命令不够的玩家走 §5.2 的"缺命令 → 速度归零、位置不动"分支；
`collectPlayerIds` 另给下游一份**跳过 `idle`** 的升序列表 —— S08 §5 的"命令游标与阶段 1 一致"按此实现。

`applyCommandToState` 是 S06 §5.2 代码块的逐字落地（运算顺序禁止改写）：`yaw`/`pitch` 先写进状态，
方向向量只经 `quantizeAngle` + `sinUnits`/`cosUnits`（本份不新建任何角度表、不出现超越函数），
`magSq > 1.0` 才归一化，最后 `vel = 方向 × 速度`，`vel.y` 恒 0。

### 7.2 静态碰撞与分离（§5.5）

`collideStatic(state, arena, radius)`：**先**谷仓推离（`pos.y >= 5` 跳过；半径外扩后的 AABB 内部才处理，按
`min(pushLeft, pushRight) <= min(pushBack, pushForward)` 选 x 面或 z 面，并清零该轴速度），**后**边界夹取
（`limit = halfSize - thickness / 2 - radius = 39.35`，x、z 各自夹取并清零该轴速度）。

`separateEntities(world)`：单趟、同格 `i < j` 与东/南/东南/西南四格半邻域各一次（每个无序对恰好处理一次），
预筛 `(2 × 0.5)^2 = 1.0`，重叠时双方各推 `(minDistance - distance) / 2`（半径按 `kind` 取 §5.4 表，
`distance < 1e-6` 时按 `(+1, 0)` 推）。**推离总量守恒**：一次配对只搬动这两个实体，且位移等大反向。
投射物不入网格（S05 §6.4），所以分离只作用于玩家/羊/掉落物。

### 7.3 `localStep` 契约（§5.6，客户端预测复用）

`LocalStepResult localStep(World&, const Command*, uint32_t commandCount, uint32_t dtMs, EntityId)`：只推进一个玩家
一个 tick，等价于权威 tick 的子集"命令应用 → 积分 → 静态碰撞"，只消费 `commands[0]`；不读 `ai`/`spawn` 流、
不写事件池、不触碰其他实体与统计量（**不记姿态环**）；位姿写回实体并推进 `tick` / `timeMs`。
`dtMs != 50`、命令缓冲为空或 id 不是活动玩家 → `{isOk=false, 当前 tick, 当前 timeMs}` 且世界不变。
一致性由用例钉住：同一初始世界 + 同一命令序列下，240 tick 的 `pos.x/y/z` 与 `yaw` 在 `localStep` 与 `stepWorld`
之间 `bit_cast<uint64_t>` 全等（`step_local_step_equals_authority`）。

### 7.4 玩家与场地常量（§5.4）

`server/src/config/player.hpp` 集中玩家/实体/按钮位域/分离参数/固定步长；场地形状（80m、栅栏 3 × 0.5、
谷仓 [-4,4] × [0,5] × [-4,4]、4 个出生点）的**唯一真值仍在 `sim/arena.hpp`**（S05），
`config::kArenaConfig` 与 `config::kKindRadiusM` 只引用它并用 `static_assert` 锚定。

### 7.5 计划文本纠正与已声明偏差（S06）

1. §3 要求 `config/player.hpp` 抄 v1 `arena.ts`，但 S05 的 `sim/arena.hpp` 已是该表的唯一来源 → 本份**不复制数值**，只引用 + `static_assert` 锚点（两处真值迟早漂移，跨语言对拍时是致命的）。
2. §5.1 的阶段表没列 S05 §5.3 的"每 tick 末尾 `recordPoseHistory`"；不记的话 S11 的回滚命中永远拿不到历史 → 作为第 13 步**追加在末尾**（0–12 的顺序一字不差）。
3. §5.1 阶段 7 末尾的"对正在救援的玩家 `clampHorizontalSpeed(1.5)`"需要救援状态机，而救援态由 S08 定义（S03 §5.4 的 `kindFlags` 只有 `downed` 位，没有"正在救援"）→ 本份只落地 `clampHorizontalSpeed` 本身并有用例覆盖，调用点等 S08 接入。
4. §5.5 的谷仓推离没写速度语义，但 §6 第 5 条要求 400 tick 后 `vel.z == 0` → 推离时清零该轴速度（与边界夹取同一语义）。
5. §5.6 的 `struct LocalStepResult { bool ok; ... }` 与工程约定 §6 的布尔前缀规则冲突 → 字段名取 `isOk`（与 S05 的 `SpawnResult::isOk` 一致）。
6. §6 第 4/6 条的"`pos.z == 4.5` / 间距 `== 0.8`"只有一部分能逐位成立：实测步行 20 tick 与 `yaw = 90°` 的 `pos.x` 恰好是 `4.5`，冲刺 20 tick 累加到 `6.3000000000000025`、分离后间距 `0.80000000000000071` → 这两条用 `1e-12` 容差断言并把实测值打成证据行（`stepSprintZ=` / `stepSeparation=`）。运算顺序按 §5.2 逐字复刻，不做"凑整"优化。
7. §5.1 阶段 1 的"按玩家 EntityId 升序"与阶段 2 的"跳过 `idle`"并列时，命令游标归属易误读 → 阶段 1 的游标**包含 `idle` 玩家**，阶段 2 才过滤（见 §7.1）。
8. **待裁决**：S10 §5 的草图写 `stepWorld(world, commands, 50)`（3 参），与 S06 §9 冻结的 4 参签名不一致 → 本份以 S06 §9 为准（`commandCount` 显式传入，`applyCommands` 才拿得到"缺命令"分支）。需要回写 S10 那行文字。
9. §5.1 的 13 个空实现阶段签名一次冻结，能查到下游冻结的就逐字照抄：`updateAiIntents`（`(World&, uint32_t dtMs, const EntityId*, uint32_t, const SpatialGrid&)`）与 `applyAiIntents` 按 S09 §9，`resolveCombat` 按 S08 §9（`CombatContext*` 前置声明），`int resolveSheepAttacks(World&, const EntityId*, uint32_t)` / `int advanceProjectiles(World&, uint32_t dtMs, ...)` / `int updateKing(World&, Entity&, uint32_t dtMs)` 按 S09 §9（返回值 = 落地条数，本份恒 0）。S09 §9 只钉住 `int resolveEliteFire(...)` 与 `advanceProjectiles` 的前缀，本份就**只实现被钉住的部分**（`resolveEliteFire(World&)`、`advanceProjectiles(World&, uint32_t)`），不替下游猜参数。
10. 阶段 11 的 `updateKing(World&, Entity&, uint32_t)` 需要一个羊王实体，而羊王由 S09 创建 → 本份冻结签名但**不设调用点**（其余四个阶段都按冻结签名调用）。
11. §5.1 的阶段 7/8 没限定实体种类（只有阶段 2 明写「跳过 `idle`」）→ 本份让**全部活动实体**走积分与静态碰撞（投射物/掉落物的半径也在 §5.4 表里）；这带来一个 spec 未定义的行为：飞出场地或谷仓的投射物会被夹到边界而不是飞出去，若 S08/S09 要求「出界即回收」，需要在 S08/S09 里覆盖本行为（**需裁决**）。
10. §3 写"（注册进 `main_test.cpp`）"，但 S01 起 `ac_tests` 用 `tests/*.cpp` 的 `CONFIGURE_DEPENDS` glob、`main()` 只在 `main_test.cpp`（§1、§4.2 第 2 条）→ 本份照旧只新增 `server/tests/step_test.cpp`，不改任何清单、也不 `#include` 进 `main_test.cpp`。
12. 计划 §4/§7 的 `- [ ]` 复选框同样**不勾选**（S01–S05 既有约定），完成情况以 §9 表格的实测行为准。

## 8. 硬约束（来自 ADR-008 / ADR-009 / ADR-010）

1. C++20；**无第三方运行时库**——UDP 可靠性层、JSON 日志、测试断言框架全部自研（新增依赖需先写 ADR）。
2. 量化、字节序、包头与通道语义一律以 ADR-009 为准，服务端不得单方面扩展字段。
3. 模拟热路径只用 `+ - * / sqrt` 与整数运算；编译禁用 fast-math 与 `-march=native`（ADR-010），Release 固定 `-O2`、`-ffp-contract=off`、`-fno-fast-math`、`-Werror`。
4. 零外部素材：本目录不得出现任何二进制资源文件（`node tools/check-assets.mjs` 会拦）。

## 9. 当前状态

**S01–S06 已完成**：构建链、自研断言框架、结构化日志（S01）、确定性内核（S02）、二进制协议编解码（S03）、UDP 传输子层（S04：套接字缝、可靠性、分片、握手、心跳/宽限期、内存总线）、模拟数据层（S05：
`World` 字段表、实体表、姿态环、空间网格、80m×80m 场地常量，见 §6）与模拟步进内核（S06：命令应用、积分、静态碰撞、实体分离、`localStep` 预测子集，见 §7）就位；战斗、AI、房间、持久化由 S08 起的各份计划按"交付物"章节逐份创建，**不预先存在**。

本机实测（2026-09-24，Windows 11 + Windows PowerShell 5.1）：

| 命令 | 实测 |
| --- | --- |
| `g++ --version` | `g++.exe (x86_64-win32-seh-rev1, Built by MinGW-Builds project) 15.2.0` |
| `powershell -NoProfile -File server/build.ps1 -Config Release` | 退出码 0，末行 `[build] ok ac_server.exe`；`[build] toolchain: cmake 4.2 + ninja + C:\\Program Files\\mingw64\\bin\\g++.exe` |
| `server/build/ac_server.exe --version` | `ac_server 0.1.0 protocol=1 tick=50ms` |
| `server/build/ac_tests.exe` | 末行 `TESTS 136/136`，退出码 0（S01 的 18 条 + S02 的 28 条 + S03 的 40 条 + S04 的 24 条 + S05 的 26 条） |
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
| g++ 直编兜底（同上 + `-lws2_32`） | `ac_tests.exe` 兜底版跑出 `TESTS 136/136`（含真实 UDP 回环那条；**S05 时点值**，S06 后为 156/156，见下方 S06 行） |
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
| `--filter=world` / `--filter=entity` / `--filter=pose` / `--filter=grid` | 末行依次 `TESTS 6/6`、`TESTS 7/7`、`TESTS 5/5`、`TESTS 4/4`，退出码全 0 |
| `--filter=alloc` 的打印行 | `steadyStateAllocations=0`（另有 `spawnReleaseAllocations=0`、`neighborScanAllocations=0`、`tickLoopAllocations=0`；10 tick 预热 + 600 tick 稳态） |
| 实体表容量与复用（§6.2） | 第 1025 次分配返回 `kEntityPoolExhausted` 且 `highWater`/`activeCount`/`activeIds` 均未变；LIFO 复用槽字段整体回初值（`entity_pool_exhaustion_keeps_state`、`entity_reuse_is_lifo_and_resets_fields`） |
| 实体表 100k 次 spawn/despawn 压测 | `activeCount + freeCount == highWater` 恒成立、空闲栈无重复、`activeIds` 保持严格升序（`entity_churn_preserves_invariants`） |
| 姿态环容量（§6.3） | `sizeof(PoseRecord) = 48`、`sizeof(PoseHistory::slots) = 7680`；`ms = 0` 逐位等于最新槽位、`ms = 75` 取 0 与 4 的中点 2、`ms > 200` 拒绝回滚（`pose_*` 五条） |
| 空间网格（§6.4） | `cellStart[401]` + `cellItems[1024]`；投射物不入网格；越界坐标夹到边界格；每格内 EntityId 升序；全覆盖查询序列与手工扫描逐项一致（`grid_*` 四条） |
| `Get-ChildItem server/src/sim -Recurse -Include *.hpp,*.cpp \| Select-String -Pattern "new ","malloc","realloc","std::vector","std::string"` | 0 命中 |
| 模拟层依赖（§7 DoD） | `server/src/sim/**` 只 `#include` 标准库与 `core/**`（无 `net/`、`ai/`） |
| `--filter=world` 的打印行 | `worldBytes=115832 entityBytes=96 poseBytes=7688 gridBytes=3652 eventBytes=8`（容量取证，README §6 引用本行） |
| 场地点位（DoD §7） | 4 个出生点与 12 个生成点坐标逐位硬编码比对、环半径 38 ± 0.01、四种 kind 的半径/高度逐值比对（`world_create_initializes_defaults`） |
| 计划偏差清单 | §6.5 的七条（`sizeof(PoseHistory)` 断言位置、`isOk` 命名、`Event` 收敛、255/64 澄清、实现落地位置、复选框不勾选、`stepWorld` 语义待裁决） |
| `server/build/ac_server.exe --selftest-log \| ConvertFrom-Json` | 解析成功，键序 `ts,level,evt,version,protocol,tickMs` |
| g++ 直编兜底（第三条命令 + 同开关 `-I server/tests`；S03 起还要加 `server/src/net/codec.cpp`） | `ac_server.exe` 与 `ac_tests.exe` 均编译成功、`--version` 与 cmake 分支一致，且兜底版 `ac_tests.exe` 末行同为 `TESTS 86/86`（**S03 时点值**；走相对路径 fixture，需在仓库根运行） |
| `Select-String -Path server/CMakeLists.txt,server/build.ps1 -Pattern 'ffast-math','-march=native','-mfma'` | 0 命中 |
| `Get-ChildItem server/src/core -Recurse -Include *.hpp,*.cpp \| Select-String -Pattern "std::(sin\|cos\|tan\|atan2\|asin\|exp\|log\|pow\|round\|lround\|llround)"` | 0 命中（S02 §7 的门禁；`Select-String server/src` 全目录同规则也是 0） |
| `node tools/check-docs.mjs` | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 50 个文档、154 条相对链接） |
| `node tools/check-assets.mjs` | 退出码 0：219 个受控文件零素材类扩展名（含 `client/` 侧 Unity 工程文件，非本份引入；该计数随客户端链增长，此处是本机时点值）；Unity 依赖 34 个全在白名单；`C++ 构建清单：未发现第三方依赖引入` |
| `server/build/ac_tests.exe`（S06 后） | 末行 `TESTS 156/156`，退出码 0（S01 18 + S02 28 + S03 40 + S04 24 + S05 26 + S06 20） |
| `--filter=step` | 末行 `TESTS 20/20`，退出码 0（S06 §6 第 3 条；用例名全含 `step`，未污染既有 20 组） |
| `--filter=step` 的打印行 | `stepWalkZ=4.5`、`stepSprintZ=6.3000000000000025`、`stepForwardX=4.5`、`stepSeparation=0.80000000000000071`、`stepWorldAllocations=0`（偏差见 §7.5 第 6 条） |
| S06 §6 第 4/5 条（移动与碰撞） | `yaw = 0`、`moveX = 1` 跑 20 tick：步行 `pos.z = 4.5`、冲刺 `pos.z = 6.3000000000000025`；从 `(0, 0, 30)` 向 -z 400 tick → `pos.z = 4.4` 且 `vel.z = 0`；从原点向 +x 400 tick → `pos.x = 39.35` 且 `vel.x = 0` |
| S06 §6 第 6 条（分离与预测一致） | 两玩家相距 0.5m → 0.8m（实测 `0.80000000000000071`，中点守恒）；对称半推（羊 0.2m → 1.0m）；零距离按 `(+1, 0)` 各推一半；`vector(0.08) + pickup(0.35)` 分离到 0.85 |
| 240 tick 预测一致性（§5.6） | 同一命令序列下 `localStep` 与 `stepWorld` 的 `pos.x/y/z`、`yaw` 位型全等（`step_local_step_equals_authority`，每 tick 4 次 `bit_cast` 比对） |
| 稳态零分配（§6 第 6 条） | 64 玩家 + 64 羊、10 tick 预热后连跑 10000 次 `stepWorld`：`stepWorldAllocations=0` |
| 21 组筛选门禁 | `size/math/trig/rng/quantize/codec/hex/fuzz/wire/match/transport/reliability/fragment/grace/memory/world/entity/pose/grid/alloc/step` 末行依次 `4/4`、`8/8`、`4/4`、`6/6`、`10/10`、`14/14`、`10/10`、`3/3`、`3/3`、`6/6`、`8/8`、`5/5`、`4/4`、`4/4`、`2/2`、`6/6`、`7/7`、`5/5`、`4/4`、`4/4`、`20/20`（既有冻结计数无一变动） |
| `Get-ChildItem server/src/sim -Recurse -Include *.cpp \| Select-String -Pattern "std::sin","std::cos","std::atan2"` | 0 命中（§7 DoD；`std::sqrt` 属 ADR-010 允许集） |
| `Select-String server/src/sim` 扫描 `\bnew\b` / `malloc` / `realloc` / `std::vector` / `std::string` | 0 命中（热路径零堆分配由 `alloc_test.cpp` 的计数版全局 `operator new` 断言） |
| `--filter=world` 的打印行（S06 后） | `worldBytes=115840 entityBytes=96 poseBytes=7688 gridBytes=3652 eventBytes=8`（`timeMs` u32 + `eventCursor` u16 + 2 填充 = +8） |
| g++ 直编兜底（新增 `server/src/sim/*.cpp`） | `TESTS 156/156`，退出码 0（与 cmake 分支同数） |
| 计划偏差清单（S06） | §7.5 的十二条（含 2 条**待裁决**：S10 §5 的 3 参草图、投射物是否参与静态碰撞） |

已知环境边界（不是仓库缺陷）：CMake 在配置阶段用管道捕获编译器输出，受限沙箱（含 workspace-write）会卡在 `Detecting CXX compiler ABI info`；需要完整文件访问才能跑通 cmake 分支与 `ctest`。g++ 直编兜底不受影响。
