using System;
using System.Collections.Generic;
using System.IO;
using Ac.Core;
using Ac.Sim;

namespace Ac.Tests
{
    // C06 §5(e)：按 tools/export-fixtures.mjs --list 的清单逐份加载对拍向量，逐位复现本机实体的每 tick 状态。
    // 清单从目录推导：只接受带 ticks 数组的 JSON（trig-table.json 因此被排除），并在 manifest 用例里断言覆盖数。
    public static class FixturePredictSuite
    {
        // 路径定位复用 C02 的 FixtureLoader（仓内只有这一份规则）
        private static readonly string FixtureDir = FixtureLoader.DefaultDirectory();
        private static readonly List<string> Unreadable = new List<string>();
        private const ushort LocalEntityId = 1;   // fixture 里的本机陈sir
        // §5(e) 覆盖下限里客户端能覆盖的三类（静止/直线/碰撞）；"连射命中"类向量尚未由服务端导出
        private static readonly string[] ExpectedNames = { "still-60t", "straight-line-240t", "barn-collision-400t", "fence-bounds-400t" };

        public static void Register()
        {
            var paths = Discover();
            SelfTest.Add("fixture_predict.manifest", delegate { ChecksManifest(paths); });
            for (var i = 0; i < paths.Count; i++)
            {
                var path = paths[i];
                SelfTest.Add("fixture_predict." + Path.GetFileNameWithoutExtension(path), delegate { ChecksFixture(path); });
            }
        }

        private static List<string> Discover()
        {
            var found = new List<string>();
            if (!Directory.Exists(FixtureDir)) return found;
            var files = Directory.GetFiles(FixtureDir, "*.json");
            Array.Sort(files, StringComparer.Ordinal);
            for (var i = 0; i < files.Length; i++)
            {
                try
                {
                    var root = MiniJson.Parse(File.ReadAllText(files[i]));
                    if (root.TryGet("ticks", out JsonValue ticks) && ticks.Count > 0) found.Add(files[i]);
                }
                catch (Exception error)
                {
                    // 不是对拍向量（或损坏）：注册期不抛，但要让 manifest 用例看见，否则"新向量自动纳入"会静默失效
                    Unreadable.Add(Path.GetFileName(files[i]) + ":" + error.GetType().Name);
                }
            }
            return found;
        }

        private static void ChecksManifest(List<string> paths)
        {
            SelfTest.True(Directory.Exists(FixtureDir), "对拍向量目录存在", FixtureDir);
            SelfTest.Equal(0, Unreadable.Count);
            if (Unreadable.Count > 0) SelfTest.Fail("fixture 解析失败：" + string.Join(",", Unreadable.ToArray()));
            // §5(e) 覆盖下限的四类：仓库内当前是静止/直线/谷仓碰撞/栅栏边界（清单由 --list 给出）
            for (var i = 0; i < ExpectedNames.Length; i++)
            {
                var found = false;
                for (var j = 0; j < paths.Count; j++) if (paths[j].EndsWith(ExpectedNames[i] + ".json", StringComparison.Ordinal)) found = true;
                SelfTest.True(found, "缺少向量 " + ExpectedNames[i], string.Join(",", paths.ToArray()));
            }
            for (var i = 0; i < paths.Count; i++) SelfTest.True(!paths[i].EndsWith("trig-table.json", StringComparison.Ordinal), "trig-table 不是对拍向量", paths[i]);
        }

