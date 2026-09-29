using System;
using System.Collections.Generic;
using System.IO;
using Ac.Core;
using Ac.Sim;

namespace Ac.Tests
{
    // C06 §5(e) / fixtures/README.md §7.3 的客户端迁移：v2 向量不再逐帧存 expected，
    // 于是本用例改成**重放**——从上一个关键帧的本机实体出发，按 `script` 的 RLE 命令逐 tick 步进，
    // 到下一个关键帧处与本机实体逐字段、逐位比较。
    //
    // 覆盖边界（诚实登记，非放宽门限）：LocalStep 只有玩家运动学 + 谷仓/栅栏碰撞，
    // 不含羊的撕咬/击退/倒地与武器后坐；本地玩家在这些向量里会与羊交互，重放必然中途发散。
    // 这类向量的**逐帧真值由服务端覆盖**（hashChain 14/14 逐 tick 重算），客户端本批只校验形状
    //（fixtures.loader 的 ShapeProblem）。名单缩短 = 重放能力提升；不在名单里的向量一旦发散，本用例立刻红。

    public static class FixturePredictSuite
    {
        // 路径定位复用 C02 的 FixtureLoader（仓内只有这一份形状规则）
        private static readonly string FixtureDir = FixtureLoader.DefaultDirectory();
        private static readonly List<string> Unrecognized = new List<string>();
        private const ushort LocalEntityId = 1;   // fixture 里的本机玩家
        // §7.2：v2 的命令条目没有 seq/clientTick，本地重放自行补（seq = clientTick = t）。

        private static readonly string[] ExpectedNames =
        {
            "still-60t", "straight-line-240t", "barn-collision-400t", "fence-bounds-400t",
            "rifle-burst-hit-120t", "shotgun-spread-60t", "downed-revive-140t", "sheep-grunt-ai-600t",
            "sheep-ram-charge-300t", "sheep-elite-bolt-300t", "sheep-king-phases-900t",
            "wave-director-1to5-1200t", "snapshot-roundtrip-240t", "rng-streams-600t",
        };

        // 本机重放覆盖不到的向量。原因**逐条核过**（不是"跑不过就跳过"）：这些向量里 1 号玩家在
        // "没有移动命令"的区间内位置仍在变 ⇒ 驱动它的是 LocalStep 没有建模的系统：
        //   · 羊的撕咬/击退/冲撞：sheep-grunt-ai / sheep-ram-charge / sheep-king-phases / rng-streams /
        //     wave-director / downed-revive（玩家被咬后位移、倒地）
        //   · 武器后坐与开火位移：shotgun-spread 的 1→15、snapshot-roundtrip 的 1→60（全程无移动命令却位移）
        //   · rifle-burst-hit-120t：1→30 的位置逐位一致，卡在 `pitch=-0.03` ——
        //     fixture 脚本是 **sim 域**的原始弧度（server/tests/fixture_test.cpp:331-334 把 JSON 里的
        //     yaw/pitch 直接喂给 sim），而客户端 `StepLocalPlayer` 是**线上域**（u16 单元、内部反量化），
        //     非 1/65536 圈网格上的角度本来就无法逐位复现（0、±π/2、π 在网格上，所以另外几份能过）。
        // 逐帧真值由服务端覆盖（14/14 逐 tick 重算 hashChain）；客户端本批对它们只校验形状。
        // 名单缩短 = 重放能力提升；ReplayableVectors 下限保证名单不会悄悄变长。
        private static readonly string[] NotReplayable =
        {
            "downed-revive-140t",
            "rifle-burst-hit-120t",
            "rng-streams-600t",
            "sheep-grunt-ai-600t",
            "sheep-king-phases-900t",
            "sheep-ram-charge-300t",
            "shotgun-spread-60t",
            "snapshot-roundtrip-240t",
            "wave-director-1to5-1200t",
        };

        // 必须逐位重放成功的向量数下限（当前 5 份：静止 / 直线 / 谷仓碰撞 / 栅栏边界 / 问界羊保距）。
        private const int ReplayableVectors = 5;

        public static void Register()
        {
            var vectors = FixtureLoader.Load(FixtureDir);
            DiscoverUnrecognized(vectors);
            SelfTest.Add("fixture_predict.manifest", delegate { ChecksManifest(vectors); });
            for (var i = 0; i < vectors.Count; i++)
            {
                var vector = vectors[i];
                SelfTest.Add("fixture_predict." + vector.Name, delegate { ChecksFixture(vector); });
            }
        }

