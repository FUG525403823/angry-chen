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
- `ac_tests` 通过编译期宏 `AC_FIXTURE_DIR` 拿 fixture 绝对路径（CMake 注入；直编兜底时回退到相对路径 `server/tests/fixtures`，需在仓库根运行）。S07 起另有 `AC_EVIDENCE_FIXTURE_DIR` 指向仓库根的 `docs/evidence/fixtures`（跨语言对拍向量，不在 `server/` 下），兜底同样回退到仓库根相对路径。

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
8. **遗留项（本份未动）**：S02 的 `rng_ai_stream_bits` 含 `ai`，会让 S09 §6 的 `--filter=ai` 从 `TESTS 22/22` 变 23 条——名字本身没错（它就是 ai 流），**S09 立项时要先改名或改门禁**（S09 处置：不改名，改门禁 —— 见 §10.1-8）；另 S09 §7 第 9 条写的 `--filter=fixture` 在本套测试里是 `TESTS 0/0`（疑为 `--filter=codec` 之误，codec 恰为 14/14），S09 落地前需澄清。
9. **`udp_socket_loopback_roundtrip` 不在 §6 的五组内**：那五组按固定条数（8/5/4/4/2）校验，套接字缝的真实回环收发单独一条用例覆盖（真 UDP、非阻塞、`poll` 超时）。POSIX 分支本机（Windows）跑不到，只在 WinSock2 分支上验证过。

## 6. 模拟数据布局（S05 §5 冻结）

`server/src/sim/` 是纯数据层（只依赖标准库与 `core/**`，不引 `net/**`；S09 起 `entity_table.hpp` 为承载 §9 冻结的 `Entity::knock`/`Entity::ai` 而包含两份**纯数据**头 `combat/knockback.hpp`、`ai/sheep_state.hpp`，二者自身都不引 `sim/**`，因此不构成包含环 —— 见 §10.1-13），热路径零堆分配：
`World` 由 `createWorld(seed)` 一次性定长预分配（实测 `sizeof(World) = 1010824` 字节 ≈ 987 KiB，S09 扩容后，容量界 <1 MiB），此后每 tick 只在已分配的数组上做计数与写入；`Entity` 960 字节、`PoseHistory` 7688 字节、`SpatialGrid` 3652 字节、`Event` 48 字节（取证行见 §12 表格）。

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
6. 计划 §4/§7 的 `- [ ]` 复选框按 S01–S04 的既有约定**不勾选**（计划文本冻结、不回收写），完成情况以 §12 表格的实测行为准。
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
| 4 / 5 | `updateAiIntents` / `applyAiIntents` | 已实现（S09，`ai/sheep_brain.cpp`；两趟意图表按 §9 冻结签名） |
| 6 | `applyKnockback` | 已实现（S09，`combat/knockback.cpp`；S06 的占位签名补上 `dtMs`，与 v1 `sim.ts:71` 同序） |
| 7 | 逐实体 `integrateState(dtMs / 1000.0)` + `aliveMs += dtMs` | 已实现（救援夹取见 §7.5 第 3 条） |
| 8 | `collideStatic`（谷仓推离 → 栅栏夹取，见 §7.2） | 已实现 |
| 9 | 重建网格 + `separateEntities`，每趟分离后重跑 `collideStatic` | 已实现（1 趟） |
| 10 | `resolveCombat` | 已实现（S08，`combat/resolve.cpp`；签名不变） |
| 11 | `resolveSheepAttacks` / `resolveEliteFire` / `advanceProjectiles` / `updateKings` | 已实现（S09：前三者在 `ai/sheep_attack.cpp`（占位签名补上 `playerIds`/`playerCount`），`updateKings` 在 `ai/king_phases.cpp`） |
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
12. §3 写"（注册进 `main_test.cpp`）"，但 S01 起 `ac_tests` 用 `tests/*.cpp` 的 `CONFIGURE_DEPENDS` glob、`main()` 只在 `main_test.cpp`（§1、§4.2 第 2 条）→ 本份照旧只新增 `server/tests/step_test.cpp`，不改任何清单、也不 `#include` 进 `main_test.cpp`。
13. 计划 §4/§7 的 `- [ ]` 复选框同样**不勾选**（S01–S05 既有约定），完成情况以 §12 表格的实测行为准。

## 8. 跨语言对拍（S07 §5 冻结）

对拍向量的唯一真值是**冻结的 v1 实现**（`D:\projects\tmp\angry-chen-bak`，**全程只读**）：`tools/export-fixtures.mjs` 在可写派生副本（默认 `D:\projects\tmp\angry-chen-fixture`，缺失时从只读源复制、排除 `node_modules`/`.git`）上把 v1 的 `Math.sin/cos/atan2/asin` 换成 `docs/evidence/fixtures/trig-table.json` 的共享整数表，逐 tick 投影实体/事件/RNG 状态写成纯文本 JSON 入库；`server/tests/fixture_io.cpp` + `fixture_test.cpp` 用同一批向量逐位复现，**首个**差异以 `DIFF <fixture> tick=<n> field=<path> expected=<hex> actual=<hex>` 报出。

| 命令（工作目录 = 仓库根） | 作用 |
| --- | --- |
| `node tools/export-fixtures.mjs` | 写盘（自动派生副本；`--root`/`--out` 可覆盖） |
| `node tools/export-fixtures.mjs --check` | 幂等校验：与盘上逐字节比较，不写盘 |
| `node tools/export-fixtures.mjs --list` | 清单：`name bytes sha256`，并打印 `configHash` 覆盖组 |
| `server/build/ac_tests.exe --filter=fixture` | C++ 侧逐 tick 逐字段逐位复现（S07 §6） |

清单、世界初态约定、再生成与判读流程、以及尚未交付场景的所有者表都在 [`docs/evidence/fixtures/README.md`](../docs/evidence/fixtures/README.md)。

### 8.1 覆盖范围与链序冲突（**需裁决**）

