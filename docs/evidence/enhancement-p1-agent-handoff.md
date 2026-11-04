# P1 Agent Handoff

## Objective and boundaries

Continue only the approved P1 finite Expedition slice in D:/projects/tmp/angry-chen. Do not repeat requirements interviews, redesign the project, reopen P0 or expand P2-P4. Preserve Classic, historical fixtures and external uncommitted edits. User authorizes committing/pushing verified P1-only changes, not secrets, build caches or public deployment. P1 is NOT accepted yet. Never fabricate visual or human approval.

Source checkpoint before this handoff: 082f857. Read enhancement-plan.md, 00-共识/CONTEXT.md, ADR-022 and the linked evidence below. Inspect actual git status before acting; do not trust old test counts as current execution.

## What is verified

- Authoritative server 603 tests; C++/C# differential 24468 lines, 34 priors, 2 negative controls. See enhancement-p1-checklist.md for original evidence and preserved failed runs.
- Real solo hunter, two-player and four-player full three-node runs, combat/reward recovery under approved impairment, valid card/tool triggers. Automation is not human fun acceptance, and successful seeds do not prove every run wins.
- P1-only small sheep groups now use instancing on supported hardware. Classic threshold remains 25. Real clean third-node four-player draw failure (131-141) was reproduced in a short synthetic threshold probe (24 sheep145 draws vs25 sheep58), then fixed and original scene rerun.
- Clean 15c3939, no scratch probe, engine293/293, release dependency leak audit PASS, actual4p run a054423b full success; third-node 3 rounds each/missing0/scoped frame+graphics PASS: maxdraw71/65/70/70, maximum P95 17.486ms, P99 21.8384ms. Original budgets20/33ms, draw120, triangles180000 unchanged. Actual engine quality5/VSync1; not the old synthetic quality2 paired gate.
- Reward keyboard added in ccc31d5: Tab/ShiftTab/arrows and official Submit; real PlayMode official focus and received Request13 plus guards tested. Full engine294/294. OS input NOT verified: foreground acquisition failed safely three times before sending any keys. Shared EventSystem with built-in navigation enabled remains unverified for double submit; do not globally change Classic input without proof.
- Reward observer added in082f857. Stage/offer changes reset incomplete windows and maxima. Real4p impaired reward recovery run ef782d29 full technical success, but each peer only1 complete reward round, missing0. Correct reward verdict remains UNVERIFIED, not PASS. Zero unused array slots are not observations.

## Current deliverable

Local build/reports/p1/P1-15c3939-candidate.zip, SHA256 a7229340de550bbcf48cc68ac045b0a4938dfd3dc752498b03b418703b3d908c. Complete Windows client/server/start scripts/BUILD-IDENTITY/SHA256SUMS. Not uploaded to GitHub. Does NOT include later keyboard/reward observer changes; not final acceptance package.

## Remaining work, in order

1. Obtain three complete real reward sample rounds. Proposed next vertical slice: opt-in acceptance driver reward reading wait, default0, validated0..20seconds, each runId/offerId resets timer, then normal Request choice. Do NOT alter server30second deadline, award cards directly, shorten combat or stitch partial samples. Natural UI reading wait is permitted; rules and difficulty unchanged. Repeat public-seam RED-to-GREEN, full engine and actual4p reward observation. Do not endlessly rerun an immediate-choice driver expecting sufficient samples.
2. Final same-source release/development paired performance using original budgets. Real expedition exact allocation is still UNMEASURED. GameLoop.Frame window excludes acceptance drive, settings and Canvas; do not present synthetic0B or heap delta as whole-frame exact allocation. Reward/combat frame/graphics scopes do not prove GPU time or human visual quality.
3. Test actual player keyboard OS path on an interactive desktop, with exact owned HWND foreground validation before keys; distinguish memory-transport diagnostic from real network. Preserve failure logs. Existing scratch probe can observe defaultCard0 but no OS keys were sent. Shared EventSystem double-submit risk needs a correctly scoped reproducible test/fix if applicable.
4. Real Canvas720/1080/1440 reward+combat, long terms and focus/interaction review. Source risk: build text rectangle intersects healing button. Existing images are unreviewed; legacy capture had125% DPI virtualization. Use physical/DPI-aware capture and actual engine dimensions; no upscaled images as evidence. Correlate stage/offer/receipt accurately. Image nonuniformity is not visual acceptance. Earlier model could not read images; check current capability and use human review where required.
5. Genuine limited human playtest using enhancement-p1-human-playtest.md: unassisted first node, both archetypes, explain conditions/tradeoffs, choice time/failure/restart interest. Empty table remains empty until real responses. Do not claim fun from automation.
6. Update stale enhancement-p1-checklist.md to a precise gate matrix (its older candidate and no-keyboard claims are stale). Build final clean candidate without scratch hooks, audit release leaks, rerun relevant scenarios, SHA256/package identity, update human materials. Only verified P1 changes commit/push; build artifacts stay local.