        private static void ChecksFixture(string path)
        {
            var name = Path.GetFileNameWithoutExtension(path);
            var root = MiniJson.Parse(File.ReadAllText(path));
            JsonValue ticks;
            if (!root.TryGet("ticks", out ticks)) { SelfTest.Fail(name + ": 缺少 ticks"); return; }
            var config = MoveConfig.Default();
            var predictor = new Predictor();
            var buffer = new CommandBuffer();
            var compared = 0;
            for (var t = 1; t < ticks.Count; t++)
            {
                var command = FindCommand(ticks[t], LocalEntityId);
                if (!command.HasValue) continue;
                if (ticks[t].Get("dtMs").AsInt() != (int)LocalStep.StepDtMs) { SelfTest.Fail(name + ": tick " + t + " dtMs != " + LocalStep.StepDtMs); return; }

                var seed = FindEntity(ticks[t - 1], LocalEntityId);
                if (!seed.HasValue) { SelfTest.Fail(name + ": tick " + (t - 1) + " 缺少本机实体"); return; }
                var expected = FindEntity(ticks[t], LocalEntityId);
                if (!expected.HasValue) { SelfTest.Fail(name + ": tick " + t + " 缺少本机实体"); return; }

                var step = ToStepCommand(command.Value);
                // 直接步进：从上一 tick 的权威状态出发走一步，必须与 fixture 逐位相同
                var state = seed.Value;
                LocalStep.StepLocalPlayer(ref state, step, config, LocalStep.StepDtMs);
                if (!Compare(name, t, state, expected.Value)) return;

                // 预测路径（CommandBuffer + Predictor）走同一条命令也必须逐位一致
                buffer.Push(step);
                predictor.SetAuthoritative(seed.Value.X, seed.Value.Y, seed.Value.Z,
                    command.Value.YawUnits, command.Value.PitchUnits);
                var replayed = predictor.StepWith(step);
                if (!replayed) { SelfTest.Fail(name + ": tick " + t + " 预测步进被拒"); return; }
                if (!Compare(name, t, predictor.State, expected.Value)) return;
                compared += 1;
            }
            SelfTest.True(compared > 0, name + " 至少比较一个 tick", compared.ToString());
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
        private static StepCommand ToStepCommand(FixtureCommand command)
        {
            var step = default(StepCommand);
            step.Seq = command.Seq;
            step.MoveX = Quantize.QuantizeAxis(command.MoveX);
            step.MoveY = Quantize.QuantizeAxis(command.MoveY);
            step.Yaw = Quantize.QuantizeAngle(command.YawRad);
            step.Pitch = Quantize.QuantizeAngle(command.PitchRad);
            step.Buttons = command.Buttons;
            return step;
        }

        private struct FixtureCommand
        {
            public ushort Seq;
            public uint ClientTick;
            public ushort YawUnits;
            public ushort PitchUnits;
            public double MoveX;
            public double MoveY;
            public double YawRad;
            public double PitchRad;
            public byte Buttons;
        }

        private static FixtureCommand? FindCommand(JsonValue tick, ushort entityId)
        {
            JsonValue commands;
            if (!tick.TryGet("commands", out commands)) return null;
            for (var i = 0; i < commands.Count; i++)
            {
                var entry = commands[i];
                if (entry.Get("id").AsInt() != entityId) continue;
                var command = default(FixtureCommand);
                command.Seq = (ushort)entry.Get("seq").AsInt();
                command.ClientTick = (uint)entry.Get("clientTick").AsLong();
                command.MoveX = entry.Get("moveX").AsDouble();
                command.MoveY = entry.Get("moveY").AsDouble();
                command.YawRad = entry.Get("yaw").AsDouble();
                command.PitchRad = entry.Get("pitch").AsDouble();
                command.Buttons = (byte)entry.Get("buttons").AsInt();
                command.YawUnits = Quantize.QuantizeAngle(command.YawRad);
                command.PitchUnits = Quantize.QuantizeAngle(command.PitchRad);
                return command;
            }
            return null;
        }

        private static MoveState? FindEntity(JsonValue tick, ushort entityId)
        {
            JsonValue expected;
            if (!tick.TryGet("expected", out expected)) return null;
            JsonValue entities;
            if (!expected.TryGet("entities", out entities)) return null;
            for (var i = 0; i < entities.Count; i++)
            {
                var entry = entities[i];
                if (entry.Get("id").AsInt() != entityId) continue;
                var pos = entry.Get("pos");
                if (pos.Count < 3) return null;
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
