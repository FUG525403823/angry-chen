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

> 该命令只编 `ac_server.exe`（与 S01 §5.3 字面量一致）；要复现 `ac_tests.exe`，用同一批开关加 `-I server/tests` 编 `server/tests/main_test.cpp`。

| 产物 | 路径 | 说明 |
| --- | --- | --- |
| `ac_server.exe` | `server/build/ac_server.exe` | ServerProcess 入口（`--version` / `--help` / `--selftest-log`） |
| `ac_tests.exe` | `server/build/ac_tests.exe` | 自研断言框架的全部用例；`--filter=<子串>` 只跑匹配用例 |
| `libac_core.a` | `server/build/libac_core.a` | `ac_core` 静态库（后续 `sim/net/ai/combat/room/persist` 都并进它） |
| ctest 用例 | 名 `unit` | `add_test(NAME unit COMMAND ac_tests)` |

后续计划的测试文件只需 `#include "tiny_test.hpp"` 并写 `AC_TEST(...)`，统一由 `ac_tests` 执行；**不得**另建第二套断言宏或第二个构建入口。

## 2. 结构化日志（S01 §5.4 冻结）

`server/src/core/log.hpp` 提供 `ac::log::write(Level, evt, fields)`、纯序列化 `ac::log::formatLine(epochMs, ...)`，
sink 切换 `useStderr()` / `useStdout()` / `useFile(path)` / `close()`（默认 stderr）。

- 字段顺序固定 `ts,level,evt,tick,room,<附加键按插入序>`；把 `tick` / `room` 当普通字段传入即可，序列化时自动提到第 4、5 位。
- `ts` 是定长 UTC ISO8601 毫秒（`YYYY-MM-DDTHH:MM:SS.mmmZ`，纯整数换算，不用 libc 时区接口）；double 用 `%.17g`，非有限值输出 `null`。
- 字符串转义：双引号写成 \`"`、反斜杠写成 \\`；控制字符写成大写十六进制 \u00XX`（例：换行 → \u000A`）。
- 单行 ≤ 4096 字节：从第一个放不下的字段起其后字段全部丢弃（不留空洞），并在末尾追加 `"truncated":true`。
- [工程约定](../docs/00-共识/工程约定.md) §7 里 v1 的日志形状写法 `{ts, level, room, tick, evt}` 以本份 §5.4 的 `ts,level,evt,tick,room` 为准（§1 已把 §7 的 v1 细则降级为参考）。

## 3. 硬约束（来自 ADR-008 / ADR-009 / ADR-010）

1. C++20；**无第三方运行时库**——UDP 可靠性层、JSON 日志、测试断言框架全部自研（新增依赖需先写 ADR）。
2. 量化、字节序、包头与通道语义一律以 ADR-009 为准，服务端不得单方面扩展字段。
3. 模拟热路径只用 `+ - * / sqrt` 与整数运算；编译禁用 fast-math 与 `-march=native`（ADR-010），Release 固定 `-O2`、`-ffp-contract=off`、`-fno-fast-math`、`-Werror`。
4. 零外部素材：本目录不得出现任何二进制资源文件（`node tools/check-assets.mjs` 会拦）。

## 4. 当前状态

**S01 已完成**：构建链、自研断言框架与结构化日志就位；`src/` 只有 `main.cpp` 与 `core/log.*`，网络/模拟/AI 由 S02 起的各份计划按"交付物"章节逐份创建，**不预先存在**。

本机实测（2026-09-24，Windows 11 + Windows PowerShell 5.1）：

| 命令 | 实测 |
| --- | --- |
| `g++ --version` | `g++.exe (x86_64-win32-seh-rev1, Built by MinGW-Builds project) 15.2.0` |
| `powershell -NoProfile -File server/build.ps1 -Config Release` | 退出码 0，末行 `[build] ok ac_server.exe`；`[build] toolchain: cmake 4.2 + ninja + C:\\Program Files\\mingw64\\bin\\g++.exe` |
| `server/build/ac_server.exe --version` | `ac_server 0.1.0 protocol=1 tick=50ms` |
| `server/build/ac_tests.exe` | 末行 `TESTS 18/18`，退出码 0 |
| `server/build/ac_tests.exe --filter=log_double` | `PASS log_double_format` + 末行 `TESTS 1/1`，退出码 0 |
| `ctest --test-dir server/build --output-on-failure` | `100% tests passed, 0 tests failed out of 1` |
| `server/build/ac_server.exe --selftest-log \| ConvertFrom-Json` | 解析成功，键序 `ts,level,evt,version,protocol,tickMs` |
| g++ 直编兜底（第三条命令 + 同开关 `-I server/tests`） | `ac_server.exe` 与 `ac_tests.exe` 均编译成功，`--version` 输出与 cmake 分支一致 |
| `Select-String -Path server/CMakeLists.txt,server/build.ps1 -Pattern 'ffast-math','-march=native','-mfma'` | 0 命中 |
| `Get-ChildItem server/src -Recurse -Include *.hpp,*.cpp \| Select-String -Pattern 'std::(round\|lround\|llround)\('` | 0 命中 |
| `node tools/check-docs.mjs` | 退出码 0：`OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`（扫描 49 个文档、154 条相对链接） |
| `node tools/check-assets.mjs` | 退出码 0：54 个受控文件零素材类扩展名；`C++ 构建清单：未发现第三方依赖引入` |

已知环境边界（不是仓库缺陷）：CMake 在配置阶段用管道捕获编译器输出，受限沙箱（含 workspace-write）会卡在 `Detecting CXX compiler ABI info`；需要完整文件访问才能跑通 cmake 分支与 `ctest`。g++ 直编兜底不受影响。
