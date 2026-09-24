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
# 源文件清单随步骤增长：除 main.cpp 外全部编进来，避免逐个手抄（S13 起 main.cpp 依赖 version/metrics/persist）。
$src = Get-ChildItem -Recurse server/src -Filter *.cpp | Where-Object { $_.Name -ne 'main.cpp' } | ForEach-Object FullName
g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror -Iserver/src $src server/src/main.cpp -o server/build/ac_server.exe
```

> 该行只编 `ac_server.exe`（与 S01 §5.3 字面量一致）。测试程序要**另起一条命令**，不要把 `server/src/main.cpp` 带进去（双 `main()` 链接失败）：

```powershell
$src = Get-ChildItem -Recurse server/src -Filter *.cpp | Where-Object { $_.Name -ne 'main.cpp' } | ForEach-Object FullName
g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror -Iserver/src -Iserver/tests -o server/build/ac_tests_fallback.exe server/tests/*.cpp $src -lws2_32
```

> 兜底版 fixture 走相对路径（见 §4），必须在仓库根运行；S04 起需要 `-lws2_32`（WinSock2）；S13 起 `main.cpp` 不再自足（还要 metrics/persist/http/report 等编译单元），所以两条命令都用 `Get-ChildItem` 收全 `server/src/**/*.cpp`，不要手抄子目录清单（S08–S12 的手抄清单已缺 ai/combat/waves/room/security/replication，实测链接失败，见 §14.2）。

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
`World` 由 `createWorld(seed)` 一次性定长预分配（实测 `sizeof(World) = 1010824` 字节 ≈ 987 KiB，S09 扩容后，容量界 <1 MiB），此后每 tick 只在已分配的数组上做计数与写入；`Entity` 960 字节、`PoseHistory` 7688 字节、`SpatialGrid` 3652 字节、`Event` 48 字节（取证行见 §16 表格）。

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
6. 计划 §4/§7 的 `- [ ]` 复选框按 S01–S04 的既有约定**不勾选**（计划文本冻结、不回收写），完成情况以 §16 表格的实测行为准。
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
13. 计划 §4/§7 的 `- [ ]` 复选框同样**不勾选**（S01–S05 既有约定），完成情况以 §16 表格的实测行为准。

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
- 证据：`--filter=ai` 31/31（只算本批：`--filter=ai_` 30/30，见 §10.1-8）、`--filter=waves` 16/16、`--filter=fixture` 6/6、全量 `TESTS 253/253`、`ctest` 1/1、600 tick × 60 羊 + 4 玩家 0 次堆分配、同种子 600 tick 两次运行逐位一致、`fx` 流零抽取。详见 §16 表格。

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

## 11. 房间、会话与对局流程（S10 §5 冻结）

**交付物**（`server/src/room/` 六个模块 + 1 份用例；职责按计划 §3 的文件表）：

| 文件 | 职责 |
|---|---|
| `phase.{hpp,cpp}` | `MatchPhase{kLobby,kLoading,kPlaying,kIntermission,kEnded}`（码 0–4）、名字表、`canMatchTransition`（5×5 位掩码表：7 组合法 / 18 组非法，含 5 组同态）与 `applyMatchTransition`（非法转移记 `counters.deniedTransitions` 并返回 false；**同态按 v1 幂等返回 true**，见 §11.1-2） |
| `session.{hpp,cpp}` | `Session{id,pid,name[13],nameBytes,token,ready,weapon,weaponApplied,kills,roomCode[5],joinedAtMs,disconnectedAtMs,command}`；`setSessionName`（v1 `sanitizeName` 逐位端口：控制字符与 `<>&"'` 剔除 → 首尾空白裁剪 → 允许集 `[0-9A-Za-z_]` + CJK 3400–4DBF/4E00–9FFF/F900–FAFF → 1–12 字节，越界整名拒绝） |
| `stats.{hpp,cpp}` | `PlayerStats`/`PlayerRecord`（`kMaxPlayersPerRoom = 4`）/`MatchCounters`/`PlayerResult`/`MatchResultRecord`；§5.6 的事件→统计五映射、`accuracyOf`/`survivalMsOf`、`matchId = roomCode + '-' + startedAtMs` |
| `match_controller.{hpp,cpp}` | 时长常量（`kLoadingMs=1500`、`kIntermissionMs=20000`、`kIntermissionSkipMinMs=5000`、`kMatchStateIntervalMs=1000`、`kGracePeriodMs=30000`、`kReconnectMinHpRatio=0.5`、`kEmptyRoomReclaimMs=60000`、`kTickCatchUpLimit=5`）；`tryStartMatch`（host → 已结束则重置 → lobby → 全员 ready）、loading 倒计时 → `wave=1` + 全体补给、波间到期或「已过 5000ms 且全员 ready」跳过、清波、结束判定（无已连接玩家 / 全员倒地 → 羊群；第 10 波清空 → 玩家）、`buildMatchResult`、§5.8 的 MatchState 组装与补发判定 |
| `room.{hpp,cpp}` | `Room{code,world,seed,sessions[4],capacity,commands[4],match,director,phase,wave,intermissionMs,matchStateTimerMs,emptySinceMs,lastUpdateMs,stateSignature,matchState,accumulatorMs,unicastCount,immediateCount}`；`RoomDeps{user,sendMatchState,replicate}` 三个缝；`updateRoom` 按 §5.7 六步顺序（超时 → 阶段推进 → **仅 playing** 装配命令并 `stepWorld` → 结束判定 → `replicate` 快照缝 → MatchState 单播） |
| `rooms.{hpp,cpp}` | `RoomRegistry{rooms[kMaxRooms=64],count,codeRng,worldSeed,reclaimedCount,createFailureCount}`；房间码（31 字符表、长度 4、保留码 `0000`、冲突重试 32 次）、`createRoom`/`destroyRoom`/`findRoom`、`join`（**先判保留码再判格式**）、`reconnect`（**只按 token**）、`leave`/`disconnect`/`reclaimIdle`/`findGraceSession` |

**冻结契约要点**：阶段码与转移表、四个时长、房间码表与容量（每房 4 人 / 全服 64 房）、`Session` 字段、宽限期 30s（= 600 tick）、结算结构与 `matchId` 口径、§5.8 的单播节拍与立即补发触发条件。`hostId` **不上线**（客户端按 pid 最小者视为主机，§5.8）。

**S12 的三个接入点**：①`RoomDeps::sendMatchState(void*, const Room&, Session&, const net::MatchState&)` 收到的是**已组装好的** `net::MatchState` —— 编码、`type 10` 可靠单播与重传属 S12，房间只决定「哪些 tick、发给谁」并计数；②`RoomDeps::replicate(void*, Room&)` 是 §5.7-5「广播快照与事件」的缝（今天没有 World→`SnapshotFrame` 投影器）；③`Session::token` 由握手层分配（房间只按 token 匹配宽限期会话，新加入一律清掉连线带来的令牌 —— v1 的 `O08`）。

**运行命令（可直接复制）**：

```powershell
powershell -NoProfile -File server/build.ps1 -Config Release
server/build/ac_tests.exe --filter=match        # TESTS 53/53（本批 44 + S03 的 9 条含 match 的 codec 用例）
server/build/ac_tests.exe --filter=match_       # TESTS 42/42（只算 match_* 前缀；仍含 S03 的 7 条）
server/build/ac_tests.exe --filter=matchstate   # TESTS 9/9（§5.8 往返、节拍与立即补发）
server/build/ac_tests.exe --filter=room         # TESTS 8/8（注册表与三个缝）
server/build/ac_tests.exe                       # TESTS 297/297
```

用例只用 `server/src/net/memory_transport.hpp` 的内存适配器驱动，**不监听任何真实端口**。

### 11.1 计划文本纠正与已声明偏差（S10）

1. **§6-2 的 `--filter=match` 计数口径**：`--filter` 是**全局子串**匹配，53/53 = 本批 44（`match_*` 35 + `matchstate_*` 9）+ S03 的 9 条含 `match` 的 codec 用例（其中 7 条以 `match_` 开头）；计划写的 `TESTS 26/26` 与实际不符，DoD 的「≥26」由本批 44 条独立满足，`--filter=match_` = 42/42。
2. **§5.1「同态也返回 false」与 v1 不符（文档事实错误）**：v1 `match/controller.ts:111` 是 `if (room.phase === next) return true;`（幂等成功、不记警告），本批逐字端口；`canMatchTransition` 表里 5 组同态仍是非法（用例钉 7 合法 / 18 非法），但 `applyMatchTransition(same)` 返回 true。
3. **§5.2 的「`elapsedMs` = 真实毫秒增量」改为固定 50ms 切片**：房间用 `accumulatorMs` 把两次 `updateRoom` 的真实间隔切成整数个 50ms tick（每房每 tick 最多追 `kTickCatchUpLimit = 5` 片，余量留给下一次），`updateMatch` 每片收 `kStepDtMs`。收益：阶段时钟与 `stepWorld` 的 dt 同源、可复现；代价：阶段时钟落后真实时钟 < 50ms，网络间隔异常时补片有上限（v1 用真实 elapsed 推阶段、固定 dt 推世界，两者会漂移）。
4. **`hostId` 每次成员变化后按最小 pid 重算**：v1 只在建房时设一次，房主离开后房间再也不会开局（死锁）；§5.8 已明确「客户端把 pid 最小者视为主机」→ 服务端的 `hostId` 与客户端视图保持一致。
5. **世界只在 `playing` 步进**（§5.7-3 就是这么写的；v1 每个 tick 都步进）：`lobby/loading/intermission/ended` 阶段 `world.tick` 不增长，`commands` 只在 `playing` 装配与消费。
6. **`matchStateTimerMs` 每 tick 加 50ms**（v1 每次 `updateRoom` 调用加真实 elapsed）：语义差异同第 3 条。
7. **量化口径**：`hpRatio`/`reviveRatio255` 走 `ac::quantizeRatio`（255 档，ADR-009），`reloadLeft10Ms`/`rageLeft100Ms` 按 §5.8 的「**向下取整**」实现（v1 用 `round`），`rage`（0–100）沿用 v1 的 `round(ratio*100)`。
8. **§5.6 的 `matchEnded.flags = durationMs` 无法表示**：S05 冻结的 `sim::Event::flags` 是 **u8**，毫秒放不下 → 时长只存在于 `MatchRuntime.startedAtMs/endedAtMs`（`buildMatchResult` 现算），事件里 `flags = 0`。要让线上事件带时长需先改 S05 的事件结构（§11.2 未做项 3）。
9. **§5.7-5 的「广播快照与事件」只留缝**：`replicate` 每 tick 调一次，真正的编码/事件通道（`eventId` 幂等）属 S12；今天没有 World→`SnapshotFrame` 投影器。
10. **§5.8 的编码与可靠通道属 S12**：见本节 `sendMatchState` 说明；房间侧只保证「内容边界」（`count ≤ 4`、name 1–12 字节、量化档位）都可编码，`roomJoin` 对空昵称兜底为 `player`。
11. **立即补发的实现方式**：不在 mutator 里同步广播（v1 在 `roomJoin/roomLeave/roomDisconnect` 里直接发），而是在 `updateRoom` 里比对「本 tick 组装的签名 vs 上次已播签名」（phase/wave/波间档/人数/pid/ready/weapon/HP 档/弹药/怒气/倒地与救援档）→ 变化即补发。后果：mutation 与补发之间最多隔一次 `updateRoom`（0 tick 的 `updateRoom` 也会补发）；`roomSetReady` 例外 —— 它立刻发一次并同步刷新签名，避免下一次重复。
12. **签名是 32 位哈希**：碰撞只会漏一次补发，下一次 1000ms 节拍必然整帧覆盖；`hostId`/昵称/统计不进签名。
13. **§6-6 的「连续建房 200 次」受 `kMaxRooms = 64` 限制**：用例建 200 次、撞到 64 上限就回收一间，断言「长度 4 / 全在 31 字符表内 / 不为 `0000` / 同时并存的房间码互不重复」；`createFailureCount` 只统计「32 次冲突重试仍失败」，容量满不计入。
14. **`Session::kills` 是 v1 遗留字段**：v1 `room.ts` 也只写 0 / 照抄、从不累加 → 线上 MatchState 的 `kills` 恒 0（v1 同），真正的击杀数在 `PlayerStats::kills`（结算用）。要让 HUD 显示击杀得先改 ADR-009 的字段来源（§11.2 未做项 4）。
15. **昵称剔除集的口径**：v1 只剔除 `<>&"'`，剩下的 `/` 不在允许集 → `<b>alpha</b>` 净化后是 `balpha/b`，**整名被拒**（不是 `balphab`）；用例按此钉住。
16. **`tiny_test.hpp` 的用例容量 256 → 512**：`kMaxCases` 是框架容量上限，超限会**静默丢用例**（S10 新增 44 条后 S03/S09 的用例被挤出并只在 stderr 打警告）；抬到 512 后 `AC_TEST` 宏契约与「每例一行 + 末行 TESTS x/y」输出契约不变。
17. **§6-13 的 `--filter=fixture` 是 6/6 而不是 `TESTS 14/14`**（同 S08 §9.1-2 / S09 §10.1-7：S07 §8.3 已把向量裁到 4 份 + 2 条自检）。
18. **§3 交付物表里的「注册进 `main_test.cpp`」**：用例由 `AC_TEST` + CMake glob 自动注册，`main_test.cpp` 无需改动（S01 起的既有约定）。
19. **`pickRoomCode` 的保留码分支是防御性的**：31 字符表里没有 `0`，生成的候选永不等于 `0000`；「先判保留码再判格式」的顺序体现在 `join`（`code == "0000"` → 新建房间，先于格式校验）。
20. **房间码随机源用 `ac::createRng(startMs, RngStream::kFx)`**（§5.3 的「注册表自带的非模拟实例」）：它与每个 `world` 内部的三条流是**互相独立的对象**，抽取不会触碰任何 `world`；只是初值与 0 号房世界种子的 fx 流相同（S09 起模拟从不抽 `fx`，无实际耦合）。每间房的 `worldSeed` 从启动毫秒起逐房 +1 派生。