§5.3 列了 14 个场景，其中 10 个（连射/霰弹/倒地救援、四种羊形 AI、羊王、波次导演、快照 round-trip、三流归属）依赖 **S08/S09/S12** 才存在的能力；§5.6 的 `configHash` 还覆盖「武器表与散布常量、战斗常数、羊形参数/AI 参数/状态转移表/命中盒、波次规则」这些 C++ 侧此刻并不存在的常量表 → 任何 14 场景的 `configHash` 都不可能通过。本份只交付**移动类 4 份**（§5.3 前 4 行）与完整管线（导出脚本、schema 读取、`configHash` 重算、逐位比较、DIFF 报告、自检），其余 10 份随各自拥有其行为的计划一起导出：**导出时刻就有消费者，才能验证一份向量到底在测什么**，避免把没人验证过的语义猜测冻成真值。

### 8.2 实测（`--filter=fixture` = 6/6）

- 4 份向量（`still-60t` 60 tick、`straight-line-240t` 240 tick、`barn-collision-400t` 400 tick、`fence-bounds-400t` 400 tick）**逐 tick 逐字段逐位一致**：1100 tick × 4 实体 × 9 字段 + 事件条数 + `rngState` 三流；`configHash = 41ffb3e9` 两侧相同。
- 自检（§6 DoD）：把最后一个 tick 的 `entities[0].pos.z` 改动 1 ULP → 报出 `DIFF fence-bounds-400t tick=400 field=entities[0].pos.z expected=0x4043acccccccccce actual=0x4043accccccccccd`；改 `configHash` → 报出 `field=configHash` 且 `comparedTicks=0`（**不跑 tick**）。
- 移动类向量的事件恒 0、三流抽取次数恒 0、`flags` 恒 0 → 与 S06 的"纯移动"内核语义一致（也说明这批向量没有偷偷消费 RNG）。

### 8.3 计划文本纠正与已声明偏差（S07，十五条）

1. 场景数 14 → 4（见 8.1，**需裁决**）；`--filter=fixture` 相应是 `TESTS 6/6`（4 份向量 + 2 条自检）而不是 §6 的 14/14。
2. §5.1 的示例把 `expected` 写成单对象，但 §5.4 要求"每个 tick 都必须有期望投影，禁止抽样跳过" → 本份把它放进每个 tick 项里（`ticks[i].expected.{entities,events,rngState}`），字段与含义不变；**ADR-010 §6 的 schema 描述需要回写**（ADR 文件与客户端链并行维护，本份不直接改）。
3. §5.1 的 `commands[]` 比 v1 `Command` 多一个 `id`：v1 用**槽位**语义（第 k 条命令给升序第 k 名玩家），写盘时记录 id 供 C++ 侧校验槽位映射。
4. 事件只比较**条数**：C++ 的 `World::Event` 目前只有 S05 冻结的条目头（`eventId`/`type`），载荷由 S08/S09 追加；本批向量 events 恒 0（条数比较等价于全等），载荷比较随 S08/S09 补齐。
5. `flags` 位表：C++ 侧（`fixture_io.hpp` 的 `kFlag*`）今天只有 bit5 `idle` 有来源（`entity.idle`）；导出侧（`export-fixtures.mjs` 的 `FLAGS`）按 v1 语义映射 bit0 `downed`/bit1 `rageMode`/bit2 `reloading`/bit5 `idle`，bit3 `charging`/bit4 `fading` 只出现在 v1 的快照位表（`net.ts`）→ 本批 `flags` 恒 0，各位的映射随 S08/S09/S12 补齐。
6. C++ 的生成走显式 `SpawnParams.hp/armor`（S05 §5.2），v1 的 `spawnEntity` 则从 `entity.baseStats[kind]` 取 → 对拍用例显式传 `kKindBaseStats`。这条差异本身是真实的跨语言语义差：**S08 起的每条生成路径都必须显式给基础属性**，否则初态就不一致（第一 tick 就会以 `entities[i].hp expected=100 actual=0` 失败）。
7. `config/player.hpp` 追加 `kMoveAxisLimit`/`kPitchLimitRad`/`kKindBaseStats`（§5.6 的 hash 要覆盖输入域与四类基础属性；羊形/投射物/掉落物的 hp/armor 抄 v1 `entity.ts`，S09 若另有数值须先改契约）。
8. `sim/arena.hpp` 第 10 个生成点的 z 由 `0.0` 改为 `-0.0`：v1 的字面量是 `{ x: -38, z: -0 }`，而 §5.4 规定 `-0.0` 与 `0.0` **不等** → `configHash`（`%.17g` 文本）会区分两者。**这是对拍机制抓到的第一处真实漂移**（此前只被 `== 0.0` 的数值断言掩盖）；**S05 §5.4 的表需要回写**（计划文本冻结、不回收写，本份只改代码并把纠正记在这里）。
9. 体积门冲突（**需裁决**）：§5.5/ADR-010 的体积门是"14 份 < 2 MB"，但按 §5.1 冻结的"每 tick 全量投影 + `%.17g`"写法，4 份移动向量已占 1 446 500 B（69%）。更麻烦的是 §6 的取证命令 `Get-ChildItem docs/evidence/fixtures -Recurse -File | Measure-Object Length -Sum` 是**目录口径**：实测 2 163 463 B（含 S02 的 `trig-table.json` 709 640 B 与本目录 `README.md`），即今天就已经超门限 66 311 B——而它数的对象（目录内所有文件）与 §5.5 说的"14 份 fixture"并不是一回事。要么把浮点格式放宽为最短往返表示（仍无损，但不再是 §5.1 的冻结格式），要么按里程碑分档抬高门限，或把口径从"目录内所有文件"改回"对拍向量文件"。
10. `--filter=fixture` 的汇总行：§5.4 的示例写作 `fixtures: N/14 passed, 1 failed`，本份按仓库冻结的输出契约交回框架自己的 `TESTS p/t`（`tiny_test.hpp`）+ 每用例一行 `fixture <name> ticks=<n> ...`——不另写一个只有这个测试文件才认识的汇总格式。
11. §4 写"注册进 `main_test.cpp`"：S01 起 `ac_tests` 用 `tests/*.cpp` 的 `CONFIGURE_DEPENDS` glob、`main()` 只在 `main_test.cpp` → 本份照旧只新增 `tests/fixture_io.{hpp,cpp}` 与 `tests/fixture_test.cpp`，不改任何清单；向量目录由新宏 `AC_EVIDENCE_FIXTURE_DIR` 注入（直编兜底回退到仓库根相对路径）。
12. 比较器在被冻结字段之外还查 4 个内部不变量：`commands[i].id`（校验槽位映射）、`entities.count`、`world.tick`、`world.timeMs`。它们失败时 `field=` 会是 schema 之外的路径——这是**刻意的**（§5.1 冻结的是字段与比较顺序，没有禁止多查），但确实是 §5.1 之外的额外形状。
13. §5.2 的"计数包装 + 重放推导"本批**没有被任何向量执行**：移动路径零 RNG 抽取（4 份都是 `draws=ai:0,spawn:0,fx:0`），首个真实消费者是 S08/S09 把 spawn/fx 流拉进来的场景。今天验证到的是"初值派生 + 每 tick 读 `World::rng.<stream>.a`"这一层（`mulberry32` 的抽取序列本身由 S02 的 `rng_*` 用例覆盖）。
14. §4 的"用共享整数表替换 v1 的 `Math.sin/cos/atan2/asin`"实现为**进程内替换**（`patchMath`，在 import v1 之前打补丁），不往任何磁盘写补丁 → §7 的"补丁只打在可写派生副本上"以更强的形式成立（两侧磁盘都不动）。副作用是替换面覆盖 v1 的全部调用点（不止移动路径），这正是对拍要的：C++ 侧同样只走整数表。
15. 导出脚本的基线纪律做成可判定：默认拒绝 `--root` 指向只读源；启动时逐文件比对派生副本与只读源（`packages/shared/src`，不一致直接失败，除非显式 `--allow-patched-copy`）；结束时比对只读源的 (文件数, 总字节, 最新 mtime) 指纹，被写入即报错；`--list` 缺文件由"打印 missing"改为**失败**；体积门移到写盘**之前**（门失败时不留下超限生成物）；CRC32C 与角度表常量改为 import `tools/lib/trig-table.mjs` 的唯一实现。