        // 目录里除共享整数表（trig-table.json）之外的每一份 JSON 都必须被识别成向量，
        // 否则"新向量自动纳入"会静默失效。
        private static void DiscoverUnrecognized(List<FixtureVector> vectors)
        {
            if (!Directory.Exists(FixtureDir)) return;
            var known = new List<string>();
            for (var i = 0; i < vectors.Count; i++) known.Add(vectors[i].Name);
            var files = Directory.GetFiles(FixtureDir, "*.json");
            Array.Sort(files, StringComparer.Ordinal);
            for (var i = 0; i < files.Length; i++)
            {
                var stem = Path.GetFileNameWithoutExtension(files[i]);
                if (stem == "trig-table") continue;
                if (!known.Contains(stem)) Unrecognized.Add(stem);
            }
        }

        private static void ChecksManifest(List<FixtureVector> vectors)
        {
            SelfTest.True(Directory.Exists(FixtureDir), "对拍向量目录存在", FixtureDir);
            SelfTest.Equal(0, Unrecognized.Count);
            if (Unrecognized.Count > 0) SelfTest.Fail("目录里的 JSON 未被识别成对拍向量：" + string.Join(",", Unrecognized.ToArray()));
            for (var i = 0; i < ExpectedNames.Length; i++)
            {
                var found = false;
                for (var j = 0; j < vectors.Count; j++) if (vectors[j].Name == ExpectedNames[i]) found = true;
                SelfTest.True(found, "缺少向量 " + ExpectedNames[i], vectors.Count.ToString() + " 份");
            }
            SelfTest.Equal((long)ExpectedNames.Length, (long)vectors.Count);
            // 重放覆盖面下限：名单里的向量只校验形状，名单外的一律逐位重放。这个数掉了就说明
            // "跳过"变多了——门禁必须看见，否则重放能力退化会静默。
            var replayable = 0;
            for (var i = 0; i < vectors.Count; i++) if (!IsNotReplayable(vectors[i].Name)) replayable += 1;
            SelfTest.True(replayable >= ReplayableVectors, "至少 " + ReplayableVectors + " 份向量必须逐位重放", replayable.ToString());
        }

        private static void ChecksFixture(FixtureVector vector)
        {
            var name = vector.Name;
            SelfTest.Equal((long)LocalStep.StepDtMs, (long)vector.DtMs);
            if (IsNotReplayable(name))
            {
                // 形状已由 fixtures.loader 校验；这里只钉住"名单没写错"——被跳过的向量确实存在关键帧，
                // 且确实与本机重放合不来（同一段脚本里 1 号玩家没有移动命令，位置却变了）。
                SelfTest.True(vector.KeyframeCount > 0, name + " 被跳过但必须有关键帧", vector.KeyframeCount.ToString());
                SelfTest.True(MovesWithoutCommand(vector), name + " 的跳过理由必须成立（该区间无移动命令却位移）", "理由不成立");
                return;
            }

            var config = MoveConfig.Default();
            var frames = vector.Keyframes;
            var compared = 0;
            for (var i = 1; i < frames.Count; i++)
            {
                var from = frames[i - 1].Get("tick").AsInt();
                var to = frames[i].Get("tick").AsInt();
                var anchor = FindEntity(frames[i - 1], LocalEntityId);
                if (!anchor.HasValue) { SelfTest.Fail(name + ": tick " + from + " 缺少本机实体"); return; }
                var expected = FindEntity(frames[i], LocalEntityId);
                if (!expected.HasValue) { SelfTest.Fail(name + ": tick " + to + " 缺少本机实体"); return; }

                // 逐 tick 重放：命令按 RLE 现取；该段没有本机命令时走 §4 的"缺命令 ⇒ 速度归零"分支。
                var state = anchor.Value;
                for (var t = from + 1; t <= to; t++)
                {
                    var command = FindCommand(vector.CommandsAt(t), LocalEntityId);
                    var step = command.HasValue ? ToStepCommand(command.Value, t) : ZeroStep(state, t);
                    LocalStep.StepLocalPlayer(ref state, step, config, LocalStep.StepDtMs);
                }
                if (!Compare(name, to, state, expected.Value)) return;
                compared += 1;
            }
            SelfTest.True(compared > 0, name + " 至少比较一个关键帧区间", compared.ToString());
        }

        private static bool IsNotReplayable(string name)
        {
            for (var i = 0; i < NotReplayable.Length; i++) if (NotReplayable[i] == name) return true;
            return false;
        }

        // "跳过"的诊断条件：某个关键帧区间里 1 号玩家的位置变了，而该区间内它**没有任何移动命令**。
        // 满足它 ⇒ 位移来自 LocalStep 未建模的系统（羊的撕咬/击退、武器后坐）。
        private static bool MovesWithoutCommand(FixtureVector vector)
        {
            var frames = vector.Keyframes;
            for (var i = 1; i < frames.Count; i++)
            {
                var from = frames[i - 1].Get("tick").AsInt();
                var to = frames[i].Get("tick").AsInt();
                var before = FindEntity(frames[i - 1], LocalEntityId);
                var after = FindEntity(frames[i], LocalEntityId);
                if (!before.HasValue || !after.HasValue) continue;
                if (before.Value.X == after.Value.X && before.Value.Z == after.Value.Z) continue;
                var commanded = false;
                for (var t = from + 1; t <= to; t++)
                {
                    var command = FindCommand(vector.CommandsAt(t), LocalEntityId);
                    if (command.HasValue && (command.Value.MoveX != 0.0 || command.Value.MoveY != 0.0)) { commanded = true; break; }
                }
                if (!commanded) return true;
            }
            return false;
        }