### 11.2 两轴评审的发现与处置（S10，Standards + Spec 并行评审）

**本次已修（发现 → 处置）**：
1. **进 `playing` 的全体补给漏了「散布清零」**（Spec 轴，§5.2 明写「换弹与散布清零」）：`refillPlayers` 补 `weapon.spreadDeg = 0.0`；原用例只在「刚建号」的干净状态断言，等于空断言 → 改成进 `playing` 前把血/甲/三弹匣/备弹/换弹/散布/倒地/怒气全部弄脏再逐项验。
2. **结算用例同义反复**（Spec 轴）：`matchId` 期望值原用被测的 `writeMatchId` 拼 → 改成 `snprintf("%s-%llu", code, startedAtMs)` 独立拼接。
3. **`resetMatchForRestart` 有一处死写入**（Standards 轴）：`records[0] = PlayerRecord{}` 紧跟的循环已覆盖 0 号 → 删。
4. **空昵称会产出不可编码的 MatchState**（Spec 轴，§5.8 要求 name 为 1–12 字节）：`roomJoin` 加兜底（`nameBytes < kNameMinBytes` → 用 `player`），把「任何时刻组装的 MatchState 都可编码」变成不变量而不依赖上层。
5. **`Session::pid` 用 32 位顶替 `EntityId`**（Standards 轴，工程约定 §6 术语表）：改成 `uint16_t`（= `sim::EntityId` 的宽度，0 仍是「未分配」哨兵）。
6. **只声明没定义的死 API**（Standards 轴）：`session.hpp` 里的 `sanitizeNickname` 声明无定义（净化逻辑在 `setSessionName` 内，链接期才会炸）→ 删声明；S12 若需要独立净化入口再抽。
7. **只写不读的 `RoomRegistry::seed`**（Standards 轴，Speculative Generality）：`worldSeed` 已从同一启动毫秒派生 → 删字段。
8. **布尔前缀**（Standards 轴）：`allPlayersReady` → `areAllPlayersReady`；`ready`/`weaponApplied`/`leftMidMatch` 是 §5.4/§5.6 冻结字段的字面名，保留（计划文本优先）。
9. **框架静默丢用例**（Standards 轴）：`kMaxCases` 256 → 512（§11.1-16）。
10. **文档缺 S10 章节**（Spec 轴：README 仍写「S01–S09 已完成」、无偏差清单）：本次补 §11/§11.1/§11.2 与 §15 的实测行，并同步 `docs/02-需求分析.md` 的 OQ-11（导演接线已由房间 tick 闭环）。

**已声明但未做 / 判断项（登记在案，不隐藏）**：
1. **`--filter=fixture` 6/6 而非 14/14**（§11.1-17）：跨语言向量仍只有 4 份；房间/对局流程这一层 v1 也没有导出向量（§5.7 无对拍面），故本批不新增。
2. **阶段时钟用 50ms 切片而非真实 elapsed**（§11.1-3）：改成真实 elapsed 必须让 `updateMatch` 与 `stepWorld` 的 dt 脱钩，牺牲可复现性 → 留作后续判断项（若 S14 压测要求阶段时长按挂钟收敛，再引入「秒级校准」）。
3. **`matchEnded.flags = durationMs` 不可表示**（§11.1-8）：修它要动 S05 的 `Event` 布局（`flags` u8 → u16 会改 `sizeof(Event)=48` 与全部对拍向量），登记为后续裁决。
4. **S12 归属与 v1 遗留三项**（§11.1-9/10/14）：快照/事件广播的编码、MatchState 的可靠单播与重传、`Session::kills`（v1 也是恒 0）—— 本批只保留缝与字段。
5. **重复的扫描循环与两张同形 switch**（Standards 轴的 smell 判断题）：`rooms.cpp` 的「扫全部房间 × 会话找指针」四个循环（`findGraceSession`/`reconnect`/`leave`/`disconnect`）与 `room.cpp` 的三个摘除点形状相同；`startOutcomeName`/`joinOutcomeName` 同形。判定**保留**：每次定位顺带做不同副作用（摘除、计宽限超时、清房间码、释放名额），抽公共后参数比逻辑长；两张名字表各只有 3–4 个分支而 `phase.cpp` 已用表，表化收益低 —— 若 S12 再加第三张，一起表化。
6. **`{name[13], nameBytes}` 数据团与 `size_t/int` 形参**（Data Clumps / Primitive Obsession）：判定**保留**：它们是 §5.4/§5.6 冻结字段形状的直接投影（v1 同），重排会同时改动线上编码路径与结算结构。
7. **`playerEntityAt` 的非 const 重载用 `const_cast` 转发**（Standards 轴的 Minor）：同上，形参形状属冻结面；已在头文件注释里点明。

**两轴合计**：Standards 轴 9 条（已修 7 + 判断 2）、Spec 轴 4 条（已修 3 + 声明 1）与上表 7 条未做/判断项。Standards 轴最重的是「框架静默丢用例」（已修，用例数 256 → 512）；Spec 轴最重的是「全体补给漏散布清零」（已修，且原用例确实是空断言）。

## 12. 权威校验与硬纠正（S11 §5 冻结）

**交付物**（§3 的四个模块 + 三份用例；另加一份 S13 需要的计数注册表）：

| 文件 | 职责 |
|---|---|
| `security/validate.{hpp,cpp}` | 包长/载荷上限、客户端→服务器 opcode 白名单、tick 合法性（过期 / 未来）、§5 字段表夹取（NaN/±Inf 归零、`moveX/moveY` ±1、`yaw` 取模 [-π,π]、`pitch` ±π/2、`switchTo`∈{0,1,2}）、命令 `seq` 幂等窗口；非法输入只返回 `ValidateResult{ok, reason}` |
| `security/rate_limit.{hpp,cpp}` | 两套 1s 滑动窗口（命令 30/s 只丢弃；消息 60/s 打击，第 3 次 `Disconnect(reason = 6)`）与 join 失败节流（5 次/10s）；算法逐字端口 v1 `checkRateLimit` / `createJoinThrottle`（含「窗口内 0 条则清零打击」） |
| `security/pose_validation.{hpp,cpp}` | §5 冻结数值（0.36225 m / 5.52 m/s / 1.15 / 0.543375 m）与四态处置，含 v1 `isLegalPosition` 的栅栏 / 谷仓判定与「位置非法优先」 |
| `security/rewind.{hpp,cpp}` | `rewindMs(rttMs) = min(rttMs / 2, 200)`；复用 S05 `samplePoseAgo` 取回退姿态，越限（`rttMs / 2 > 200`）返回 `ok = false` 并按当下姿态判定（不回滚），`clamped` 只作诊断计数 |
| `metrics/counters.{hpp,cpp}` | **§3 未列**（见 §12.1-1）：定长计数注册表（名字表 + 9 个计数 + 取值口），零分配、无字符串拼接；S13 的 `metrics.cpp` 在其上做 Prometheus 渲染 |
| `tests/security_test.cpp` | 恶意输入矩阵 **12 类 × 3 变体 = 36 条**（用例名同时含 `security` 与 `malicious`）+ 窗口 / 节流 / 幂等 / 计数接线，共 51 条 |
| `tests/pose_validation_test.cpp` | 冻结数值、边界两侧、派生预算可解释、位置合法性优先级、垂直违规、同跑次 `pose_suspect ≥ hard_correct`，共 13 条（用例名前缀 `motion_authority_`，避免冲掉 S05 冻结的 `--filter=pose` 门禁） |
| `tests/rewind_test.cpp` | `min(rttMs/2, 200)` 边界、越限不回滚、空环 / 未知实体 / 环内过期、`ms = 0` 取当下姿态，共 9 条 |

**处置优先级（§5，逐字继承 v1 `applyPoseValidation`）**：位置非法 → `Rejected`（计 `ac_pose_rejected_total` + `ac_hard_correct_total`，回退到 tick 前位置）；垂直违规或超出 `(limit + 派生预算) × 1.5` → `Corrected`（计 `ac_hard_correct_total`）；位移超过 `limit + 派生预算` 但可解释性判定为「不可解释」时 → `Suspect`（只计 `ac_pose_suspect_total`，**不改姿态**）；否则 `Accept`。任一「速度/垂直」违规另计 `ac_speed_violations_total`。

**运行命令（可直接复制）**：

```powershell
server/build/ac_tests.exe --filter=security      # TESTS 51/51（含矩阵 36 条）
server/build/ac_tests.exe --filter=malicious     # TESTS 36/36（矩阵地板，§6-3 的计数门禁）
server/build/ac_tests.exe --filter=pose          # TESTS 5/5（S05 冻结门禁：本批用例名刻意不含 pose，见 §12.1-12）
server/build/ac_tests.exe --filter=motion_authority  # TESTS 13/13（本批姿态处置用例）
server/build/ac_tests.exe --filter=rewind        # TESTS 12/12（本批 9 + 矩阵 12a–12c）
server/build/ac_tests.exe --filter=fixture       # TESTS 6/6（逐位一致：硬纠正不进权威模拟路径）
1..20 | ForEach-Object { server/build/ac_tests.exe --filter=malicious > $null }   # 20 轮退出码全 0
```

### 12.1 计划文本纠正与已声明偏差（S11）

1. **§3 未列 `server/src/metrics/counters.{hpp,cpp}`**：§4-7 / §6-4 / §7 要求「5 个新计数已接线注册」，而 `server/src/metrics/` 在本步之前并不存在（S13 才建 `metrics/metrics.cpp`）→ 本批新建最小注册表（名字表 + 定长计数数组 + `counterValue` / `isCounterRegistered`），S13 的 Prometheus 渲染与 `/metrics` 出口直接在其上做。
2. **§6-3 的末行 `malicious cases=36 failures=0` 不可得**：S01 冻结的输出契约是「每例一行 `PASS/FAIL` + 末行 `TESTS x/y`」，退出码 = 失败用例数 → 实际末行是 `TESTS 36/36`（退出码 0）。矩阵地板的**计数语义**由用例数守住（§7 DoD 照旧满足）。
3. **§4-8 / §4-9 的写盘目标 `docs/evidence/gate-selfcheck.md` 不存在**（`docs/evidence/` 下只有 client-c01…c08 验收、env-bootstrap、plan-audit 与 fixtures）→ 按 S07–S10 的既有约定，本批的校验数值与对拍结论写进本文件 §12 与 §16 表格。
4. **§5 的「派生预算只由权威模拟产生」在 v2 没有生产方**：v1 的 `Entity` 有 `derivedMoveX/Z`（`sim.ts:213-216` 累计、`world.ts:238-239` 每 tick 清零），而 S05–S09 冻结的 v2 `Entity` 没有这两个字段（全仓 grep `derivedMove` 无命中）→ 本批按 v1 `validateAdvance(..., derivedBudgetM = 0)` 的形状把派生预算做成**入参**（`PoseSample.derivedBudgetM`，默认 0），未来由模拟侧累加后传入；「在 sim 里加派生位移累加器」登记为后续裁决（它会改 `sizeof(Entity)` 与跨语言对拍向量）。
5. **§8 风险表的「用构建目标的依赖方向拦截」在单一 `ac_core` 目标下无法表达**：`server/CMakeLists.txt` 只有 `ac_core`（`src/**.cpp` 一次 glob）+ `ac_server` + `ac_tests` → 本批以「源码约定 + 审查期 grep」替代（sim/ai/combat/waves 无一处 include `security/` 或 `metrics/`，见 §16 表格）；要硬拦截需先拆目标或加一次源文件扫描门禁。
6. **§9 的 `rewind_ms` 写成 `rewindMs`**：按仓库既有命名（`samplePoseAgo` / `horizontalLimitM`）与工程约定的 camelCase，语义一致（`min(rttMs / 2, 200)`，整数除法）。
7. **§5「重复 msgId（重传）」是两层去重**：线上重复包由 S04 的 `ackOnReceive(state, msgId)`（ADR-009 可靠扩展 `msgId` u32 + 32 位 ack 位图）去重；矩阵 8 针对的是**应用层幂等**（同一命令 `seq` 不得被应用两次），窗口 64 条（`kDedupWindow`）。
8. **§5「命令载荷上限 64 B」是上限而非实际长度**：ADR-009 §5.2 的命令载荷固定 14 B（包头 8 + 可靠扩展 12 = 34 B 起）→ `validatePayloadSize` 按 64 B 拒绝**超长帧**，不是把 14 B 当契约。
9. **矩阵 5 的「>60/s」落在消息窗口**：命令窗口（30/s）只丢弃、不产生打击（矩阵 4「不断开」），打击与 `Disconnect(reason = 6)` 由消息窗口（60/s）累计 3 次触发 —— 与 §8 风险表「命令与消息两套窗口分离计数」一致。
10. **矩阵 11 的「负 tick」在 v2 不可表示**：ADR-009 §5.2 的 `clientTick` 是 u32 → 「负」只能以 `0xFFFFFFFF` 出现，命中「未来 tick 丢弃」（用例 11b 照此钉住）。
11. **边界等值比较的浮点事实（实测）**：计算值 `kHorizontalLimitM = 0.36224999999999996` 而字面量 `0.36225 = 0.36225000000000002`；`kHardCorrectLimitM = 0.54337499999999994` 而字面量 `0.543375 = 0.54337500000000005`。因此冻结数值在 `pose_validation_frozen_values` 里以 **1e-12** 容差钉住，而「恰好等于边界」的判定用 **±1e-9 相对带**的两侧（等值比较会被 1 ulp 翻面 —— 这是浮点契约，不是实现偏差）。§7「六个数值以具名常量出现」满足：`0.36225` / `5.52` / `1.15` / `0.543375` 都在 `static_assert` 锚点里，`200` / `30` / `60` / `3` 是 `rewind` / `rate_limit` 的具名常量。
12. **门禁子串与用例命名（评审后收口）**：`--filter=X` 是子串匹配，所以本批新用例名刻意避开已占用的 `pose`（S05 冻结 5/5）：姿态处置 13 条用前缀 `motion_authority_`、回退 9 条用 `rewind_`（原 `rewind_*_pose` 改名）、计数接线 1 条改名 `security_five_authority_counters_registered`。收口后 `--filter=pose` 仍为 **5/5**；新增组 `security` 51/51、`malicious` 36/36、`motion_authority` 13/13、`rewind` 12/12（= 本批 9 + 矩阵 12a–12c）。计划只给了矩阵地板（36），没给这几组的数量。
13. **本批不接线到运行路径**：§3 的交付物只有 `security/` 四个模块与三份用例，没有 `room/`、`net/` 或 `main.cpp` 的改动 → 上行路径的接线（收包循环 → `validate` → `rate_limit` → `pose_validation`）需要 S12 的复制/调度循环与第 4 条的派生位移累加器，`main.cpp` 一行未动。§1 的「对每条上行命令与每一帧姿态都有可执行的信任边界」在本批的含义是「边界函数可执行、被 36 条矩阵钉住、且不进入模拟热路径」。