## 9. 战斗（S08 §5 冻结）

- 配置：`config/weapons.hpp`（三把枪整表 + 散布/后坐/弹道标量）、`config/combat.hpp`（命中部位、伤害与护甲吸收、怒气、救援、羊命中盒）。`configHashText` 自本批起多出七组（`weapons` / `weapon.rules` / `shot` / `damage` / `rage` / `revive` / `sheepHit`），C++ 与导出脚本逐字节一致，4 份向量已按新哈希重新导出。
- 武器状态机：`combat/weapon.{hpp,cpp}` 逐行照 v1 `combat/weapon.ts`（弹匣/备弹/换弹/切枪/首次射击间隔）；`isAuto` 对应 v1 的 `auto`（`auto` 是 C++ 关键字，仅此一处改名）。
- 弹道：`combat/raycast.{hpp,cpp}` + `combat/resolve.cpp` 的 `traceRay` 与 v1 同序（谷仓 AABB → 升序 `activeIds` → 玩家按 `poseHistory.found` 回滚、羊按局部盒），角度只走 ADR-009 整数表；slab 用显式状态结构，不再有 v1 的模块级全局。
- 结算：`resolveCombat` 四阶段（全实体 `updateWeapon` → 命令游标：切枪/换弹/怒气/开火 → 全实体 `updateRage` → `updateRevives`）；命中写 6 类事件，载荷是帧内米制坐标 + 伤害和；`eventId` 仍留 0（S12 分配）。
- 布局：`Entity` 96 → 232 B、`Event` 48 B、`World` 265 352 B（§6.1 表已同步）；战斗热路径零堆分配、不读 `rng.fx`。
- 证据：`--filter=combat` 45/45、`--filter=fixture` 6/6、全量 `TESTS 207/207`、`ctest` 1/1、1000 tick 射击 0 次分配。

### 9.1 计划文本纠正与已声明偏差（S08）

1. §5.2 把俯仰上限与输入域并列写 `kPitchLimitRad = 1.5533`：v1 `combat/resolve.ts:47` 的 `PITCH_LIMIT_RAD = 1.5533` 是**结算侧夹取**，输入域仍是 `Math.PI/2` → 本份取 `config::kAimPitchLimitRad = 1.5533`，输入夹取沿用 S06 的 `kPitchLimitRad`。
2. §6 的 `--filter=fixture` 写 14/14：S07 §8.3 已裁为 4 份向量（S08 后 6/6 = 4 向量 + 2 自检）。§5.7 的 3 份战斗向量（`rifle-burst-hit-120t` / `shotgun-spread-60t` / `downed-revive-140t`）需要「会移动的羊」→ 依赖 S09 的 AI 意图，**改挂 S09**（`docs/evidence/fixtures/README.md` 归属列已改）。
3. §5.1 的 `WeaponDef.auto` 在 C++ 里是 `isAuto`（`auto` 是关键字），字段序与语义不变。
4. `Event` 32 → 48 B、`Entity` 96 → 232 B → §6.1 的容量界从 `<128 KiB` 抬到 `<288 KiB`。
5. `SpawnParams.hp/armor` 是 double，且 `maxHp` 取 **kind 基础值**（v1 `world.ts:231`），只有 `hp` 被参数覆盖；3 参便捷重载自本批起按 v1 填 `hp/armor`（此前留 0，会让救援/怒气用例拿到 0 血实体）。
6. 玩家胶囊上「头部」只有顶端半球能达成（线段顶 1.3 + 半径 0.4 = 1.7，头部阈值 0.85×1.7 = 1.445）→ 用例改为瞄 1.55 打端盖，修正了「平射即爆头」的写法。
7. §5.6 的事件载荷用帧内米制坐标（与 v1 `sim/events.ts` 一致），不是量化后的线上坐标。

### 9.2 两轴评审的发现与处置（S08，Standards + Spec 并行评审）

**本次已修**：`Event`/`Entity` 尺寸与 `configHash` 换新导致的历史叙述 —— §6.2 第 5 条与 §6.1 行内仍写「`Entity` 96 字节 / `hp`·`maxHp`·`armor` 各 4 / `maxHp = hp`」「`Event` 只固定条目头」，均以本节的 232/48 B 与 §12 证据行为准（历史行不回收改写）。