## Working tree and paused RED cleanup

The paused reward reading slice only added tests referencing an unimplemented RewardReadingWindow and runner assertions. These agent-owned uncommitted additions were removed for this handoff; no helper/driver wait was implemented. The earlier RED log is local build/scratch/p1-client-ui/reward-read-red.log. Recreate the approved public seam tests when implementing; do not assume the feature exists. Cleanup restored these two files to HEAD, not external edits.

External dirty DO NOT stage/revert: Boot/PresentationLayer.cs (remaining weapon material edits), Core/BuildInfo.g.cs, View/RemotePlayerVisuals.cs, SheepMesh.cs, WeaponMesh.cs, Shaders/SheepVertexLit.shader, Tests/SuiteRegistry.cs, Packages/manifest.json and packages-lock.json, ProjectSettings/GraphicsSettings.asset and ProjectSettings.asset, tools/client-baseline-check.ps1; untracked ModelPreview/ModelMeshBuilder/ModelGeometrySuite and metas. Reinspect actual status. PresentationLayer P1 instancing hunk is already committed; only unrelated dirty remains. No git reset/clean/checkout to restore the worktree.

## Environment and commands

Windows Tuanjie D:/tools/unity/unity_install/2022.3.62t16/Editor/Tuanjie.exe. Build method Ac.Editor.BuildEntry.BuildWindows64. Full suite Ac.Tests.SuiteRegistry.RunAll. tools/p0-engine-local.ps1 handles normal engine calls; custom PlayMode method Ac.Tests.ExpeditionKeyboardSuite.RunInPlayMode must run WITHOUT -quit (the helper only special-cases the old PlayModeSmoke.Run). Serialize engine/build/player/performance; never benchmark concurrently. Save real child exit codes, not only wrapper exit0.

Clean export: git checkout-index --all --prefix=<new-absolute-isolated-dir>/, never reuse/overwrite old evidence. Build against clean export to exclude external dirty. Current server build/scratch/p0-msvc/Release/ac_server.exe. Acceptance client/tools/expedition-acceptance.ps1 supports Players1/2/4, Impaired, RecoveryStage combat/reward, WindowWidth/Height, ObservePerformance, PerformanceStage combat/reward and PerformanceNode1/3. Reward stage reports requestedNode0 by design. RewardReadSeconds is NOT implemented.

Read enhancement-p1-expedition-perf-scope.md, enhancement-p1-ui-audit.md, enhancement-p1-performance-checkpoint.md and enhancement-p1-human-playtest.md for numbers and limitations. Local build/reports evidence and scratch hooks are ignored, not GitHub artifacts. Client cached vendor package directory is needed for codely-dependencies audit-release; clean export lacks it, so use verified vendor contents at workspace root against explicit clean release stage, disclose this distinction.

At handoff no required engine/player background work is running; teammates stopped. Inspect runtime goal state instead of assuming continuation is armed. Work until requested scope is actually achieved; only mark complete with all required evidence, otherwise report concrete remaining gates honestly.