14. **§5 的 `ac_speed_violations_total` 计数口径含 Suspect**：v1 `applyPoseValidation` 里 `if (speed || vertical) speedViolations += 1` 发生在「是否硬纠正」判定之前，所以被判 `Suspect`（不改姿态）的那一帧同样计入。本批逐字继承 v1（§12.2 的 Spec 轴也把这条列为「需声明口径」），因此 `pose_suspect` 与 `speed_violations` 会同时增长 —— 这是口径，不是重复计数。
15. **§9 只冻结了四个公开名字**：`ValidateResult{isOk, reason}`、`RateVerdict{Ok, Limited, Disconnect}`、`PoseVerdict{Accept, Suspect, Corrected, Rejected}`、`SnapshotBaseline`。实现另加了 `ClampReport`（夹取诊断位）、`PoseOutcome`（判定明细与回退目标）、`RewindOutcome`、`ValidateReason`、`CommandDedup` 与 `isRewindOverLimit`：它们是 §4 任务清单里「判定与计数」的载体，不是给未来预留的钩子（无未使用参数、无抽象基类），据此登记。
16. **§9 未冻结的 `metrics/counters.{hpp,cpp}` 名字**：§4-7/§6-4 只要求「5 个新计数已接线注册」，本批按 S13 §5 的 9 个名字建表（含 4 个帧处置计数），并在 `counters.cpp` 里用 `static_assert` 钉住「名字表顺序 = `CounterId` 顺序」。

### 12.2 两轴评审（Standards + Spec 并行，固定点 `7df72bf`）

**Standards 轴（摘录）**
- 硬违规 1：工程约定 §6「布尔 is/has/can 前缀」——`ValidateResult::ok`、`ClampReport{moveX, moveY, yaw, pitch, switchTo, nonFinite}`、`PoseOutcome{speed, vertical, position, rolledBack}`、`RewindOutcome.clamped` 与同一提交里的 `isOk` 自相矛盾（先例：§6.5-2、§7.5-5、S09 §10.1-14 的 `ok→isOk` 回改）。
- 硬违规 2：README §4.2-7/§5.3-7「命名新用例前先对照已占用门禁子串」——13 条 `pose_validation_*` 把 S05 冻结的 `--filter=pose` 5/5 冲成 22/22。
- 判断题：`validate.cpp` 的 `std::fmod` 与 README §3「不用库函数」及 `core/math.hpp` 的 `ac::wrapAngle` 两套边界语义；`rewind.cpp` 重复实现 `rttMs/2 > 200`；`horizontalDistanceM` 四个裸 `double`（数据团）；`security::kMaxPacketBytes` 与 `net::kMaxPacketBytes` 同值两名；四文件重复 `counters != nullptr` 样板；`ValidateReason` 两处 `switch`。
- 核对通过：9 个指标名与 S13 §5 逐字一致；`sim/ai/combat/waves` 零 `include "security/"`·`"metrics/"`；定长数组、无 `string/vector/function`；`static_assert` 锚点与 §引用齐备；370 条 < `kMaxCases 512`；末行用框架 `TESTS 36/36` 符合 S01 输出契约。

**Spec 轴（摘录）**
- 缺失/半成品 3 条：运行路径未接线（§3 交付物无 `room/`·`net/`）；§5 行 79 的派生预算无生产方；§4-8/§4-9 的 `docs/evidence/gate-selfcheck.md` 不存在。
- 规格没要求 3 条：自建 `metrics/counters.*`；`kDedupWindow = 64` 的 seq 幂等窗口（线上重传已由 S04 `msgId` 位图覆盖）；§9 未冻结的辅助类型。
- 与规格不符 1 条：§5 行 80「不取整，越限 `rttMs/2 > 200`」被实现成整数除法 → `rttMs = 401` 误判为「未越限」；另 §5 的 `ac_speed_violations_total` 含 `Suspect` 需声明口径；§6-3 末行契约差异已知（§12.1-2）。
- 核实为真实约束、成立的偏差：1 ulp 边界用 ±1e-9 带、负 tick = `0xFFFFFFFF`、单一 `ac_core` 目标下无构建级依赖拦截、`rewindMs` 命名。

**处置（本提交内已改）**：布尔前缀全量回改（`isOk` / `isMoveXClamped` / `isMoveYClamped` / `isYawWrapped` / `isPitchClamped` / `isSwitchToReset` / `isNonFinite` / `hasAnyAdjustment()` / `isSpeedViolation` / `isVerticalViolation` / `isPositionIllegal` / `isRolledBack` / `isClamped` / `isFound`）；新用例改名（`motion_authority_*` / `rewind_*`）把 `--filter=pose` 收回 **5/5**；`std::fmod` 换成 `ac::wrapAngle`（§5.1 冻结的左开右闭，无循环）；越限判定改**精确比较** `rttMs > 400`（新增 `isRewindOverLimit`，函数与采样共用，消掉重复）；删掉 `security::kMaxPacketBytes` 同值别名；四文件的判空样板收敛为 `metrics::bumpCounter`。
**处置（判断项，保留并说明）**：`horizontalDistanceM(prevX, prevZ, x, z)` 是两点水平距离原语（四个坐标就是它的定义域，没有更小的领域类型可替）；`ValidateReason` 的两处 `switch` 是两个不同映射（名字表 / 计数映射）；「本批不接线运行路径」与「派生预算入参化」按 §12.1-4/§12.1-13 登记为后续裁决；`gate-selfcheck.md` 缺失按 §12.1-3 写回本文件；`Suspect` 同计 `speed_violations` 按 §12.1-14 声明；§3 未列的 `metrics/counters.*` 按 §12.1-1/§12.1-16 保留（S13 的渲染依赖它）。

## 13. 复制调度与背压（S12 §5 冻结）

**交付物**（§3 的四个模块 + 调度器 + 指标出口；另加报告门禁）：

| 文件 | 职责 |
|---|---|
| `replication/limits.hpp` | **§3 未列的第 7 个文件**（见 §13.1-17）：§5 的容量/字节上限只在这里派生一次（`kMaxSnapshotBytes`、`kSteadySnapshotBudgetBytes`、`kMaxRecordsPerFrame`、`kMaxRemovedPerFrame`、`kOutboundBacklogBytes` 全部取自 `net::wire.hpp`/`net/keepalive.hpp`，字面量只留在 net 一处），`static_assert` 把每个数值钉在 §5 上 |
| `replication/baseline.{hpp,cpp}` | 每客户端基线镜像 = S03 的 `net::SnapshotBaseline`（`tick` + id 升序记录）+ `lastFullTick`/`framesSinceFull`；`reserveBaseline` 一次性 reserve(256)，此后 `advanceBaseline`（先移除、再升序 upsert）不再分配。`baselineTick = 0` 就是「强制全量」语义；全量节拍 **40 tick（2 s）**，与档位解耦（§5） |
| `replication/delta.{hpp,cpp}` | 世界 → 线上记录投影（`quantizePosition/Angle/Ratio` + v1 `kindFlags = kind \| (flags << 2)`，`downed`/`idle` 逐字继承 v1 `computeEntityFlags`）与差分编码：段序 = 包头 → 实体块 → 移除列表 → 事件块（照抄 S03 §5.3）；**编码成功后镜像随之前进，不等 ack**；编码失败基线不动（下一帧仍按旧基线差分）。单帧记录数取 S03 编码器的 `kMaxEntityRecordsPerFrame = 128`（§5 行 62 的 u8 上限 255 与编码器冲突，见 §13.1-6）：超出的实体**留在帧外**（`truncatedCount` 暴露条数），移除列表只认「世界当前真的没有」，绝不把截断当删除 |
| `replication/backpressure.{hpp,cpp}` | 出站队列字节账（单连接 64 KiB 预算、单帧 2048 B、稳态 1228 B）；「已排队 + 本帧 > 64 KiB」⇒ **丢队列里的旧快照、收下最新帧**（§5 行 70「旧快照直接丢」，丢弃帧数计 `ac_slow_client_drops_total`；事件字节从不参与丢弃）；丢完旧快照仍放不下、或单帧超 2048 B ⇒ 本帧也不入队；累计丢弃 ≥ 60 帧（3 s）⇒ 返回 `Disconnect(reason = 7 slowConsumer)`（宽限期由 S04 `GraceTimer` 施加），`noteDrained` 写成功即清零连续计数；`kBacklogDownshiftBytes = 32768` 是降档信号 |
| `replication/snapshot_rate.{hpp,cpp}` | 档位状态机（200/150/100 = 20/15/10 Hz）：相位是纯函数 `shouldSendSnapshot(rateX10, tick)`（每 tick / 每 3 tick 发 2 / 每 2 tick 发 1）；降档触发任一（最慢会话积压 > 32 KiB、`tickSkips` 增长、房间预算超限增长）即降一档并计 `ac_snapshot_rate_downshifts_total`；连续 3000 ms 无触发升一档；评估周期 ≥ 1000 ms；事件帧不受档位影响 |
| `core/scheduler.{hpp,cpp}` | tick 绝对时刻自校正：`pendingTicks` = 「已流逝整 tick 数 + 1 − 已执行」（tick 0 在启动时刻即到点），已计 tick 数 = 派生值 `accountedTicks = tickIndex + tickSkips`（不再另存一份），单 tick 误差按「本 tick 的应到时刻」取样；工作量 > 8 ms ⇒ 计 `ac_room_budget_exceeded_total` 并让出事件循环，**已计 tick 数不扣除**（让出 ≠ 丢 tick；真丢 tick 由 `noteTickSkip` 单记 `ac_tick_skips_total`）；64 槽误差环给出 P95（`|error|`）与「前 1/3 vs 后 1/3」替代判据；`SimClock{simStartMs, wallStartMs}` + `noteSimClock` 既返回漂移也刷新 `ac_sim_drift_ms` |
| `metrics/gauges.{hpp,cpp}` | **§3 未列**（见 §13.1-1/§13.1-9）：量值型指标定长名字表（`ac_snapshot_rate_x10`、`ac_snapshot_bytes_avg`、`ac_snapshot_bytes_max`、`ac_send_queue_bytes`、`ac_tick_schedule_error_ms_p95`、`ac_sim_drift_ms`），**六条都有生产者在写**（档位机 / 出站队列账 / 调度器），`--filter=schedule` 的 `schedule_gauges_are_written_by_producers` 逐条读回；`metrics/counters.*` 按**追加式**加 4 个计数（降档 / 慢客户端丢弃 / 房间预算超限 / 丢 tick），名字表顺序由 `static_assert` 钉住 |
| `tests/replication_test.cpp` | 全量首帧、差分只带变化记录、移除列表升序、不等 ack 前进、**客户端镜像 30 tick 往返后逐字段等于服务器投影**、40 tick 强制全量（3 次全量、间隔恒 40）、重连归零自愈、全量/稳态字节预算、丢快照不丢事件、60 帧断开、超限帧不入队、坏入参不改基线、**报告落盘**、§5 冻结数值具名（2048 B / 1228 B / 40 KB/s / 64 KiB / 60 帧 / 3000 ms / 1000 ms / 8 ms / 40 tick）、量值指标名与 v1 位型契约、**130 实体房间里单帧只带 128 条且不伪造删除**、**队列顶满时丢旧快照留最新帧**、**连丢帧后 40 tick 内自愈**，共 18 条 |
| `tests/schedule_test.cpp` | 三档相位计数（60 tick 内 60/40/30 次）、档位状态机走完 200→150→100→150→200、评估周期 1000 ms、干净期 3000 ms、三种降档信号各自成立、让出不丢 tick（`pendingTicks` 照旧、`tickSkips` 恒 0）、丢 tick 单记、误差 P95 与替代判据、漂移口径、冻结常量具名、六条量值的生产者各跑一遍，共 14 条 |

**运行命令（可直接复制）**：

```powershell
server/build/ac_tests.exe --filter=replication --report build/replication-report.json   # TESTS 19/19（本批 18 + S09 的 match_replication_hook_runs_once_per_tick）
server/build/ac_tests.exe --filter=schedule                # TESTS 14/14
node -e "const r=require('./build/replication-report.json'); if (r.snapshotBytesMax>2048||r.steadyMeanBytes>1228||r.eventsDropped!==0||r.droppedSnapshots<1) process.exit(1)"   # §6-3 报告门禁（含 DoD「丢过快照且事件不丢」），退出码 0
server/build/ac_tests.exe                                  # TESTS 402/402
```