**已声明但未做（已知缺口，登记在案，不隐藏）**：
1. **救援者限速未接**（计划 §5.7 的 `kReviverMaxSpeed = 1.5`）：v1 `sim.ts:109-115` 在阶段 1 对「按住 interact 且附近有倒地队友」的救援者调 `clampHorizontalSpeed(1.5)`；本份只在 `resolve.cpp` 判定里**排除**速度 > 1.5 的救援者，`step.cpp` 的 `applyCommands` 没有这个 clamp → 边走边按交互救不起人（与 v1 不一致）。README §7.5-3 的原计划就是「调用点等 S08 接入」，本批未接入，**顺延到 S09 第一件事**。→ **S09 已闭环**：`step.cpp` 阶段 1 现按 v1 `sim.ts:109-115` 调 `clampHorizontalSpeed(state, kReviveSpeedClampMps)`，用例 `step_clamps_reviver_speed_when_holding_interact` 钉住四档（限速 / 不按交互 / 队友未倒地 / 队友 3m 超距），见 §10.2。
2. **回滚通路无覆盖**（计划 §5.7 的 200ms）：`CombatContext` 在权威步进里恒为 `nullptr`（`step.cpp`），`resolve.cpp` 的回滚分支因此是死路；`combat_test.cpp` 也没有 `rewindMs = 0/100/200` 的用例。回滚的真正消费方是 S11 的回溯命中验证。
3. **§6 的三条场景只部分落地**：步枪「按住 10 tick = 6 发」无用例；霰弹「8 弹丸共 96 伤害」被弱化为 `hits ∈ [1,8]` 且 `damage ∈ [12·hits, 24·hits]`；「连射 20 发后 0.25」实测为 3 发，「每 tick −0.3°」因一步夹到 0 而不可观测（`kSpreadDecayPerSecondDeg ≥ 2` 即可过）。
4. **§6-4 的 160.5m 口径**：用例改为传 `maxDist = 4.0` 打 7.4m 目标，验的是入参上限而不是场地对角线之外的羊。
5. **§6-1 的计数**：计划写 `TESTS 26/26`，实测 `TESTS 45/45`（多出的用例是本次修的谷仓遮挡/胶囊端盖/零分配等边界）。
6. **弹丸步长与抖动盐无守卫**：`13/29/0x9e3/0x51f` 是 v1 `resolve.ts` 的内联字面量（未导出），导出脚本只能抄一份进 `shot=` 组 → v1 改这四个值对拍不会红；C++ 侧自身抄错仍被冻结的 `configHash` 拦住。用例 `combat_jitter_matches_v1_vectors` 传的是自己的字面量，不引用 `kJitterYawSalt`。
7. **判断项（未改，留待统一裁决）**：`bool downed` / `bool hit` / `bool interactHeld` 未用 `is`/`has` 前缀（工程约定 §6，S05 曾据此把 `ok` 改名 `isOk`）；`raycast.cpp` 用 `std::fabs`（ADR-010 §2 的允许项写「`abs` 用位运算实现」）；`s08` 是 `export-fixtures.mjs` 的模块级可变全局（S07 §8.3-14 曾以消除模块级全局为卖点）。

## 10. 羊群 AI 与波次导演（S09 §5 冻结）

- 配置：`config/sheep.hpp`（四羊形整表、`SHEEP_AI` 全字段、13 行状态转移表、局部常量、攻击档案）、`config/waves.hpp`（波次上限与间歇、人数缩放、每 tick 生成上限、出生点约束，以及 `waveBaseBudget`/`waveBudget`/`sheepSpeedMultiplier`/`firstWaveFor`/`isBossWave`/`kindAllowedAt`）。`configHashText` 自本批起多出七组（`sheep` / `sheep.ai` / `sheep.states` / `sheep.local` / `sheep.attack` / `waves` / `waves.scaling`），C++ 与导出脚本逐字节一致（`configHash = 19a978ea`），4 份向量已按新哈希重新导出。
- AI 模块（全部逐行照 v1 `packages/shared/src/ai/*.ts`）：`ai/steering.{hpp,cpp}`（`seek`/`arrive`/`normalize`/`separation`/`obstacleAvoid`）、`ai/flocking.{hpp,cpp}`（按 `(distanceSq, EntityId)` 有界插入的 12 邻居 + 分离/对齐/凝聚加权 + `0.5` 混合比）、`ai/targeting.{hpp,cpp}`（8 槽仇恨、每 tick `×0.98`、命中 `+20` 无上限、遮挡可见性、`score = aggro + 1/(1+d)` 与 `×1.5` 换目标阈值）、`ai/sheep_brain.{hpp,cpp}`（13 态转移表、四羊形行为分支、`updateAiIntents`/`applyAiIntents` 两趟结构）、`ai/sheep_attack.{hpp,cpp}`（撕咬 1.4+0.5+0.4、冲锋 0.5+0.4+0.15、精英问号弹、投射物推进与销毁）、`ai/king_phases.{hpp,cpp}`（`>0.66`/`>0.33` 三阶段 + 4 只咩咩兵召唤）。
- 波次：`waves/director.{hpp,cpp}` 逐行照 v1 `ai/director.ts`（预算平面数组按 `grunt→ram→elite→king` 的 `cursor` 消耗、每 tick ≤8 个、出生点随机起点 + 只收 ≥15m 的点且 ≤3 个、±1.5m 抖动、清波与第 10 波结算）。`DirectorState` 是外部状态（§9 冻结签名），**未**接进 `stepWorld`（见 §10.1-3）。
- 接线：`sim/step.cpp` 阶段 4/5 调 `ai::updateAiIntents`/`applyAiIntents`，阶段 11 调 `resolveEliteFire` → `advanceProjectiles` → `updateKings`；`combat/knockback.{hpp,cpp}` 的 `applyKnockback` 落在**阶段 6**（紧跟阶段 4/5 的 AI 意图之后，与 v1 `sim.ts:69-71` 同序）。阶段 1 另按 v1 `sim.ts:109-115` 补上 S08 §9.2-1 的救援者限速。`Entity` 追加 `knock{}` 与 `ai{}`，尺寸 232 → **960 B**、`World` → **1 010 824 B**。
- 证据：`--filter=ai` 31/31（只算本批：`--filter=ai_` 30/30，见 §10.1-8）、`--filter=waves` 16/16、`--filter=fixture` 6/6、全量 `TESTS 253/253`、`ctest` 1/1、600 tick × 60 羊 + 4 玩家 0 次堆分配、同种子 600 tick 两次运行逐位一致、`fx` 流零抽取。详见 §12 表格。