        // §4：缺命令的槽位速度归零；朝向沿用当前值（经量化器回代，与服务端读线上 yaw 单元同口径）。
        private static StepCommand ZeroStep(in MoveState state, int tick)
        {
            var step = default(StepCommand);
            step.Seq = (ushort)tick;
            step.Yaw = Quantize.QuantizeAngle(state.YawRad);
            step.Pitch = Quantize.QuantizeAngle(state.PitchRad);
            return step;
        }

        private static bool Compare(string name, int tick, MoveState state, MoveState expected)
        {
            if (Bits(state.X) != Bits(expected.X)) { Report(name, tick, "pos.x", state.X, expected.X); return false; }
            if (Bits(state.Y) != Bits(expected.Y)) { Report(name, tick, "pos.y", state.Y, expected.Y); return false; }
            if (Bits(state.Z) != Bits(expected.Z)) { Report(name, tick, "pos.z", state.Z, expected.Z); return false; }
            if (Bits(state.YawRad) != Bits(expected.YawRad)) { Report(name, tick, "yaw", state.YawRad, expected.YawRad); return false; }
            if (Bits(state.PitchRad) != Bits(expected.PitchRad)) { Report(name, tick, "pitch", state.PitchRad, expected.PitchRad); return false; }
            return true;
        }

        private static void Report(string name, int tick, string field, double client, double fixture)
        {
            SelfTest.Fail(name + " tick=" + tick + " field=" + field + " client=0x" + Bits(client).ToString("X16")
                + " fixture=0x" + Bits(fixture).ToString("X16"));
        }

        private static long Bits(double value)
        {
            return BitConverter.DoubleToInt64Bits(value);
        }

        // fixture 的 moveX/moveY 是反量化后的轴（±1），yaw/pitch 是弧度；这里映射回 StepCommand 的线上域。
        // v2 的脚本条目没有 seq，按 §7.2 自补（用该区间的末端 tick，单调即可）。
        private static StepCommand ToStepCommand(FixtureCommand command, int tick)
        {
            var step = default(StepCommand);
            step.Seq = (ushort)tick;
            step.MoveX = Quantize.QuantizeAxis(command.MoveX);
            step.MoveY = Quantize.QuantizeAxis(command.MoveY);
            step.Yaw = Quantize.QuantizeAngle(command.YawRad);
            step.Pitch = Quantize.QuantizeAngle(command.PitchRad);
            step.Buttons = command.Buttons;
            return step;
        }

        private struct FixtureCommand
        {
            public double MoveX;
            public double MoveY;
            public double YawRad;
            public double PitchRad;
            public byte Buttons;
        }

        private static FixtureCommand? FindCommand(JsonValue commands, ushort entityId)
        {
            if (commands == null || commands.Kind != JsonKind.Array) return null;
            for (var i = 0; i < commands.Count; i++)
            {
                var entry = commands[i];
                if (entry.Get("id").AsInt() != entityId) continue;
                var command = default(FixtureCommand);
                command.MoveX = entry.Get("moveX").AsDouble();
                command.MoveY = entry.Get("moveY").AsDouble();
                command.YawRad = entry.Get("yaw").AsDouble();
                command.PitchRad = entry.Get("pitch").AsDouble();
                command.Buttons = (byte)entry.Get("buttons").AsInt();
                return command;
            }
            return null;
        }

        // v2：实体状态只在关键帧里（keyframes[i].entities[]），形状与 v1 的 expected.entities 相同。
        private static MoveState? FindEntity(JsonValue keyframe, ushort entityId)
        {
            JsonValue entities;
            if (!keyframe.TryGet("entities", out entities)) return null;
            for (var i = 0; i < entities.Count; i++)
            {
                var entry = entities[i];
                if (entry.Get("id").AsInt() != entityId) continue;
                var pos = entry.Get("pos");
                if (pos == null || pos.Count < 3) return null;
                var state = default(MoveState);
                state.X = pos[0].AsDouble();
                state.Y = pos[1].AsDouble();
                state.Z = pos[2].AsDouble();
                state.YawRad = entry.Get("yaw").AsDouble();
                state.PitchRad = entry.Get("pitch").AsDouble();
                return state;
            }
            return null;
        }
    }
}