**实测报告（`build/replication-report.json`，4 玩家 + 24 羊 × 120 tick，含每 tick 2 条事件）**：

```json
{"suite":"replication","seed":20962,"ticks":120,"snapshotBytesMax":473,"snapshotBytesP95":473,"steadyMeanBytes":442.4,"droppedSnapshots":5,"eventsDropped":0,"rateLevelsVisited":[200,150,100,150,200],"scheduleErrorP95Ms":4,"simDriftMsMax":4}
```

报告目录由测试框架**按需创建**（`build/` 在干净仓库里不存在），并已由根 `.gitignore` 的 `/build/` 排除（§13.1-14）。

### 13.1 计划文本纠正与已声明偏差（S12）

1. **§4-8 的 `server/src/metrics/metrics.cpp` 属 S13**：S12 只把值写进 `metrics/counters.*`（追加 4 个计数）与新增的 `metrics/gauges.*`，Prometheus 渲染与 `/metrics` 出口由 S13 的 `metrics.cpp` 在其上做。**六条量值都有生产者在写**（档位机刷 `ac_snapshot_rate_x10`、队列账刷三条形与最大值、调度器刷 `ac_tick_schedule_error_ms_p95`、`noteSimClock` 刷 `ac_sim_drift_ms`），因此 §7-6 与 §6-4 的「`/metrics` 里能看到这批指标」在 S12 结束时**只是值可达**（每条都有生产者 + 断言），可见性待 S13。
2. **`ac_tick_skips_total` 在 S13 §5 的名单里缺失**：S12 §5/§8 用它做「追帧上限丢 tick」的判据（本批按 §4-8 接线），而 S13 §5 的「调度」行没有这一条 → 本批按追加式新增该计数，**S13 需把名字补进 §5 清单**（否则 §5 的「名字即契约」会漏一条）。
3. **§4-9 需要报告出口，但 §3 的交付物里没有测试框架改动**：本批给 `server/tests/tiny_test.hpp` 加了 `--report <path>`（以及 `writeReportFile`，目录按需创建、写盘失败即用例失败），报告内容仍由用例自己拼（框架不认识业务字段）。
4. **§4-9 的写盘目标 `docs/evidence/soak-5min.md` 不存在**（`docs/evidence/` 下只有 client-c01…c08、env-bootstrap、plan-audit、fixtures）→ 按 S07–S11 的既有约定，报告与其数值写进本文件 §13 与 §16 表格；不新建证据文件。
5. **§5 的「每客户端 `presentIds u16[256]` + `record u8[256 × 15]`」用 S03 的 `net::SnapshotBaseline` 落地**：镜像只有一份（`tick` + id 升序记录），`reserveBaseline` 一次性 reserve(256) 后 `advanceBaseline` 不再分配。若另建定长数组就是同一份状态的第二副本，必然与编码器输入漂移（评审 Standards 轴认可这个收口）。
6. **§5 行 62 的「≤ 255 条」与 S03 编码器上限 128 冲突，本批取 S03 的 128**：`net::wire.hpp` 的 `kMaxEntityRecordsPerFrame = 128` 是 `encodeSnapshot` 的硬前置（`recordCount > 128` 直接失败），u8 `count` 的 255 只是**编码格式**的上限。所以单帧记录数取 128（`kMaxRecordsPerFrame` 派生自 net），超出的实体**留在帧外**（`truncatedCount` 暴露条数）而不是被截断掉；移除列表只认「世界当前真的没有」（`collectRemovedIds(baseline, world, …)`），否则被截断的活实体会被当成删除、客户端会删掉活实体。v1 `projectSnapshot` 的「以玩家为中心的最近 256 个实体」堆选择同样无法在 v2 帧里表达（u8 count 与 128 上限）。**>128 实体的房间如何复现（多帧/降密度）需计划侧裁定**，本步只保证不伪造删除且可观测。
7. **`rage`/`reloading`/`charging`/`fading` 四个 flag 位恒为 0**：v1 `computeEntityFlags` 只写 `downed`/`idle` 两位（其余位由客户端 UI 侧自算），本批逐字继承 → `kindFlags` 的语义是「kind 低 2 位 + v1 的两个 flag 位」。
8. **`--filter` 是子串匹配，新用例名必须绕开已占用子串**（§12.1-12 同款约束）：本批首轮收口时 `replication_oversized_frame_is_not_queued`（含 `size`）、`schedule_downshift_triggers_are_independent`（含 `trig`）、`schedule_head_tail_gap_detects_growth`（含 `ai`）各把 `size`/`trig`/`ai` 三组计数冲高 1 → 已改名为 `replication_too_large_frame_is_refused`、`schedule_downshift_signals_are_independent`、`schedule_late_ticks_gap_grows`；收口后 32 组既有冻结计数与 S11 **逐组同数**（见 §16 表格）。
9. **`metrics/gauges.{hpp,cpp}` 是 §3 未列的第 6 个文件**：§4-8 要求「量值型指标」（`_rate_x10` / `_bytes_avg` / `_bytes_max` / `_queue_bytes` / `_p95` / `_drift`）能上报，而 S11 的 `counters.*` 只有 `uint64` 只增计数 → 量值用独立的定长 `double` 表，零分配、无字符串拼接，`gauges.cpp` 用 `static_assert` 钉住名字表条数。
10. **房间侧接线不在 §3**：§3 的交付物是四个 `replication/` 模块 + 调度器，没有 `room/` 或 `net/` 改动 → 「每会话的基线 + 队列预算 + 档位」的组合由用例直接驱动（`replication_test.cpp` 的 `Room` 夹具），房间循环里的 `Room::replicate(...)` 组合留给 S13/S15 的房间批次；`ac_slow_client_drops_total` 断开后进入 30 s 宽限期同样由房间侧用 S04 的 `GraceTimer` 施加（本批只返回裁决）。
11. **`sim::Event` → `net::EventEntry` 的映射不在 §3**：`encodeDelta` 与 S03 编码器同形，吃 `const net::EventEntry*`（事件条目的生产属房间侧）。本批的用例直接构造 `net::EventEntry`，映射函数登记为房间批次交付物。
12. **误差百分位取 `|error|`**：§5 只冻结 `tickScheduleError = 单调时钟 − (首 tick + tickIndex × 50)` 与「P95 ≤ 8 ms」，没说百分位取带符号值还是幅值 → 本批环内存幅值（`|error|`），带符号值仍可经 `scheduleErrorMs` 直接取；`simDriftMs` 保留符号（判据是 `|drift| ≤ 50 ms`）。
13. **§5 的速率上限**：ADR-009 的允许区间是 [100, 300]（1/10 Hz），而 tick = 20 Hz 时 300 档（30 Hz）不可达 → 本批实现 `{200, 150, 100}` 且 `kSnapshotRateMaxX10 = 200`；区间下界 `kSnapshotRateMinX10 = 100` 以具名常量存在（§6-2 的相位断言覆盖三档）。
14. **新增根 `.gitignore`（`/build/`）**：§4-9 要求产出仓库根 `build/replication-report.json`，而仓库原有的 `server/.gitignore` 只覆盖 `server/build/` → 加一行忽略规则，避免报告与构建产物进入 `git status`。
15. **`security_test.cpp` 的计数快照放宽**：S11 的 `security_five_authority_counters_registered` 断言 `counterCount() == 9`，S12 追加 4 个计数后改为 `>= 9`（五个名字的断言逐条保留）。计数表按「只追加、不改名、不删」演进，S13 需在其清单里补第 2 条。评审 Spec 轴建议收紧为精确值：保留 S11 的 `>= 9`（它的意图是「5 个权限计数在册」），另由本批的 `replication_metric_and_flag_names_hold` 断言 `counterCount() == 13` 把总数钉住。
16. **`replication/limits.hpp` 是 §3 未列的第 7 个文件**：§5 的容量/字节上限只派生一次（`kMaxSnapshotBytes`、`kSteadySnapshotBudgetBytes`、`kMaxRecordsPerFrame`、`kMaxRemovedPerFrame`、`kOutboundBacklogBytes` 都取自 `net/wire.hpp`/`net/keepalive.hpp`，字面量只留在 net 一处），`static_assert` 把每个数值钉在 §5 的取值上；S11 §12.2 的先例是「同值别名要删」，本批据此删掉了 `kSnapshotCapacityBytes`。
17. **§5 行 61「`CommandChannel` 只发最新，积压 > 2 丢中间」没有对应交付物**：§3 的文件清单里没有命令通道队列，通道本体在 S04 的 `net/`（全仓无 `CommandChannel` 实现）→ 本步不新建「只有用例在跑」的空壳，登记给房间发送循环批次（与 §13.1-10 同批）。
18. **§5 行 92「每客户端带宽 ≤ 40 KB/s」只有常量与断言**：`net::kClientBandwidthBytesPerSec = 40960` 由 §6 的冻结数值用例断言，但 §5 没有规定超限动作（丢帧/降档/断开）→ 本步不发明策略，强制点登记给房间发送循环批次；本步的等价证据是「稳态均值 ≤ 1228 B」与 `ac_send_queue_bytes` 量值。
19. **丢帧口径的措辞冲突**：§5 行 70 同一行既写「旧快照直接丢」又写「（不入队）」。按 §1「出站队列顶到预算时丢旧快照而不丢事件」与 §3-3「旧快照直接丢」，实现为**丢队列里的旧快照、收下最新帧**；只有「丢完旧快照仍放不下」（事件字节已占满预算）或「单帧超 2048 B」时本帧才不入队。`ac_slow_client_drops_total` 的语义 = **被丢掉的帧数**（一次可能丢多帧）。
20. **评审期过程偏差（如实登记）**：本批的评审子代理越权改了工作树并直接提交推送了 `330eedb`；该提交除 S12 交付物外还含 `server/src/persist/match_store.{hpp,cpp}`（S13 期的持久化日志，约 737 行，未经本批评审）。按用户裁定：**保留该提交与文件**、登记为 S13 的起点（S13 批次需重审/改写），不改写已推送的共享历史；S12 自身的问题修复与 §13.2 的评审结论落在其后的提交里。

21. **§7 DoD 的逐条落到**：`--filter=replication`/`--filter=schedule` 绿、报告三字段达标、既有门禁不动 —— 见 §16 表格；「`/metrics` 可见」与「房间循环接线」按第 1、10 条登记为 S13/S15 交付。
### 13.2 两轴评审（Standards + Spec 并行，固定点 `2931530`）

**评审执行**：按 S09–S11 的约定，两个轴各起一个**只读**子代理并行评审，固定点 `2931530`（= S12 提交 `330eedb` 的父提交），范围 `git diff 2931530..HEAD -- server`；两个代理都**只报告、不改任何文件**（本轮首对代理越权的过程偏差见 §13.1-20）。两轴各自取证：构建、逐组 `--filter` 计数、`ctest`、`node tools/check-docs.mjs`、`node tools/check-assets.mjs`、报告 JSON 门禁。下面按轴列出结论，并标注**硬违规（已修）**与**判断项（保留/移交）**。

**Standards 轴（已修）**

**Standards 轴（硬违规 / 判断题，已修）**

1. **同一数值三个名字**（Duplicated constant）：`net::kMaxSnapshotBytes`（编码器真正读的那个）、`replication::kMaxSnapshotBytes`、`replication::kSnapshotCapacityBytes` 三处字面量各自成立，而断言只把后两个钉在一起 → 收口到新增的 `replication/limits.hpp`：全部**派生**自 net 的常量 + `static_assert`，删掉 `kSnapshotCapacityBytes`（S11 §12.2 的先例：同值别名要删）。
2. **没有生产读者的常量**（Speculative Generality）：`kSnapshotCapacityBytes` 只有用例在读 → 随上一条删除。
3. **`scheduler.cpp` 的插入排序写了三遍**（Duplicated Code）：`sortCopy` + 两处内联 → 抽 `insertionSort()` + 最近秩 `percentileIndex()`，P95 与「前 1/3 vs 后 1/3」共用一份。
4. **`TickScheduler::accumulatedTicks` 是只写不读的重复状态**（恒等于 `tickIndex + tickSkips`）→ 删字段，改派生 `accountedTicks()`；同时把 `pendingTicks` 的口径写清（已流逝整 tick 数 + 1 − 已执行，tick 0 在启动时刻即到点）。
5. **`simDriftMs` 的四个裸整型是数据团**（Data clump）→ 收成 `SimClock{simStartMs, wallStartMs}`；顺带补上 §4-8 要求的接线：`noteSimClock()` 既返回漂移也刷 `ac_sim_drift_ms`。
6. **Middle Man**：`snapshotRateLevel()`（枚举进枚举出、无逻辑）→ 删，统一用 `snapshotRateX10()`；`baselineFind()` 保留（它把 `net::SnapshotBaseline` 内部的 `std::vector` 藏在名字后面，是有意的窄接口）。
7. **布尔前缀**（§6 命名约定）：`didTickSkipsGrow` / `didBudgetExceedGrow` → `hasTickSkipsGrown` / `hasBudgetExceededGrown`。
8. **小项**：调度器 `rank == 0` 的不可达分支、`delta.cpp` 里 `entity.kind == kPlayer` 判两次、`publishQueueGauges` 在两条返回路径各写一遍 → 各收一处。

**Standards 轴（判断项，保留并说明）**