### 10.1 计划文本纠正与已声明偏差（S09）

1. **§5.7 的朝向写法不 wrap**：v1 的补丁把 `Math.atan2(dx, dz)` 换成 `angleUnitsFromVector` + `radiansFromUnits`（导出脚本的 `patchMath` 就是 `Math.atan2 = (y, x) => trig.radiansFromUnits(trig.angleUnitsFromVector(y, x))`，`tools/export-fixtures.mjs:268`），而 `radiansFromUnits` **不做** `wrapAngle`（只有 `dequantizeAngle` 才 wrap）。羊的 `yaw` 因此落在 `[0, 2π)`；照 §5.7 的字面（用 `dequantizeAngle`）会在 `dx < 0` 时得到负角、与 v1 的位型不同。
2. **§5.5 的 840ms 羊王攻击冷却不存在**：v1 `kingPhases.ts:29` 导出的 `kingAttackCooldownMs` **从未被调用**（羊王撕咬走 `SHEEP_AI.attackCooldownMs = 1200`）。C++ 侧保留 `kingSpeedMultiplier`/`kingAttackCooldownMs` 作为 v1 模块面的 1:1 端口（`ai/king_phases.hpp`），但行为路径与 v1 一样**内联** `kingPhase3SpeedMultiplier`，840ms 分支不可达；用例只钉数值。
3. **导演未接 tick 循环**：§9 把 `DirectorState&` 冻结为外部状态，v1 也把它留在比赛控制器里；波次间歇与"下一波何时开"属于 S10 → 本批 `updateDirector` 只被用例调用，`stepWorld` 里没有它。连带后果：`docs/evidence/fixtures/README.md` §5 中属于 S09 的 5 份 AI 场景向量**未导出**（见下一条）。**需裁决**（已登记为 `docs/02-需求分析.md` 的 **OQ-11**）：§4 要求「接进 `step.cpp`」，§9 却把 `DirectorState` 冻结为外部状态，而 S06 §5.1 冻死的 `stepWorld(World&, const Command*, uint32_t, uint32_t)` 没有它的入口、§5.3 的 20s/5s 波间时钟也没有推进者 → 要么把 `DirectorState` 放进 `World` 并追加阶段（要动 S06/S09 的计划文本），要么由 S10 的比赛控制器在 `stepWorld` 之外持有并调用。
4. **5 份场景向量未导出**：`docs/evidence/fixtures/README.md` §5 把 `sheep-grunt-ai-600t`/`sheep-ram-charge-300t`/`sheep-elite-bolt-300t`/`sheep-king-phases-900t`/`wave-director-1to5-1200t` 挂在 S09；本批只交付了**共享 `configHash` 覆盖**（这才是 §7 DoD 的"AI 数值与 v1 基线漂移"守卫）。要跑羊群，导出器的场景集需要新增"生成羊 + 空命令"的驱动；`wave-director-1to5-1200t` 另外依赖上一条的接线。
5. **§7 的 grep 门禁按字面不可满足**：`Select-String` 默认**大小写不敏感**，而 `exp` 是 `constexpr` 的子串 → 该命令在 `server/src/ai`+`server/src/waves` 恒定命中 5 行（全是 `constexpr`）。等价的、可执行的门禁是 `Select-String -CaseSensitive -Pattern "unordered_map","std::sin","std::cos","atan2","asin","\bpow\b"` 与 `"\bexp\s*\("`，实测**均 0 命中**；注释里也不留 `atan2`/`asin` 字面量。
6. **死羊分支不可达**：v1 `sim.ts:149` 在阶段 4 对 `state === dead` 的羊 `continue`（不进意图表），阶段 5 的"1500ms 回收"分支因此永远进不去；S08 起击杀即 `despawnEntity`，`dead` 态也到不了。C++ 逐字端口这两处（含 stage 5 的死分支），用例断言的也是"只累加 `timerMs`、不改速度、不回收"。
7. **`--filter=fixture` 是 6/6 而不是计划写的 14/14**（与 S08 §9.1-2 同因：S07 §8.3 已把向量裁到 4 份）。
8. **`--filter=ai` 含 2 条既有污染**：`rng_ai_stream_bits`（名字里有 `ai_`）与 `fixture_tampered_hash_fails_before_ticks`（`f-ai-ls`）→ 31 = 本批 29 + 2；按 S05 §5.3-8 的遗留项**改门禁**收口：本批 29 条一律以 `ai_` 起头，`--filter=ai_` = 30 = 29 + `rng_ai_stream_bits`；两条既有用例名本身正确，故不改名。`--filter=waves` 无污染（16 = 16）。
9. **实体/世界扩容**：`Entity` 232 → 960 B（+击退状态 +羊 AI 状态；§10.2-3 删掉 `ai.sheepKind` 镜像后由 968 回落）、`World` → 1 010 824 B → §6 行内尺寸已同步为 S09 值，`world_test.cpp` 的容量界改成 <1 MiB；§6.2 第 5 条的"96 字节"是 S05 的历史推导，按既有约定**不回收改写**。
10. **羊的默认阵营是敌对方**：v1 `world.ts` 的 `spawnEntity` 会按 kind 填 `team`（羊 = 1），S05 的 3 参便捷重载此前留 0 → 羊与玩家同队、`FRIENDLY_FIRE=false` 下**打不掉血**。本批在 `config/player.hpp` 加 `kDefaultTeamByKind` 并让 `spawnEntity` 用它（用例 `ai_spawn_teams_make_sheep_hostile` 钉住）。
11. **`updateKing` 双签名**：S06 冻结的是 `int updateKing(World&, Entity&, uint32_t)`（声明在 `sim/step.hpp`），S09 §9 要求实现落在 `ai/` → 实现放 `ai/king_phases.cpp`，另有一个同名转发函数保持冻结签名可用。
12. **`kSheepAttackProfile` 复用 S08 的 `WeaponDef`**（`pellets = 1`、`falloffStartM = 1e9`、`headshotMultiplier = 1`）；AI 只用它的 `damage` 字段（8/22/14/30），散布/弹匣等字段不参与羊的攻击结算（v1 同样如此）。
13. **`sim/` 的两处纯数据包含**：`sim/entity_table.hpp` 为承载 §9 冻结的 `Entity::knock{}` 与 `Entity::ai{}` 而包含 `combat/knockback.hpp`、`ai/sheep_state.hpp`，与 §6 开头「不引 `ai/**`」的字面冲突；两份头都不引 `sim/**`（`ai/sheep_state.hpp` 只引 `config/sheep.hpp`，`knockback.hpp` 是纯 POD），因此**无包含环**，按局部豁免处理（§6 开头已回写）。
14. **命名按 CONTEXT / 工程约定 §6 回改**（两轴评审后）：①`questionBolt`（CONTEXT §1）—— `kBoltLifeMs`/`kBoltRadiusM`/`kBoltSpawnHeightM` → `kQuestionBolt*`、`spawnBolt` → `spawnQuestionBolt`；②布尔前缀（工程约定 §6）—— `DirectorState::finished`/`waveStartPending` → `isFinished`/`isWaveStartPending`，`DirectorTick::waveStarted`/`waveCleared`/`matchEnded` → `isWaveStart`/`isWaveClear`/`isMatchEnd`；③`kRescueSpeedClampMps` → `kReviveSpeedClampMps`（CONTEXT §1「救援 = `revive`」，S08 的旧名一并回改）。**保留**：`SHEEP_AI` 的 `eliteBoltRangeM`/`eliteBoltCooldownMs`/`boltSpeedMps` 是 §5.2 冻结的 v1 字段名（`configHashText` 的 `sheep.ai` 组逐字对齐），不改。
15. **删掉 `ai.sheepKind` 镜像字段**：v1 把羊形存在 `entity.ai.sheepKind`，而 S08 已把同一份状态放在 `Entity::sheepKind`（命中盒/攻击档案查询用）→ 本批让 `applySheepKind`/`sheepKindOf` 统一读写 `Entity::sheepKind`，删掉从不被读的 `ai.sheepKind`（评审的 Speculative Generality 项）；副作用是 `sizeof(Entity)` 968 → 960（§6 行内与 §12 已同步）。
16. **两趟意图表用模块级静态池**：§9 冻结的 `updateAiIntents(World&, …)` / `applyAiIntents(World&)` 之间要传 `SheepIntent[kMaxEntities]`，v1 `sim.ts` 用的也是模块级 `intentPool`/`intentIds` → C++ 同样落成 `ai/sheep_brain.cpp` 匿名命名空间里的定长静态池（约 34 KiB，无堆分配）。与 §5.1「全部可变状态都住在 `World` 里」的字面冲突：世界仍自洽（池每个 tick 全量重写、不跨 tick 读），但**同一进程内两个 `World` 不能交错步进**（与 v1 同限制）；若 S12 要求并行多世界，需把池搬进 `World`（追加字段，不动阶段顺序）。
17. **`absoluteValue` 合并**：`sheep_brain.cpp` 与 `sheep_attack.cpp` 各有一份本地绝对值（评审的 Duplicated Code 项）→ 合并到 `ai/steering.hpp` 的 `inline constexpr double absoluteValue(double)`（ADR-010 §2 的位运算口径，不引浮点库）。

