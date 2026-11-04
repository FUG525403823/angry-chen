# P1 differential v1 — independent offline specification

## Run

From the repository root: `python tools/p1-differential.py`.
An already-built adapter can be used with `--cpp-exe <absolute-path>`.
The default build uses `g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror`, and only the new adapter plus production Run, Encounter, Build and weapon translation units. No CMake changes. C# is compiled with the explicitly configured Windows Mono csc; no Unity engine starts.

- [Input spec](input.json) is authored before replay and is not derived from C++ output. It is JSON with deliberately compact one-record-per-line scalar objects, checked by the Python entrypoint. `dtMs=50`; ticks are zero-based. Run requests execute before tickRun; newly-entered combat executes its first Encounter tick immediately. Each request is dispatched in listed order, including multiple same-tick requests. Run and standalone Encounter use explicit defended/invader inputs; these are public module arguments, not simulated room presence or movement.
- [Hand expectations](expectations.json) contains rule-derived boundary assertions, not exported goldens. No historical fixture is read as a new expectation or rewritten.
- [C++ adapter](../../../../server/tools/p1-differential.cpp) calls actual production beginRun/request/tickRun/finishEncounter, beginEncounter/stepEncounter, enableBuild/selectCard/updateBuild/calculateHitModifier/onHit/onKill. It never duplicates those rules to stand in for production.
- [C# specification](../../../../tools/p1-differential.cs) independently calculates public ADR-022/content rules using its own state classes and consumes only the input spec. It neither reads C++ projection files nor references client gameplay classes. It is a verifier, **not client-authoritative gameplay**.
- [Runner](../../../../tools/p1-differential.py) compares every emitted field, in every tick, and then the uint64 FNV1a64 hash chain. It independently validates each C++ hash too. Doubles are their complete IEEE754 binary64 bits (16 lower-case hex digits), not tolerance-rounded numbers or JS doubles. The chain resets per case, starts `cbf29ce484222325`, multiplies by `100000001b3` modulo 2^64, and includes the UTF8 projection text without newline/hash suffix. This versioned projection intentionally differs from Classic's decimal schema without changing Classic.

## Bounded coverage

Six cases, 24,468 ticks: three successive 180-second defended encounters with rewards between; 4 invaders destroy the 1000HP objective at 125s; undefended timeout at 240s; facility destruction and 180s completion on the same tick must prefer destruction; all six cards separately and combined; disabled Classic Build neutrality. Rewards include fixed seed 17 using independent RNG stream 4, prerequisites/nonduplicate/unowned filtering, one reroll, reroll duplicate replay, exhausted reroll rejection, skip supply, early continuation, three nodes and terminal success. Requests additionally exercise duplicate conflict and old-run `UINT32_MAX` before current ID 1 (and again before ID 2): stale runs must return wrongRun without contaminating current-run cache/watermark. This is the run-scoped interpretation of ADR-022's per-player **per-run** monotonic IDs.

Build events cover pre-hit marking order/expiry, head refund cooldown, stationary 1s threshold/movement reset, near stagger/cooldown, near healing and active-mag refill/reload cancellation, unchanged reserve, far penalties, and multiplication in card-ID order. Build projection columns are: selectedMask, stationaryMs, modifierBits, hpBits, activeMag, reserve, reloadEndsBits, triggerCount, refundReadyAtMs, healReadyAtMs, tempoReadyAtMs, markUntilMs, staggerReadyAtMs, timerBits, sheepState. Hits are scripted direct public-hook events; this does not claim ballistic, AI, or real socket coverage.

The content bytes are pinned by SHA256 `22ad0db7b9d1278854d75284e80f376da7758de3388ae0ce096090c8d8a890e7`; FNV1a32 is `079e8cb1`. The runner refuses changed content until this new fixture version is deliberately reviewed.

## Evidence and limits

The build/reports paths below are local generated artifacts, excluded from the source commit; they are not public GitHub evidence links.

Report directory（本机 `../../../../build/reports/p1/differential/`） contains complete C++ and C# per-tick projections, compiler stdout/stderr and exit files, execution exit files, runner log, summary, and `gate.exit`. Two negative controls mutate only disposable C# report copies (numeric progress and hash respectively); both must be rejected with first case/tick/field diagnostics and write exit 1. The gate itself writes exit 0 only after positive and negative checks succeed.

This is new **pure-module** evidence, not a substitute for room/runtime/codec/production integration tests, human play evidence, Classic movement fixtures, tool-clock safety-stage checks, or P4's full matrix. In particular, no C# production Run implementation is claimed: production C# still predicts movement only. Existing Classic fixtures stay unchanged; this suite adds a disabled-Build neutral check, but does not re-run the historical Classic fixture gate. No net/room/client/business/CMake files are changed, no engine is started, and no commit is created.