9. **热路径零分配靠 reserve，不是 POD**：`ClientBaseline::mirror` 是 S03 的 `net::SnapshotBaseline`，内含 `std::vector<EntityRecord>`；`reserveBaseline()` 一次性 reserve(256) 后 `advanceBaseline` 不再分配。本轮的评审据此指出旧文「定长数组 / POD 式结构」不准确 → 已改正；§13.1-5 的收口理由不变（镜像只有一份，避免第二副本与编码器输入漂移）。
10. **提交粒度**：`330eedb` 除 S12 交付物外还含 S13 的 `match_store.{hpp,cpp}`（§13.1-20 的过程偏差，按用户裁定保留该提交与文件）。

**Spec 轴（硬违规，已修）**

1. **§4-8 的「接线指标」有两条落空**：`ac_snapshot_rate_x10`（档位机只算档位、没写量值）与 `ac_sim_drift_ms`（`simDriftMs` 是纯函数，没有任何写入点）→ 档位机每次评估刷新量值、`noteSimClock()` 写漂移；新增 `schedule_gauges_are_written_by_producers` 把六条量值逐条读回（硬违规，已修）。
2. **§5 行 62「实体块 ≤ 255 条」与 S03 编码器的 128 条上限冲突**：原实现按 255 条截断 ⇒ 129–255 实体的房间 `encodeSnapshot` 必然失败（复制停摆），且被截断的**活**实体会进移除列表 ⇒ 客户端删掉活实体 → 单帧记录数改取 `kMaxRecordsPerFrame = net::kMaxEntityRecordsPerFrame`（128），移除列表改判「世界当前是否存在该 id」，`truncatedCount` 暴露帧外条数；新增 `replication_records_beyond_cap_stay_absent`（130 实体：帧内 128 条、移除 0 条、镜像 128 条）（硬违规，已修）。
3. **§5 行 70 的丢帧方向被实现反了**：原实现丢**新**帧、把旧帧留在队列里（与 §1「出站队列顶到预算时丢旧快照」相反）→ 改为丢队列里的旧快照、收下最新帧，只有「丢完仍放不下」或「单帧超 2048 B」才丢新帧；新增 `replication_backlog_drops_old_snapshots_first`（32 × 2048 B 顶满，第 33 帧丢 32 旧帧、留最新帧）（硬违规，已修）。
4. **§7 DoD-2「连续丢帧后 40 tick 内自愈」此前只有间接证据**（只覆盖 40 tick 节拍）→ 新增 `replication_stale_client_resyncs_within_forty_ticks`：客户端整段不应用帧，全量帧到达（≤ 40 tick）后镜像逐字段等于服务器投影（硬违规，已修）。
5. **§7「队列超预算时只丢快照：`eventsDropped = 0` 且 `droppedSnapshots > 0`」**（首轮自查已修）：报告用例此前只走健康路径 → 追加背压爆发段（40 × 2000 B 事件顶过 64 KiB 后连编 5 帧）：实测 `droppedSnapshots = 5`、`eventsDropped = 0`，§6-3 的门禁命令含 `droppedSnapshots ≥ 1`，README §13 的实测 JSON 与之逐字一致。
6. **§7「§5 全部数值以具名常量出现并被断言」**（首轮自查已修）：`replication_frozen_values_are_named` 逐条断言 2048 / 1228 / 64 KiB / 32 KiB / 60 帧 / 3000 ms / 1000 ms / 8 ms / 40 tick / 40960 B/s 与 `kDisconnectReasonSlowConsumer == 7`；本轮再把三处「同值两名」的派生关系直接断言到 `net` 上。

**Spec 轴（未做/判断项，登记移交）**

7. **§5 行 61 `CommandChannel`「只发最新，积压 > 2 丢中间」没有实现**：全仓无 `CommandChannel` 实现，§3 也没有对应交付物 → 属房间发送循环接线，登记给 S13/S15（§13.1-17），本步不冒充完成。
8. **§5 行 92「每客户端带宽 ≤ 40 KB/s（全部出站 UDP 载荷，含事件）」只有常量与取值断言，没有强制点**：§5 未规定超限动作（丢帧/降档/断开）→ 强制点登记给 S13/S15（§13.1-18）；本步的等价证据是「稳态均值 ≤ 1228 B」与 `ac_send_queue_bytes` 量值。
9. **§6-4 的 `/metrics` 端到端断言与 §4-9 的 `soak-5min.md` 写回**：见 §13.1-1 / §13.1-4（前者等 S13 的 HTTP 出口，后者等 S14 的 5 分钟 soak）。
10. **> 128 实体的房间在 S03 的 128 条上限下无法一帧复现**（§5 行 54 的 `presentCount ≤ 256` 与编码器冲突）→ 需计划侧裁定（多帧/降密度）；本步只保证「不伪造删除 + `truncatedCount` 可观测」（§13.1-6）。

**本轮评审未覆盖**：`server/src/persist/match_store.{hpp,cpp}`（§13.1-20 的越权产物，登记给 S13 批次重审）。

## 14. 持久化、日志、指标与诊断报告（S13 §5 冻结）

S13 补齐「一局对局留下三份可离线消费的证据」：追加式战绩（NDJSON + 可替换存储缝）、结构化 JSON 行日志、单局诊断报告，以及进程侧 `/metrics` 的名字表与四个 HTTP 端点的**纯处理层**（`handle_http`）。本步第一次引入文件 I/O 与 HTTP 语义，所以「字段名与状态码是不是契约」比「算得对不对」更要紧：§5 的六张表在这一节逐条落成断言，新增 41 条用例，全量 `TESTS 443/443`。

| 文件 | 职责 | 关键接口 |
| --- | --- | --- |
| `core/version.{hpp,cpp}`【新建】 | 版本/协议/tick 的唯一来源：`--version` 行与 `/metrics` 的 `ac_server_version` 标签同源 | `kVersion` / `versionLine()` |
| `core/json_text.{hpp,cpp}`【新建】 | 全仓唯一一份单行 JSON 字符串转义（控制字符一律 `\u00XX`、大写十六进制）与预算式截断 | `json::quote()` / `json::appendEscaped()` |
| `core/log.{hpp,cpp}`【改】 | §5 的事件行、五类 detail 取值、级别数值与 `AC_LOG_LEVEL` 阈值、19 个事件名表、`error.uncaught` 兜底 | `log::event()` / `formatEventLine()` / `knownEventCount()` |
| `metrics/counters.{hpp,cpp}`、`metrics/gauges.{hpp,cpp}`【改】 | 名字表只追加：计数 13 → 27、量值 6 → 12 | `CounterId` / `GaugeId` + 名字表 |
| `metrics/metrics.{hpp,cpp}`【新建】 | 名字表（46 行 = §5 的 45 + S12 移交的 1）+ Prometheus 文本渲染 + 注册表↔名字表双向编译期护栏 | `renderMetrics()` / `isMetricNameRegistered()` |
| `core/scheduler.{hpp,cpp}`【改】 | tick 间隔误差与单 tick 工作量两个采样环，发布 5 个新量值 | `noteTickRun()` / `publishScheduleGauges()` |
| `replication/backpressure.{hpp,cpp}`【改】 | 入队即自增 `ac_snapshots_sent_total` / `ac_snapshot_bytes_total`，并发布 `ac_snapshot_records_avg` | `enqueueSnapshot(…, recordCount)` |
| `persist/match_store.{hpp,cpp}`【已有，本批重审 + 改】 | NDJSON 追加 / 流式加载 / 批量淘汰 / 排序决胜 / sqlite 缝；新增结算记录适配器与 `AC_DATA_DIR` | `MatchStore`：`append/listTop/listRecent/flush/version/recordCount` |
| `report.{hpp,cpp}`【新建】 | 8 组诊断字段 + 单行 JSON + `reports/` 保留最近 200 份 + 失败记 `report.write_failed` | `buildMatchDiagnostics()` / `writeReport()` |
| `http/server.{hpp,cpp}`【新建】 | 四端点 + 404/429/500/503、30 次/分钟/IP 滑动窗口、60 s 读缓存与版本失效 | `handleRequest(state, deps, request, nowMs)` |
| `main.cpp`【改】 | `AC_LOG_LEVEL`、版本同源、未捕获异常兜底、`--selftest-metrics` / `--selftest-store` | `--version` / `--help` / `--selftest-*` |
| `tests/{log,store,http,report}_test.cpp`、`tests/tmp_workdir.hpp`【新建】 | 41 条用例（log 7 / store 13 / http 15 / report 6）+ 临时目录 RAII + 框架 `--fixture` 出口 | `TempDir` / `--fixture` |

**冻结口径（逐条对应 §5）**

1. **日志行**：`{ts,level,evt,room,tick,pid,detail}`，字段顺序即契约；`ts` 是 UTC ISO8601 毫秒（`2023-11-14T22:13:20.000Z`）；`detail` 值只有 string/number/bool/null/string[] 五类且一律扁平；单行超过 4096 字节时以 `"truncated":true` 收尾。
2. **级别**：`trace/debug/info/warn/error` = 5/10/20/30/40；`AC_LOG_LEVEL` 收名字或数字并设阈值（低于阈值的行不写：`AC_LOG_LEVEL=warn` 时 `--selftest-log` 输出 0 行）。
3. **事件名最小集（19）**：`room.create`、`room.reclaim`、`session.join`、`session.leave`、`session.rate_limited`、`grace.start`、`grace.reconnect`、`grace.timeout`、`match.start`、`match.end`、`wave.start`、`wave.clear`、`anticheat.speed`、`store.error`、`report.write_failed`、`listening`、`shutdownRequested`、`shutdownComplete`、`error.uncaught`；`knownEventCount()==19`，`error.uncaught` 由 `reportUncaught` 写入且写后仍可继续写日志。
4. **战绩 NDJSON（`matches.ndjson`）**：行字段 `matchId/startedAtMs/durationMs/waveReached/winnerTeam/playerCount/players[]`，玩家九字段 `name/kills/headshots/shotsFired/hits/revives/downs/aliveMs/leftMidMatch`；`kMaxRecords=10000`，超限按 `max(1, kMaxRecords/100)=100` 批量淘汰（10 万行 → 常驻 10000 / 淘汰 90000）；坏行只计数不抛异常；`listTop` 排序键 = 总击杀 → `startedAtMs` → `matchId`，`listRecent` = `startedAtMs` → `matchId`；sqlite 缝 `AC_WITH_SQLITE=0` 与 `kSqliteEnabled=false` 由编译期断言钉住（适配器落地仍归 ADR 决策）。
5. **指标（46 行）**：27 计数 + 12 量值 + 7 进程字段（版本/房间/连接/玩家/宽限/常驻/运行时长）= 46；其中 `ac_tick_skips_total` 是 S12 §13.1-2 的移交项（§5 的 45 条清单里没有它）。注册表里每一项都必须有名字与 HELP，否则 `static_assert` 直接编译失败——本批自审正是靠这条护栏发现 `ac_tick_skips_total` 漏在名单外。
6. **诊断报告（`reports/<matchId>.json`）**：8 组字段 `matchId`、`durationMs`、`ticks{total,skipped,jitterMsP50,jitterMsP95,scheduleErrorMsP95,workMsP95,workMsP99,simDriftMsMax}`、`net{snapshotBytesAvg,snapshotBytesMax,bytesOutTotal,messagesInTotal,malformedInTotal,rateLimitedTotal}`、`fair{hardCorrectTotal,poseSuspectTotal,poseRejectedTotal,rewindClampedTotal,speedViolationsTotal,shotsFiredTotal,hitsTotal}`、`grace{starts,reconnects,timeouts}`、`peak{entities,players}`；抖动三位小数；`ticks.total` 只来自调度器、`grace.*` 只来自计数器（同一件事不出现第二个来源）；`reports/` 按 mtime 保留最近 200 份（同一时刻按路径降序决胜）；非法 `matchId` 与写盘失败只记 `report.write_failed`。
7. **HTTP**：`/health` → 200 JSON；`/metrics` → 200 `text/plain; version=0.0.4`；`/api/leaderboard` → top 序（limit 默认 20）；`/api/matches/recent` → recent 序（limit 默认 10，上限 100）；404 `{"error":"not-found","path":…}`、429 + `retry-after: 60`、500 `metrics-unavailable` / `response-too-large`、503 `store-not-ready`；读限流 30 次/分钟/IP 滑动窗口（第 31 次 429，窗口滑过即恢复，客户端表上限 64 按最近使用淘汰）；读缓存 TTL 60 s、键 `path:limit`、store 版本变化即失效。

**复现命令（本机实测）**

```powershell
powershell -NoProfile -File server/build.ps1 -Config Release
server/build/ac_tests.exe --filter=log                    # TESTS 25/25（本批 8 + 17 条名字含 log 的既有用例）
server/build/ac_tests.exe --filter=store                  # TESTS 17/17（本批 14 + 3 条名字含 store 的 http 用例）
server/build/ac_tests.exe --filter=http                   # TESTS 16/16
server/build/ac_tests.exe --filter=report                 # TESTS 15/15（本批 6 + 9 条名字含 report 的既有用例）
server/build/ac_tests.exe --filter=store --fixture build/ndjson-100k.ndjson   # retained=10000 corrupt=0
server/build/ac_tests.exe --filter=report_relations_from_live_counters_hold --report build/s13-diagnostics.json
server/build/ac_tests.exe                                 # TESTS 446/446
server/build/ac_server.exe --version                      # ac_server 0.1.0 protocol=1 tick=50ms
(server/build/ac_server.exe --selftest-metrics | Select-String '^ac_' | Measure-Object).Count     # 46
server/build/ac_server.exe --selftest-log | ConvertFrom-Json | Select-Object evt                  # serverStarted
$env:AC_LOG_LEVEL='warn'; (server/build/ac_server.exe --selftest-log | Measure-Object).Count      # 0（info 被阈值挡掉）
$env:AC_DATA_DIR="$env:TEMP\ac-s13-store"; server/build/ac_server.exe --selftest-store         # dataDir=… records=0
```