### 10.2 两轴评审的发现与处置（S09，Standards + Spec 并行评审）

**本次已修（发现 → 处置）**：
1. **救援者限速未接**（S08 §9.2-1 的顺延项，Spec 轴问「是否有该做的没做」）：`step.cpp` 阶段 1 现按 v1 `sim.ts:109-115` 补上 `clampHorizontalSpeed(state, kReviveSpeedClampMps)`，条件是「非倒地 + 本 tick 命令按了交互 + 救援距离（2.0m）内有倒地队友」；用例 `step_clamps_reviver_speed_when_holding_interact` 钉四档（限速 1.5 / 不按交互 / 队友未倒地 / 队友 3m 超距）。
2. **命名与死状态**（Standards 轴）：见 §10.1-14、§10.1-15、§10.1-17（`questionBolt`、布尔前缀、`kReviveSpeedClampMps`、删 `ai.sheepKind`、合并 `absoluteValue`）。
3. **文档事实错误**：§10 原写「`applyKnockback` 接在阶段 4 之前」，实际是**阶段 6**（v1 `sim.ts:71`，紧跟 `applyAiIntents`）→ 已改；§7 的阶段表 4/5/6/10/11 四行原写「空实现」→ 已改为各脚本文件的实际归属。
4. **按 §5.3-8 收口门禁**：`--filter=ai_` = 30/30（本批 29 + `rng_ai_stream_bits`），`--filter=ai` = 31/31（再多一条 `f-ai-ls` 污染）；两条既有用例名本身正确，不改名。

**已声明但未做 / 判断项（登记在案，不隐藏）**：
1. **5 份 AI 场景向量未导出**（§10.1-4）：本批的跨语言守卫只有共享 `configHash`（数值表逐字节一致），AI 的**行为**没有位级对拍 —— 要补需给导出器加「生成羊 + 空命令」的场景驱动，属 S07/S09 之间的接口缺口。
2. **导演未进 tick**（§10.1-3）：已登记为 `docs/02-需求分析.md` 的 **OQ-11**。
3. **判断项（未改）**：`ai::damagePlayer`（`sheep_attack.cpp`）与 `combat::applyHit`（`resolve.cpp`）的「护甲 → 血量 → 倒地 → 事件」序列重复（v1 也是两份实现，逐行端口保留）；`FlockNeighbors`/`TargetState` 的固定容量与 `setSheepState(Entity&, int32_t)` 的 int 形参（Data Clumps / Primitive Obsession，二者都是 §9 冻结签名或 v1 形状）；`addNeighbor`、`SheepAiState::summoned` 只被测试或只被写（v1 模块面保留）。