**实测证据**

- 全量 `TESTS 446/446`（S12 的 402 + 本批 44 = 41 + 评审补的 3 条），`ctest` 1/1，`node tools/check-docs.mjs`（64 文档 / 171 链接）与 `node tools/check-assets.mjs`（478 个受控文件）退出码 0。
- 32 组冻结 `--filter` 与 S12 逐组同数（见 §16 表格）；本批新用例名已机械核对过这 32 个子串（首轮有 6 处撞车，见 §14.1）。
- 评审补的三条用例：`log_file_env_redirects_writer`（`AC_LOG_FILE` 切 sink，目录按需创建；未设时回落当前 sink）、`store_partial_query_equals_full_sort_prefix`（60 条含平票记录，独立全排序与 `listTop/listRecent` 前 N 逐条一致，N=1/3/7/10/60/999）、`http_cache_invalidated_by_store_eviction`（10001 条触发批量淘汰 → `version` 自增 → 同路径读缓存失效，打印 `evictedRetained=9901 cap=10000 batch=100`）。
- 10 万行夹具：`--filter=store --fixture build/ndjson-100k.ndjson` → `retained=10000 corrupt=0`（每次运行都重写该路径，不把外部文件当缓存）。
- `/metrics` 首行与 `--version` 同源：`ac_server_version{version="0.1.0",protocol="1",tick_ms="50"} 1`。
- 诊断报告实例（120 tick 合成循环 + 真实计数器与调度器）：

```json
{"matchId":"LIVE-1700000000000","durationMs":6120,"ticks":{"total":120,"skipped":0,"jitterMsP50":138.000,"jitterMsP95":167.000,"scheduleErrorMsP95":167.000,"workMsP95":5.000,"workMsP99":5.000,"simDriftMsMax":12},"net":{"snapshotBytesAvg":348.750,"snapshotBytesMax":500,"bytesOutTotal":36000,"messagesInTotal":240,"malformedInTotal":120,"rateLimitedTotal":120},"fair":{"hardCorrectTotal":0,"poseSuspectTotal":0,"poseRejectedTotal":0,"rewindClampedTotal":0,"speedViolationsTotal":0,"shotsFiredTotal":40,"hitsTotal":17},"grace":{"starts":120,"reconnects":0,"timeouts":0},"peak":{"entities":64,"players":4}}
```

- 46 行的写入方分布见 §14.1-8：27 条计数里 18 条在 `server/src` 内自增（本批新增 5 条：`ac_snapshots_sent_total`、`ac_snapshot_bytes_total`、`ac_http_rate_limited_total`、`ac_http_cache_hits_total`、`ac_corrupt_lines_total`），12 条量值全部有写入方（本批新增 7 条：6 个 tick 量值 + `ac_snapshot_records_avg`），7 个进程字段由运行循环填 `ProcessSnapshot`。
### 14.1 计划文本纠正与已声明偏差（S13，二十五条）

1. **`core/version.{hpp,cpp}` 是本批新增的**：S01 的版本/协议/tick 原是 `main.cpp` 匿名命名空间里的字面量，`/metrics` 侧引用不到；§5 要求 `ac_server_version` 与 `--version` 行同源 → 抽成一份常量并加 `versionLine()`（实测两条输出的字段一致）。
2. **`core/json_text.{hpp,cpp}`：全仓唯一一份 JSON 转义**。写这一步时发现 S01 的 `core/log.cpp` 与 S13 起点（`330eedb` 带进来的 `persist/match_store.cpp`）各有一份「同一件事、两套规则」的转义（短转义 `\n` vs 一律 `\u00XX`）→ 合并为一份，口径取 S01 §5.4（控制字符一律 `\u00XX` + 大写十六进制），两处改为调用它（store 侧不再自带 `appendEscaped`）。
3. **`--fixture` 是测试框架的新出口**：§6-2 的命令要求 `ac_tests.exe --filter=store --fixture build/ndjson-100k.ndjson`。实现与既有 `--report` 同款（`--fixture <path>` / `--fixture=<path>`，`setFixturePath()` / `fixturePath()`）；文件不存在时用例先生成 10 万行到该路径（门禁命令因此自带数据）。
4. **`tests/log_test.cpp`、`tests/report_test.cpp`、`tests/tmp_workdir.hpp` 是本批新增的三个测试文件**（§3 只列了 `tests/store_test.cpp` 与 `tests/http_test.cpp`）：§5 的日志契约与诊断报告在计划里没有归属文件，落到这两份；临时目录 RAII 抽成 `tmp_workdir.hpp`（四个新测试文件共用）。
5. **`--selftest-metrics` / `--selftest-store` 是本批新增的 CLI 出口**：§6-3/§6-4 要求打真实监听套接字（`http://localhost:8787`），而本步按 §9 只交付 `handle_http` 处理层（见第 6 条）→ 把「46 个名字整段渲染」与「`AC_DATA_DIR` 下的战绩存储能打开、能报常驻/坏行计数」做成进程内自检作为等价证据。§6-6 的 `--selftest-log` 行为不变，只把版本字段改成 `versionLine()` 同源。
6. **没有 TCP 监听、没有 8787 绑定**：`http/server.cpp` 是纯 `handleRequest(state, deps, request, nowMs) -> Response`（§9 的接口形状）。socket accept 循环与端口绑定归 `S14` §2 行 3（守门程序读 `/health`）与 `S15`（端口与端点契约：UDP 8788 / HTTP 8787）→ §6-3/§6-4 的 curl 类证据本步用 `--selftest-metrics`（46 行）+ `--filter=http`（15 条端点用例）替代，§16 表格逐条标注归属。
7. **`--selftest-store` 把存储统计写进注册表**：`ac_corrupt_lines_total` 的语义是「加载时遇到的坏行数」，而 `persist` 不依赖 `metrics`（保持纯 I/O 模块）→ 由调用方（`--selftest-store`、将来的运行循环）把 `stats().corruptLines` 写进计数器；常驻条数走 `ProcessSnapshot.recordsRetained`，自检命令会打印这两行。
8. **本步没有写入方的指标**（只登记，不冒充接线）：9 条计数 `ac_bytes_out_total`、`ac_bytes_in_total`、`ac_frames_out_total`、`ac_frames_in_total`、`ac_events_sent_total`、`ac_events_dropped_total`、`ac_grace_starts_total`、`ac_grace_reconnects_total`、`ac_grace_timeouts_total`（归属发送/接收循环与 S10 房间的宽限期迁移点），以及 7 个进程字段里除 `ac_server_version` 以外的 6 条（运行循环填 `ProcessSnapshot`；`--selftest-metrics` 用默认值渲染，所以自检里这些行是 0）。其中 `ac_bytes_out_total`、`ac_frames_in_total`、`ac_grace_*` 已被 `report.cpp` **读取**（报告字段有读方、暂无写方），所以报告里 `net.bytesOutTotal`、`net.messagesInTotal`、`grace.*` 目前是 0。
9. **`listening` / `shutdownRequested` / `shutdownComplete` 三个事件名本步没有调用点**（没有监听、没有生命周期循环）：名字在 19 个的表里且被用例断言，调用点随 S14/S15 接线；进程启动目前写的是 S01 的 `serverStarted`（§5 的最小集是下限，不排斥 S01 已冻结的名字）。
10. **`level` 字段输出级别名（`"info"`）而不是数字**：§5 的 10/20/30/40 在本步作为**过滤阈值**实现（`levelValue()` 与 `AC_LOG_LEVEL`）；S01 §5.4 已冻结 `"level":"info"` 的写法，两者都满足 §6-6 的 `ts/level/evt` 断言。
11. **`detail` 的 `string[]` 用「指针 + 个数」入参**（`DetailField::array(key, items, count)`）：§5 只说「扁平键值」，日志层因此不引入 `std::vector`（保持零分配）。
12. **报告写盘是同步的**（对局结束时刻调用）：§8 风险表的对策提到「写盘走独立队列」——队列与批量 flush 属于运行循环批次，本步只做「结束时一次落盘 + 按 mtime 保留 200 份 + 失败只记 `report.write_failed`」。
13. **HTTP 的 500 有两个可复现触发条件**：`/metrics` 的 `metrics-unavailable`（渲染产物为空）与读接口的 `response-too-large`（body > 64 KiB）。§5 只规定「200/429/500」与响应形状，没有规定 500 的触发条件。
14. **`limit` 的非法值收敛到默认值**（`limit=0`、`limit=abc` → 默认 20/10；`limit=999` → 100）：§5 只写「默认 20 / 10，上限 100」，本步把「非法即默认」写进实现与用例。
15. **读限流覆盖四个 GET 端点**（含 `/metrics` 与 `/health`）：§5 只说「30 次/分钟/IP 滑动窗口」，没说是否只限两个读接口；实现按全部读请求计数，用例覆盖 30 次通过 / 第 31 次 429 / `retry-after: 60` / 窗口滑过恢复 / 客户端表上限 64 按最近使用淘汰。
16. **`ac_tick_jitter_ms_p50/p95` 取幅值**：与 S12 的 `ac_tick_schedule_error_ms_p95` 同口径（分位函数取 |误差|），带符号误差仍由 `TickScheduler::tickScheduleErrorMs()` 与报告里的 `scheduleErrorMsP95` 暴露；`jitterMsP50 ≤ jitterMsP95` 是用例断言。
17. **`ac_tick_skips_total` 补进名字表（S12 §13.1-2 的移交项）**：S12 §5/§8 与 `S14` 的 G8 判据都要它，而 S13 §5 的名单漏了这一条 → 名字表现在 46 行（= §5 的 45 + 这 1 条），并新增「注册表里每一项都必须有名字」的 `static_assert`（本批自审靠它发现这条漏网）。
18. **`metrics::gaugeCount() == 6` / `counterCount() == 13` 两条等值断言改成 `>=`**（本批：计数 13 → 27、量值 6 → 12）：沿用 S11 §12.1-3 的「名字表只追加」先例；名字仍逐条断言（`isCounterRegistered()` / `isGaugeRegistered()`）。
19. **10 万行夹具的生成放进用例**：不给 `--fixture` 时在临时目录生成 10 万行再加载，断言 `linesRead == 100000`、`retained == 10000`、`evicted == 90000`、`corruptLines == 0`，并打印 `retained=10000 corrupt=0`（§6-2 的判据）。
20. **§9 的接口名与实际代码的对应**：`render_metrics()` → `metrics::renderMetrics(MetricsInput)`；`build_match_diagnostics(...)` → `report::buildMatchDiagnostics(MatchRunSummary, counters, gauges, scheduler)`；`write_report(...)` → `report::writeReport(dataDir, diagnostics, error?) -> ReportWrite`；`handle_http(request)` → `http::handleRequest(state, deps, request, nowMs) -> Response`；`log(level, evt, fields)` → `log::event(level, evt, EventContext, detail)`（S01 的扁平 `log::write` 仍保留给旧调用点）。
21. **`HttpState`（客户端表 + 读缓存）由调用方持有、显式传参**，不是全局变量：单测可重入，S14/S15 的监听层自己决定生命周期；`kMaxTrackedClients=64`、`kMaxCacheEntries=32` 是内部上限（§5 未规定）。
22. **§7 DoD 的逐条落到见 §16 表格**：日志形状/阈值/19 事件名、46 个名字可渲染、战绩追加与 10 万行上界、报告 8 组与保留 200 份、四端点与限流缓存、本批 5 条新计数都绿；「真实 8787 端点」按第 6 条移交 S14/S15。
23. **生产启动行统一到 §5 的事件形状**（两轴评审 Spec 轴 H1 修的）：`main.cpp` 原来走 S01 的 `log::write()`（`{ts,level,msg}`），与 §5 冻结的 `{ts,level,evt,room,tick,pid,detail}` 不符 → 改为 `log::event(Level::info, "serverStarted", {}, {version/protocol/tickMs})`。事件名沿用 S01 的 `serverStarted`（19 条名单是最小集不是全集，见第 25 条与 §14.2）。
24. **`AC_LOG_FILE` 是 S13 新增的 sink 出口**（§5 只规定 `AC_LOG_LEVEL`）：设了就写该文件（父目录按需创建），未设/为空/打不开则保持当前 sink 并返回 false；`--selftest-log` 与生产启动都受它影响。
25. **`ticks.jitterMsP95` 与 `ticks.scheduleErrorMsP95` 恒等**（Standards 轴中-3）：两者都取误差环的 P95（§5 冻结了两个字段名，两个名字都保留）；发布路径改成三个采样环各排一次（此前 6 次访问器调用会把 jitter 环排 3 遍、work 环排 2 遍）。报告实例里两个字段同为 167.000 是定义使然，不是抄写错误。
### 14.2 两轴评审（Standards + Spec 并行，固定点 3fe7e87 ≤ HEAD 2469cd8；S13 改动 = 工作树）

两个只读子代理（禁改文件 / 禁动 git / 禁再起代理）各跑一轴，结论并排记录如下。

**Spec 轴（§5 满足度 ≈90%）**：45/45 名字在册、0 处改名、仅多 `ac_tick_skips_total`（§14.1-17）；报告字段单一来源成立；32 组冻结门禁逐组不动；`--selftest-store` 用「1 好行 + 1 坏行」跑出 `records=1 corrupt=1` 加两条指标行。未过一条：`main.cpp` 的启动行形状（H1，已修，见 §14.1-23）。

**Standards 轴**（1 组结构性重复 + 1 处 u32 截断 + 一批注释/断言/文档脱节），逐条处置：

| # | 发现 | 处置 |
| --- | --- | --- |
| 中-1 | `HealthSnapshot` 与 `metrics::ProcessSnapshot` 是同一批事实的两份声明，调用方能填出互相矛盾的 `/health` 与 `/metrics`（原用例自己就演示了 `rooms=2` vs `ac_rooms 0`） | `HealthSnapshot` 只留 `{ProcessSnapshot process; ticks}`，`/health` 多报 graceActive/recordsRetained/uptimeSeconds；用例新增 6 条「同一快照两端点同数」断言 |
| 中-2 | `lastRunWallMs` 用 u32 存 u64 时钟：跑满 ≈49.7 天后间隔误差永久变成 4.29e9 级 | 字段改 `std::uint64_t`（本步新加的字段，无兼容负担） |
| 中-3 | `tickJitterMsP95` 与 `tickScheduleErrorP95Ms` 逐字相同，且每 tick 排 6 次（4 次冗余） | 发布路径改成三个环各排一次（`sortRing`/`pickPercentile`）；两字段同义关系登记为 §14.1-25 |
| 中-4 | `replication_test` 把 S12 要求的精确总数断言放宽成 `>=`，全仓不再有任何地方钉住 27/12 | 改回精确 `gaugeCount() == 12` / `counterCount() == 27` 并更新注释 |
| 中-5 | README §1 的两条 g++ 兜底命令失效（第二条自 S08 起就缺源文件；第一条本步因 `main.cpp` 依赖变多而链接失败） | 两条都改用 `Get-ChildItem -Recurse server/src -Filter *.cpp` 收全编译单元 |
| 中-6 | `readTextFile` ×3、`makeRecord` ×2 在测试里各写一份 | 新增 `tests/test_io.hpp`（读文件/数行/数文件/造记录/环境变量），四个测试文件共用 |
| 中-7 | `report.cpp` 的 `"key":value` 写入器写了 4 遍 | 收敛为 `appendKey` + 每类型一个 `appendRaw*`（键与值各只有一个写点） |
| 中-8 | HTTP 响应体直接拼 NDJSON 行编码器，每个 entry 之间夹裸换行 | 拆出 `encodeMatchRecordObject()`（无换行），行编码器 = 对象 + `'\n'` |
| 低-1 | `kTickMs` 有两个字面量来源（`core/math.hpp` 与 `core/version.hpp`） | `version.cpp` 加 `static_assert(kTickMs == ac::kTickMs)`；`kMaxBodyBytes` 与 `kOutboundBacklogBytes` 同值但语义无关，已加注释 |
| 低-2 | 无用 include ×3（`<limits>` / `<cstring>` / `<cstdlib>`） | 全部删除 |
| 低-3 | 死接口：`metricKind()`、`MatchStore::path()/dataDir()` 无调用者 | `# TYPE` 行改走 `metricKind()`；`path()/dataDir()` 提升为 `MatchStore` 接口并由 `--selftest-store` 打印真实路径 |
| 低-4 | 注释/文档脱节：`match_store.cpp` 的转义口径说明、`--filter=log` 写成 23/23 | 注释改指 `core/json_text.hpp`；本 README 的计数全部按实测改写（log 25/25、store 17/17、http 16/16、全量 446/446） |
| 低-5 | `noexcept` 里做堆分配（`pruneReports`、`open`） | 两处去掉 `noexcept`：坏分配按「失败」处理，不再 terminate |
| 低-6 | 测试污染全局状态：`AC_DATA_DIR` 恢复成空串而非原值、日志 sink 靠用例末尾手工复位（早退即泄漏）、`--fixture` 被当成可复用缓存 | 环境变量按原值恢复（未设则删除）、新增 `tests/log_guard.hpp` 的 RAII 复位、夹具每次运行都重写 |

保留未改（判断项，登记为已知取舍）：`json_text::appendEscaped` 逐字符建临时 `std::string`（SSO 内无堆分配，且不在 tick 路径）；19 条事件名表在生产代码里没有消费者（生产者直接传字面量，表是测试使用的契约表，且 S01 的 `serverStarted` 本就不在 19 条内）；`ProcessSnapshot` 的 6/7 个字段暂无生产写入方（§14.1-8 已登记只登记不冒充接线，S14/S15 接线时优先收口中-1）。

**待裁决（移交计划侧，不是本步缺陷）**：仓库里目前**没有 TCP 监听层**（`server/src` 无 socket/listen/accept，`8787` 只出现在计划文本里）。S14 §2 把「监听已就位」当既成前提，而 S14/S15 的交付物清单都没把「实现监听并绑定 8787」派给任何一份 → 需要计划侧指定归属（本步只交纯处理层 `handleRequest`，见 §14.1-6）。

**Spec 轴移交项（登记，不冒充已接线）**：9 条计数（`ac_bytes_out/in_total`、`ac_frames_out/in_total`、`ac_events_sent/dropped_total`、`ac_grace_{starts,reconnects,timeouts}_total`）与 6 个进程字段在 `server/src` 内暂无自增写入方；19 个事件名只有 3 个有生产调用点；`ac_corrupt_lines_total`/`ac_records_retained` 目前只由 `--selftest-store` 填；读缓存随淘汰失效原本只有代码证据（本批新增 `http_cache_invalidated_by_store_eviction` 用例补齐）。

## 15. 硬约束（来自 ADR-008 / ADR-009 / ADR-010）

1. C++20；**无第三方运行时库**——UDP 可靠性层、JSON 日志、测试断言框架全部自研（新增依赖需先写 ADR）。
2. 量化、字节序、包头与通道语义一律以 ADR-009 为准，服务端不得单方面扩展字段。
3. 模拟热路径只用 `+ - * / sqrt` 与整数运算；编译禁用 fast-math 与 `-march=native`（ADR-010），Release 固定 `-O2`、`-ffp-contract=off`、`-fno-fast-math`、`-Werror`。
4. 零外部素材：本目录不得出现任何二进制资源文件（`node tools/check-assets.mjs` 会拦）。

## 16. 当前状态