## 11. 硬约束（来自 ADR-008 / ADR-009 / ADR-010）

1. C++20；**无第三方运行时库**——UDP 可靠性层、JSON 日志、测试断言框架全部自研（新增依赖需先写 ADR）。
2. 量化、字节序、包头与通道语义一律以 ADR-009 为准，服务端不得单方面扩展字段。
3. 模拟热路径只用 `+ - * / sqrt` 与整数运算；编译禁用 fast-math 与 `-march=native`（ADR-010），Release 固定 `-O2`、`-ffp-contract=off`、`-fno-fast-math`、`-Werror`。
4. 零外部素材：本目录不得出现任何二进制资源文件（`node tools/check-assets.mjs` 会拦）。

## 12. 当前状态

**S01–S09 已完成**：构建链、自研断言框架、结构化日志（S01）、确定性内核（S02）、二进制协议编解码（S03）、UDP 传输子层（S04：套接字缝、可靠性、分片、握手、心跳/宽限期、内存总线）、模拟数据层（S05：
`World` 字段表、实体表、姿态环、空间网格、80m×80m 场地常量，见 §6）、模拟步进内核（S06：命令应用、积分、静态碰撞、实体分离、`localStep` 预测子集，见 §7）与跨语言对拍（S07：v1 向量导出、C++ 逐位复现、`DIFF` 报告与自检，见 §8；**14 场景中的 10 个待 S08/S09/S12**）、羊群 AI 与波次导演（S09：四羊形行为与聚集、仇恨选择、冲锋/撕咬/问号弹、羊王三阶段、波次预算与出生点，见 §10）就位；房间与持久化由
S10 起的各份计划按"交付物"章节逐份创建，**不预先存在**。

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
| g++ 直编兜底（同上 + `-lws2_32`） | `ac_tests.exe` 兜底版跑出 `TESTS 136/136`（含真实 UDP 回环那条；**S05 时点值**，S06 后为 156/156、S07 后为 162/162，见下方各行） |
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
| 计划偏差清单（S06） | §7.5 的十三条（含 2 条**待裁决**：S10 §5 的 3 参草图、投射物是否参与静态碰撞） |
| `node tools/export-fixtures.mjs`（S07 后） | 从只读源自动派生副本后写出 4 份向量，总体积 1 446 500 B（体积门 2 097 152 B）；`configHash = 41ffb3e9` |
| `node tools/export-fixtures.mjs --check` / `--list` | `check ok：4/4 与盘上逐字节一致`（幂等）；`本批 4 份 = 1446500 B（体积门 2097152 B）`；`--out 目录合计 = 2163463 B / 6 个文件`（§6 的同一条命令，超出 66311 B）；`只读源未写入：packages/shared/src 62 文件 / 317509 B`；清单 4 行 `name bytes sha256`（见 `docs/evidence/fixtures/README.md` §1） |
| 导出侧自检（S07 §5.2/§6） | `crc32c('123456789') == 0xe3069283`；`%.17g` 11 条 **C 侧实测**向量（含 tie 例 `0x42f8ceb15abbf812 → 436416285491073.12`）；启动时派生副本与只读源逐文件一致 |
| `server/build/ac_tests.exe --filter=fixture` | 末行 `TESTS 6/6`，退出码 0：4 份向量逐 tick 逐字段**逐位**一致（1100 tick × 4 实体 × 9 字段 + 事件条数 + 三流 RNG 状态） |
| `--filter=fixture` 的自检（§6） | 改最后 tick 的 `entities[0].pos.z` 一位 → `DIFF fence-bounds-400t tick=400 field=entities[0].pos.z expected=0x4043acccccccccce actual=0x4043accccccccccd`；改 `configHash` → `field=configHash` 且 `comparedTicks=0`（不跑 tick） |
| `server/build/ac_tests.exe`（S07 后） | 末行 `TESTS 162/162`，退出码 0（S01 18 + S02 28 + S03 40 + S04 24 + S05 26 + S06 20 + S07 6） |
| 22 组 `--filter` 计数（S07 后） | 与 S06 逐组同数（size 4/4、math 8/8、trig 4/4、rng 6/6、quantize 10/10、codec 14/14、hex 10/10、fuzz 3/3、wire 3/3、match 6/6、transport 8/8、reliability 5/5、fragment 4/4、grace 4/4、memory 2/2、world 6/6、entity 7/7、pose 5/5、grid 4/4、alloc 4/4、step 20/20）+ 新增 `fixture 6/6` |
| g++ 直编兜底（S07 后，不加 `AC_EVIDENCE_FIXTURE_DIR`） | `TESTS 162/162`，退出码 0（对拍向量按仓库根相对路径读取） |
| 计划偏差清单（S07） | §8.3 的十五条（含 3 条**需裁决**：14 场景 → 4、事件载荷比较、体积门与 `%.17g` 不相容；这 3 条已登记为 `docs/02-需求分析.md` 的 OQ-07…OQ-09） |
| 战斗模块（S08） | `--filter=combat` 末行 `TESTS 45/45`，退出码 0（武器状态机 8、散布与后坐 6、伤害与护甲 6、射线 5、命中盒与轨迹 6、整步结算 12、零分配与确定性 8） |
| 全量回归（S08 后） | `server/build/ac_tests.exe` 末行 `TESTS 207/207`；`ctest` 1/1；g++ 直编兜底同数 |
| configHash 扩展（S08） | 新增 `weapons` / `weapon.rules` / `shot` / `damage` / `rage` / `revive` / `sheepHit` 七组；`node tools/export-fixtures.mjs` 重写 4 份向量（1 446 500 B）后 `check ok：4/4`，C++ `--filter=fixture` 6/6 |
| 结构体尺寸（S08） | `sizeof(Entity)=232`、`sizeof(Event)=48`、`sizeof(World)=265352`、`WeaponState=48`、`RageState=24`、`DownedState=40` |
| 计划偏差清单（S08） | §9.1 的七条（3 份战斗向量改挂 S09、`auto`→`isAuto`、`Event`/`Entity` 尺寸与容量界、`maxHp` 口径、玩家爆头几何、事件载荷口径） |
| 羊群 AI（S09） | `--filter=ai` 末行 `TESTS 31/31`，退出码 0（本批 29 条 + 既有 2 条含 `ai` 子串：`rng_ai_stream_bits`、`fixture_tampered_hash_fails_before_ticks` 的 `f-ai-ls`）；只算本批的收口门禁 `--filter=ai_` = `TESTS 30/30`（§10.1-8） |
| 波次导演（S09） | `--filter=waves` 末行 `TESTS 16/16`，退出码 0（预算表、组队、节流、出生点距离、清波/结算、池满丢弃、同种子逐位一致、10 波打到结束） |
| 全量回归（S09 后） | `server/build/ac_tests.exe` 末行 `TESTS 253/253`（S01 18 + S02 28 + S03 40 + S04 24 + S05 26 + S06 21（含 S09 补的救援限速一条）+ S07 6 + S08 45 + S09 45）；`ctest --test-dir server/build --output-on-failure` → `100% tests passed, 0 tests failed out of 1` |
| 25 组 `--filter` 计数（S09 后） | size 4/4、math 8/8、trig 4/4、rng 6/6、quantize 10/10、codec 15/15、hex 10/10、fuzz 3/3、wire 3/3、match 9/9、transport 8/8、reliability 5/5、fragment 4/4、grace 4/4、memory 2/2、world 6/6、entity 7/7、pose 5/5、grid 4/4、alloc 5/5、step 24/25（+救援限速一条）、combat 45/45、fixture 6/6、ai 31/31（`ai_` 30/30）、waves 16/16（`codec`/`match`/`alloc` 的增量来自 S08，本批未动；AI 的 2 条污染见 §10.1-8） |
| AI 热路径零分配（§7 DoD） | `ai_swarm_tick_loop_is_heap_clean` 打印 `aiSwarmTicks=600 sheep=60 allocations=0`（4 玩家 + 60 羊，10 tick 预热后 600 次 `stepWorld`，计数版全局 `operator new`） |
| AI 确定性与 `fx` 隔离（§7 DoD） | 同种子 4 玩家 + 60 羊跑 600 tick 的两次运行：三流 RNG 内部状态、`activeIds`、事件数、`aliveSheep` 与全部实体的 `pos/vel/yaw/hp/armor/state/sheepKind/aliveMs/ai.*/knock.*` 位型逐位相等；`rng.fx.a` 全程不变（`ai_two_runs_same_seed_are_bit_identical`） |
| configHash 扩展（S09） | 新增 `sheep` / `sheep.ai` / `sheep.states` / `sheep.local` / `sheep.attack` / `waves` / `waves.scaling` 七组；`node tools/export-fixtures.mjs --allow-patched-copy` 重写 4 份向量（1 446 500 B，逐文件只动 `configHash` 行）后 `--check` 为 `check ok：4/4 与盘上逐字节一致`，C++ `--filter=fixture` 6/6，`configHash = 19a978ea` |
| 结构体尺寸（S09） | `sizeof(Entity) = 960`（S08 的 232 + 击退状态 + 羊 AI 状态）、`sizeof(World) = 1010824`（`< 1 MiB` 由 `world_test.cpp` 断言）、`sizeof(Event) = 48`、`sizeof(PoseHistory) = 7688`、`sizeof(SpatialGrid) = 3652`；`--filter=world` 打印行 `worldBytes=1010824 entityBytes=960 poseBytes=7688 gridBytes=3652 eventBytes=48`（§10.2-2 删 `ai.sheepKind` 后重测） |
| `server/src/ai` + `server/src/waves` 的禁用子串门禁（§7 DoD） | 计划原文的命令（`-Pattern "unordered_map\|std::sin\|std::cos\|atan2\|asin\|exp\|pow"`）恒定命中 5 行，全是 `constexpr` 里的 `exp`（`Select-String` 默认大小写不敏感，见 §10.1-5）；等价门禁 `Select-String -CaseSensitive -Pattern "unordered_map","std::sin","std::cos","atan2","asin","\bpow\b"` 与 `"\bexp\s*\("` 实测均 **0 命中** |
| `node tools/check-docs.mjs`（S09 后） | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 55 个文档、166 条相对链接） |
| `node tools/check-assets.mjs`（S09 后） | 退出码 0：285 个受控文件、二进制嗅探 285 个、零素材类扩展名；Unity 依赖 34 个全在白名单；`C++ 构建清单：未发现第三方依赖引入` |
| 计划偏差清单（S09） | §10.1 的十七条（§5.7 朝向不 wrap、840ms 是 v1 死代码、导演未接 tick（OQ-11）、5 份 AI 向量未导出、grep 门禁按字面不可满足、死羊分支不可达、`fixture` 6/6 而非 14/14、`ai` 的 2 条污染与 `ai_` 收口、`Entity`/`World` 扩容、羊的默认阵营、`updateKing` 双签名、羊攻击档案复用 `WeaponDef`、`sim/` 的两处纯数据包含、命名回改（`questionBolt`/布尔前缀/`kReviveSpeedClampMps`）、删 `ai.sheepKind`、两趟意图表的模块级静态池、`absoluteValue` 合并） |
| 救援者限速接回（S09，闭环 S08 §9.2-1） | `step_clamps_reviver_speed_when_holding_interact` 四档：按住交互 + 队友 1m → 速度 1.5；不按交互 / 队友未倒地 / 队友 3m 超距 → 4.5（`--filter=step` 末行 `TESTS 25/25`，全量 `TESTS 253/253`） |
| 两轴评审（S09，Standards + Spec 并行） | §10.2 记录：已修 4 类（救援限速、命名与死状态、`applyKnockback` 阶段号与 §7 阶段表的文档事实错误、门禁收口）；未做/判断项 3 条（AI 行为无位级对拍、导演未进 tick 挂 OQ-11、`damagePlayer` 重复与冻结签名形状） |

已知环境边界（不是仓库缺陷）：CMake 在配置阶段用管道捕获编译器输出，受限沙箱（含 workspace-write）会卡在 `Detecting CXX compiler ABI info`；需要完整文件访问才能跑通 cmake 分支与 `ctest`。g++ 直编兜底不受影响。