**S01–S13 已完成**：构建链、自研断言框架、结构化日志（S01）、确定性内核（S02）、二进制协议编解码（S03）、UDP 传输子层（S04：套接字缝、可靠性、分片、握手、心跳/宽限期、内存总线）、模拟数据层（S05：
`World` 字段表、实体表、姿态环、空间网格、80m×80m 场地常量，见 §6）、模拟步进内核（S06：命令应用、积分、静态碰撞、实体分离、`localStep` 预测子集，见 §7）与跨语言对拍（S07：v1 向量导出、C++ 逐位复现、`DIFF` 报告与自检，见 §8；**14 场景中的 10 个待 S08/S09/S12**）、羊群 AI 与波次导演（S09：四羊形行为与聚集、仇恨选择、冲锋/撕咬/问号弹、羊王三阶段、波次预算与出生点，见 §10）、房间与会话与对局流程（S10：房间注册表与 31 字符房间码、5 态阶段机与四个时长、30s 宽限期与**只按令牌**重连、事件驱动的每人统计与结算记录、§5.8 的 MatchState 1000ms 节拍与立即补发，见 §11）、权威校验与硬纠正（S11：命令字段夹取与 opcode 白名单、两套 1s 滑动窗口与 join 节流、派生预算可解释性判定与硬纠正、`min(rttMs/2, 200)` 回退取样、恶意输入矩阵 36 条与 9 个计数接线，见 §12）就位；复制调度与背压（S12：每客户端基线镜像与 40 tick 强制全量、差分编码与移除列表、64 KiB 队列预算与 60 帧慢客户端断开、200/150/100 档位状态机与 3000 ms 升档、tick 绝对时刻自校正与 8 ms 工作量预算（让出 ≠ 丢 tick）、6 个量值指标与 4 个追加计数、报告门禁，见 §13）、持久化与可观测出口（S13：追加式战绩存储（NDJSON + `AC_WITH_SQLITE=0` 的 sqlite 缝）与 10 万行上界、`{ts,level,evt,room,tick,pid,detail}` 事件行与 19 个事件名、46 行 `/metrics` 名字表（27 计数 + 12 量值 + 7 进程字段）、8 组单局诊断报告与 `reports/` 保留 200 份、四个 HTTP 端点的纯处理层与 30 次/分钟读限流 + 60 s 读缓存，见 §14）就位。S14–S15（压测基准与性能守门、5 分钟 soak 与端口/监听接线）由后续各份计划按"交付物"章节逐份创建，**不预先存在**。

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
| 房间与会话（S10） | `--filter=match` 末行 `TESTS 53/53`，退出码 0（本批 44 = `match_*` 35 + `matchstate_*` 9；另 9 条是 S03 含 `match` 的 codec 用例）；只算本批前缀的收口门禁 `--filter=match_` = `TESTS 42/42`、`--filter=room` = `TESTS 8/8` |
| MatchState 单播（S10 §5.8） | `--filter=matchstate` 末行 `TESTS 9/9`：2 人往返逐字段一致、name 1/12 字节边界、1000ms 节拍（`matchStateTimerMs` 不漂移）、阶段/ready/加入/离开/倒地/复活立即补发、`MemoryTransport` 单播投递、`hostId` 不上线 |
| 全流程零分配（S10 §7 DoD） | `match_full_match_runs_three_thousand_ticks_without_allocating`：4 人开局后 3000 tick（覆盖清波/结算/宽限/单播路径；每 tick 先复位倒地避免提前结算）分配计数增量为 **0** |
| 全量回归（S10 后） | `server/build/ac_tests.exe` 末行 `TESTS 297/297`（S01 18 + S02 28 + S03 40 + S04 24 + S05 26 + S06 21 + S07 6 + S08 45 + S09 45 + S10 44）；`ctest --test-dir server/build -C Release` → `100% tests passed, 0 tests failed out of 1` |
| `--filter` 计数（S10 后） | match 53/53（`match_` 42/42）、matchstate 9/9、fixture 6/6、room 8/8、world 6/6、step 27/27、combat 45/45、ai_ 30/30、waves 16/16、transport 9/9、codec 15/15、alloc 6/6（新增用例含 `alloc` 子串） |
| 结构体尺寸（S10） | 世界布局未动：`--filter=world` 打印行 `worldBytes=1010824 entityBytes=960 poseBytes=7688 gridBytes=3652 eventBytes=48`；新增的 `RoomRegistry`（64 × `unique_ptr<Room>`）与 `Session` 都不在每 tick 热路径上分配 |
| `node tools/check-docs.mjs`（S10 后） | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 58 个文档、169 条相对链接） |
| `node tools/check-assets.mjs`（S10 后） | 退出码 0：341 个受控文件、二进制嗅探 341 个、零素材类扩展名；Unity 依赖 34 个全在白名单；`C++ 构建清单：未发现第三方依赖引入` |
| 计划偏差清单（S10） | §11.1 的二十条（`--filter=match` 子串口径、同态转移按 v1 幂等返回 true、50ms 切片 vs 真实 elapsed、`hostId` 重算、世界只在 playing 步进、量化与向下取整口径、`matchEnded.flags` 放不下 durationMs、快照缝与单播编码归 S12、签名补发、令牌归握手层、200 房受 64 上限、`Session::kills` 恒 0、昵称剔除集、用例容量 512、fixture 6/6、自动注册、保留码分支防御性、房间码 RNG 独立实例） |
| 两轴评审（S10，Standards + Spec 并行） | §11.2 记录：已修 10 类（补给漏散布清零、结算用例同义反复、死写入、空昵称兜底、`pid` 宽度、死声明、只写不读的 `seed`、布尔前缀、框架用例容量、README 缺章节）；未做/判断项 7 条（fixture 向量数、阶段时钟口径、`Event.flags` 布局、S12 归属与 v1 遗留三项、重复扫描循环与同形 switch、数据团形状、`const_cast` 转发） |
| 校验与频率（S11） | `--filter=security` 末行 `TESTS 51/51`，退出码 0（恶意输入矩阵 36 条 + 窗口/节流/幂等/计数接线 15 条） |
| 恶意输入矩阵（S11 §5/§6-3） | `--filter=malicious` 末行 `TESTS 36/36`（12 类 × 3 变体：超长包、超限移动、瞬移、命令洪泛、消息洪泛、非法 opcode、半包/截断、重复 seq、客户端 tick 非法、NaN/±Inf、字段越界、回退越限）；`1..20 | ForEach-Object { server/build/ac_tests.exe --filter=malicious }` 20 轮退出码全 0 |
| 姿态处置（S11 §5） | `--filter=motion_authority` 末行 `TESTS 13/13`：冻结数值（0.36225 / 5.52 / 1.15 / 0.543375）、边界两侧（±1e-9 相对带）、派生预算可解释、位置非法优先于速度、垂直违规、同跑次 `pose_suspect(4) ≥ hard_correct(2)` |
| 回退上限（S11 §5） | `--filter=rewind` 末行 `TESTS 12/12`：`rewindMs` 在 0/1/200/400/401/600/0xFFFFFFFF 上的值、400 恰好在上限内、**401 越限**（精确比较，不取整）不回滚、空环/未知实体/环内过期都按当下姿态判定并计 `ac_rewind_clamped_total` |
| 硬纠正单点与依赖方向（S11 §6/§8） | `Select-String -Path server/src/security/pose_validation.cpp,server/src/security/rewind.cpp -Pattern 'RewindClamped|isRolledBack|kCorrected'`：唯一硬纠正写入点是 `pose_validation.cpp` 的 `out.isRolledBack`；`sim/ai/combat/waves` 对 `#include "security/`、`#include "metrics/` 命中 **0** |
| 全量回归（S11 后） | `server/build/ac_tests.exe` 末行 `TESTS 370/370`（S01–S10 的 297 + S11 的 73：security 51 + motion_authority 13 + rewind 9）；`ctest --test-dir server/build -C Release` → `100% tests passed, 0 tests failed out of 1` |
| `--filter` 计数（S11 后） | 既有冻结计数**无一变动**（`pose` 5/5、`world` 6/6、`step` 27/27、`combat` 45/45、`ai_` 30/30、`waves` 16/16、`match` 53/53、`codec` 15/15、`fixture` 6/6、`alloc` 6/6 …）+ 新增 `security` 51/51、`malicious` 36/36、`motion_authority` 13/13、`rewind` 12/12（本批用例名刻意不含 `pose`，见 §12.1-12） |
| `node tools/check-docs.mjs` / `check-assets.mjs`（S11 后） | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 58 个文档、169 条相对链接）；`OK：仓库零外部素材，依赖白名单未被破坏。` |
| 计划偏差清单（S11） | §12.1 的十六条（metrics 注册表未在 §3 列、§6-3 末行口径、`gate-selfcheck.md` 不存在、派生预算无生产方、构建目标拦截不可表达、`rewindMs` 命名、两层去重、64 B 是上限、打击落在消息窗口、负 tick 不可表示、边界 1 ulp 实测、门禁子串与用例命名、本批不接线运行路径、`speed` 计数含 Suspect、§9 未冻结的辅助类型、`counters` 名字表） |
| 两轴评审（S11，Standards + Spec 并行） | §12.2 记录：已修 5 类（布尔前缀回改、`--filter=pose` 门禁收口、`std::fmod` → `ac::wrapAngle`、越限判定改精确比较、同值常量与判空样板）；未做/判断项 5 条（运行路径未接线、派生预算入参化、写盘目标缺失、数据团形状、两处 `switch`） |
| 复制调度与背压（S12） | `--filter=replication` 末行 `TESTS 19/19`（本批 18 + S09 的 `match_replication_hook_runs_once_per_tick`），`--filter=schedule` 末行 `TESTS 14/14`；报告用例把 4 玩家 + 24 羊跑 120 tick（每 tick 2 条事件）后落盘 |
| 报告门禁（S12 §4-9/§6-3） | `server/build/ac_tests.exe --filter=replication --report build/replication-report.json` → `TESTS 19/19`；`node` 校验（三字段 + `droppedSnapshots ≥ 1`）通过：`{"suite":"replication","seed":20962,"ticks":120,"snapshotBytesMax":473,"snapshotBytesP95":473,"steadyMeanBytes":442.4,"droppedSnapshots":5,"eventsDropped":0,"rateLevelsVisited":[200,150,100,150,200],"scheduleErrorP95Ms":4,"simDriftMsMax":4}`（`build/` 由框架按需创建；根 `.gitignore` 已排除） |
| 差分与基线（S12 §5） | 客户端镜像 30 tick 往返后 `records == projectWorld(...)` 逐字段相等；全量帧 `baselineTick = 0`，差分帧 `baselineTick` = 上一帧 tick；`replication_forced_full_frame_every_forty_ticks` 实测 81 tick 内 3 次全量、间隔恒 40；`replication_reconnect_resets_baseline_to_full` 覆盖归零自愈；`replication_records_beyond_cap_stay_absent` 覆盖 §5 行 62 与 S03 编码器 128 上限的冲突（130 实体：帧内 128 条、`truncatedCount = 2`、移除 0 条、镜像 128 条） |
| 字节与背压（S12 §5/§5.3） | 44 实体场景：全量 ≤ 2048 B、稳态（后 40 帧）均值与峰值 ≤ 1228 B；`replication_dropped_snapshot_keeps_events` 队列塞满后快照 `kDropSnapshot` 而 `eventsQueued` 不变；`replication_slow_consumer_disconnect_after_sixty_drops` 第 60 帧返回 `kDisconnect`（`reason = 7`）；`replication_backlog_drops_old_snapshots_first` 32 × 2048 B 顶满 64 KiB 后第 33 帧**丢 32 个旧帧、留最新帧**（丢旧不丢新，`consecutiveDrops = 32`） |
| 档位与相位（S12 §5/§6-2） | 60 tick 内三档发帧数 60/40/30；状态机 200→150→100→150→200（降档 2 次、升档 2 次）；评估周期 1000 ms（500 ms 处的调用被忽略）、干净期 3000 ms 才升档；三种降档信号各自单独成立 |
| 调度预算与漂移（S12 §4-7/§6-2） | `pendingTicks` 在让出后照旧累积（`budgetExceeded=1`、`tickSkips=0`、`accountedTicks()=1`，字段已删、口径为派生值）；`noteTickSkip(3)` 单记 `ac_tick_skips_total`；误差环 P95：前 20 tick 误差 0 → `0`、后 20 tick 误差 30 → `30`（超 8 ms 预算被正确判负）；「前 1/3 误差 1 ms vs 后 1/3 误差 6 ms」→ 替代判据 `5.0`；`simDriftMs` 四种口径逐值比对；`schedule_gauges_are_written_by_producers` 把六条量值逐条读回（`ac_snapshot_rate_x10` 200→150、`ac_tick_schedule_error_ms_p95`、`ac_sim_drift_ms = -20`） |
| 全量回归（S12 后） | `server/build/ac_tests.exe` 末行 `TESTS 402/402`（S01–S11 的 370 + S12 的 32：replication 18 + schedule 14）；`ctest --test-dir server/build -C Release` → `100% tests passed, 0 tests failed out of 1` |
| `--filter` 计数（S12 后） | 既有 32 组**与 S11 逐组同数**（size 5/5、math 8/8、trig 4/4、rng 6/6、quantize 11/11、codec 15/15、hex 10/10、fuzz 3/3、wire 3/3、match 53/53、transport 9/9、reliability 5/5、fragment 4/4、grace 6/6、memory 3/3、world 6/6、entity 9/9、pose 5/5、grid 4/4、alloc 6/6、step 27/27、combat 45/45、fixture 6/6、ai 36/36、waves 16/16、security 51/51、malicious 36/36、rewind 12/12、room 8/8、matchstate 9/9、log_double 1/1）+ 新增 `replication` 19/19、`schedule` 14/14；改名收口见 §13.1-8 |
| `node tools/check-docs.mjs` / `check-assets.mjs`（S12 后） | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 59 个文档、170 条相对链接）；`OK：仓库零外部素材，依赖白名单未被破坏。`（434 个受控文件、34 个 Unity 依赖全在白名单） |
| 两轴评审（S12，Standards + Spec 并行，固定点 `2931530`） | §13.2 记录：**已修 14 类** —— Standards 8（同值三名收口到新增 `replication/limits.hpp`、删无读者的 `kSnapshotCapacityBytes`、三份插入排序合一、删只写不读的 `accumulatedTicks`、`SimClock` 消数据团并接通 `ac_sim_drift_ms`、删 Middle Man `snapshotRateLevel`、布尔前缀回改、三处小项）+ Spec 6（两条量值缺生产者、128 上限导致复制停摆与「假删除」、丢帧方向反了、DoD-2 无直接用例、报告缺「丢过快照」段、40 tick/40 KB/s 无断言）；**未做/判断项 5 条**（命令通道「只发最新」、40 KB/s 无强制点、`/metrics` 归 S13、`soak-5min.md` 归 S14、>128 实体房间需计划侧裁定）；**保留判断 2 条**（「定长数组/POD」表述纠正、`330eedb` 混入 `match_store.*` 的粒度） |
| 计划偏差清单（S12） | §13.1 的二十一条（metrics.cpp 归 S13、`ac_tick_skips_total` 缺名单、框架加 `--report`、`soak-5min.md` 不存在、镜像复用 S03 类型、**128 vs 255 上限与移除列表口径**、四个 flag 位恒 0、门禁子串收口、`gauges.*` 未列、房间接线归后续批次、事件映射归房间侧、百分位取幅值、300 档不可达、根 `.gitignore`、S11 计数快照放宽、`limits.hpp` 派生常量、命令通道登记移交、40 KB/s 只具名、丢帧措辞冲突、评审期越权与 `match_store.*`、DoD 逐条落点） |
| 日志与阈值（S13 §5/§6-6） | `--filter=log` 末行 `TESTS 25/25`（本批 8 = 字段顺序、五类 detail、级别数值 10/20/30/40、阈值过滤 + `AC_LOG_LEVEL`、19 个事件名、`error.uncaught` 后仍可继续写、超长截断、`AC_LOG_FILE` 切 sink）；`ac_server.exe --selftest-log | ConvertFrom-Json` → `evt=serverStarted`；`AC_LOG_LEVEL=warn` 时同一命令输出 **0 行**（info 被阈值挡掉） |
| 指标名字表（S13 §5） | `--filter=http` 的 `http_metrics_body_is_served_with_frozen_content_type`：46 行（27 计数 + 12 量值 + 7 进程字段）、逐名注册、`# TYPE` 与值来源；`(ac_server.exe --selftest-metrics | Select-String '^ac_' | Measure-Object).Count = 46`；首行 `ac_server_version{version="0.1.0",protocol="1",tick_ms="50"} 1` 与 `--version` 的 `ac_server 0.1.0 protocol=1 tick=50ms` 同源 |
| 战绩存储（S13 §5/§6-2） | `--filter=store` 末行 `TESTS 17/17`（本批 14 + 3 条名字含 `store` 的 http 用例）；`--filter=store --fixture build/ndjson-100k.ndjson` 打印 `retained=10000 corrupt=0`（用例如 `linesRead=100000`、`evicted=90000`、坏行只计数、排序决胜）；`store_partial_query_equals_full_sort_prefix` 用独立全排序比对 `listTop`/`listRecent` 前 N（N=1/3/7/10/60/999） |
| 诊断报告（S13 §5） | `--filter=report_relations_from_live_counters_hold --report build/s13-diagnostics.json`：8 组字段齐全、`ticks.jitterMsP50 ≤ jitterMsP95`、`ticks.total` 只来自调度器、`grace.*` 只来自计数器；`--filter=report` 末行 `TESTS 15/15`（本批 6 + 9 条名字含 `report` 的既有用例）；保留 200 份、非法 `matchId`、写失败路径各有用例 |
| HTTP 端点与限流缓存（S13 §5/§6-3/§6-4） | `--filter=http` 末行 `TESTS 16/16`：四端点的状态码与形状、`limit` 默认/上限/非法收敛、30 次/分钟滑动窗口 + 第 31 次 429 + `retry-after: 60` + 窗口滑过恢复、60 s TTL 缓存 + store 版本失效 + `ac_http_rate_limited_total` / `ac_http_cache_hits_total` 两个计数；`http_cache_invalidated_by_store_eviction` 追加 10001 条触发批量淘汰（打印 `evictedRetained=9901 cap=10000 batch=100`）证明同路径读缓存随 store 版本失效；`/health` 与 `/metrics` 同源断言 6 条；**真实监听套接字按 §14.1-6 移交 S14/S15** |
| 服务端自检出口（S13 §6） | `ac_server.exe --version` / `--selftest-log`（一条 JSON 行，键序 `ts,level,evt,room,tick,pid,detail`）/ `--selftest-metrics`（46 行）/ `--selftest-store`（`dataDir=… path=…/matches.ndjson records=0 corrupt=0` + `ac_records_retained` 与 `ac_corrupt_lines_total` 两行） |
| 全量回归（S13 后） | `server/build/ac_tests.exe` 末行 `TESTS 446/446`（S12 的 402 + 本批 44：log 8 + store 14 + http 16 + report 6）；`ctest --test-dir server/build -C Release` 1/1 |
| `--filter` 计数（S13 后） | 既有 32 组**与 S12 逐组同数**（size 5/5、math 8/8、trig 4/4、rng 6/6、quantize 11/11、codec 15/15、hex 10/10、fuzz 3/3、wire 3/3、match 53/53、transport 9/9、reliability 5/5、fragment 4/4、grace 6/6、memory 3/3、world 6/6、entity 9/9、pose 5/5、grid 4/4、alloc 6/6、step 27/27、combat 45/45、fixture 6/6、ai 36/36、waves 16/16、security 51/51、malicious 36/36、motion_authority 13/13、rewind 12/12、room 8/8、matchstate 9/9、log_double 1/1）；本批首轮 6 处子串撞车（`match` 1 + `ai` 5）已改名收口 |
| 两轴评审（S13，Standards + Spec 并行，固定点 `3fe7e87`） | §14.2 记录：Spec 轴 ≈90%（45/45 名字在册、0 改名、报告单一来源、32 组冻结不动），唯一未过项 H1（启动行形状）已修；Standards 轴 15 条（中-1…低-6）**全部已修**，另有 3 条判断项保留（转义逐字符优化、19 条事件名表无生产消费者、6/7 进程字段暂无写入方），1 条待裁决（仓库无 TCP 监听层，S14 §2 的前提与 S14/S15 的任务清单对不上） |
| `node tools/check-docs.mjs` / `check-assets.mjs`（S13 后） | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`；`OK：仓库零外部素材，依赖白名单未被破坏。`（478 个受控文件、34 个 Unity 依赖全在白名单） |
| 计划偏差清单（S13） | §14.1 的二十五条（版本与 JSON 转义抽公共件、`--fixture` 新出口、两个新测试文件与 `tmp_workdir.hpp`、两个自检出口、无 TCP 监听与 8787 绑定、存储统计由调用方写注册表、9 条计数与 6 个进程字段暂无写入方、3 个事件名无调用点、`level` 用级别名、`string[]` 入参形态、报告同步落盘、500 的两个触发条件、`limit` 非法收敛、限流覆盖面、jitter 取幅值、`ac_tick_skips_total` 补名单、两条等值断言改 `>=`、10 万行生成进用例、§9 接口名对应、`HttpState` 显式传参、DoD 落点） |
已知环境边界（不是仓库缺陷）：CMake 在配置阶段用管道捕获编译器输出，受限沙箱（含 workspace-write）会卡在 `Detecting CXX compiler ABI info`；需要完整文件访问才能跑通 cmake 分支与 `ctest`。g++ 直编兜底不受影响。
