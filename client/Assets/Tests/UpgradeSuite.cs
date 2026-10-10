using System;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using UnityEngine;

namespace Ac.Tests
{
    // S16：波间升级 + 弹药补给箱的纯逻辑层用例。
    //  升级表镜像（server/src/config/upgrades.hpp 的跨语言冻结值）、面板模型的乐观购买、
    //  UpgradeSelect 编解码、MatchState 升级段解码、ScreenFlow 喂给面板的路径。
    //  Unity 对象与渲染（箱子池 / 升级面板 IMGUI）在 BootSuite / RenderSuite / PresentationSuite 里测。
    public static class UpgradeSuite
    {
        public static void Add(string id, System.Action body) { SelfTest.Add(id, body); }

        public static void Register()
        {
            Add("upgrade.table_mirror", ChecksTableMirror);
            Add("upgrade.model_buy", ChecksModelBuy);
            Add("upgrade.codec_bounds", ChecksCodecBounds);
            Add("upgrade.matchstate_decode", ChecksMatchStateDecode);
            Add("upgrade.screenflow_feed", ChecksScreenFlowFeed);
            Add("upgrade.schema_consistency", ChecksSchemaConsistency);
        }

        private static void ChecksTableMirror()
        {
            SelfTest.Equal(4, (long)UpgradeTable.Count);
            SelfTest.Equal(5, (long)UpgradeTable.MaxLevel);
            SelfTest.Equal(1, (long)UpgradeTable.PointsPerWaveClear);
            // 与服务端 upgrades.hpp 每级增量逐条对齐（伤害 +15% / 移速 +8% / 换弹 -12% / 备弹 +40）。
            Near(0.15f, UpgradeTable.DamagePerLevel, "伤害每级 +15%");
            Near(0.08f, UpgradeTable.SpeedPerLevel, "移速每级 +8%");
            Near(0.12f, UpgradeTable.ReloadTimePerLevel, "换弹每级 -12%");
            SelfTest.Equal(40, (long)UpgradeTable.ReservePerLevel);
            // 等级 0 必须严格等于 1.0 / 0（升级前数字不能动，见 ADR-010 位精确：x*1.0==x）。
            Near(1f, UpgradeTable.DamageMultiplier(0), "等级 0 伤害乘数恒等");
            Near(1f, UpgradeTable.SpeedMultiplier(0), "等级 0 移速乘数恒等");
            Near(1f, UpgradeTable.ReloadTimeMultiplier(0), "等级 0 换弹乘数恒等");
            SelfTest.Equal(0, (long)UpgradeTable.ReserveBonus(0));
            // 满级数值（服务端同一条公式）。
            Near(1.75f, UpgradeTable.DamageMultiplier(5), "满级伤害 1 + 0.15*5");   // 1 + 0.15*5
            Near(1.4f, UpgradeTable.SpeedMultiplier(5), "满级移速 1 + 0.08*5");     // 1 + 0.08*5
            Near(0.4f, UpgradeTable.ReloadTimeMultiplier(5), "满级换弹 max(0.4, 1-0.12*5)");// max(0.4, 1-0.12*5)
            SelfTest.Equal(200, (long)UpgradeTable.ReserveBonus(5));   // 40*5
            // 越界等级钳到 [0,5]，与 ClampLevel 同语义（客户端镜像只用于展示/预判，裁决在服务端）。
            Near(1f, UpgradeTable.DamageMultiplier(-1), "负等级钳 0");
            Near(1.75f, UpgradeTable.DamageMultiplier(99), "超上限钳 5");
            // 换弹下限不为 0。
            Near(0.4f, UpgradeTable.ReloadTimeMultiplier(99), "换弹下限 0.4");
        }

        // float 断言助手：SelfTest 只有位级/整型比较，浮点镜像值用 1e-4 容差（公式两侧同源，误差只在编译期常量舍入）。
        private static void Near(float expected, float actual, string what)
        {
            SelfTest.True(Math.Abs(actual - expected) < 0.0001f, what + " 应为 " + expected.ToString("R"), actual.ToString("R"));
        }

        private static MatchStatePlayer SelfWith(int points, int damage, int speed, int reload, int reserve)
        {
            var player = default(MatchStatePlayer);
            player.Pid = 1;
            player.Name = "self";
            player.UpgradePoints = (byte)points;
            player.UpgradeDamage = (byte)damage;
            player.UpgradeSpeed = (byte)speed;
            player.UpgradeReload = (byte)reload;
            player.UpgradeReserve = (byte)reserve;
            return player;
        }

        private static void ChecksModelBuy()
        {
            var model = new UpgradeModel();
            // 未 Apply 前不可见、不可买。
            SelfTest.True(!model.Visible, "未 Apply 前不可见", "可见");
            SelfTest.Equal(-1L, (long)model.TryBuy(UpgradeTable.Damage));
            // 1 点 → 只能买 1 张。
            model.Apply(SelfWith(1, 0, 0, 0, 0));
            SelfTest.True(model.Visible, "Apply 后可买", "不可见");
            SelfTest.Equal(1L, (long)model.Points);
            SelfTest.Equal(0L, (long)model.LevelOf(UpgradeTable.Damage));
            var events = 0;
            var lastId = -1;
            model.OnUpgrade += delegate(int id) { events += 1; lastId = id; };
            SelfTest.Equal((long)UpgradeTable.Damage, (long)model.TryBuy(UpgradeTable.Damage));
            SelfTest.Equal(1L, (long)events);
            SelfTest.Equal((long)UpgradeTable.Damage, (long)lastId);
            SelfTest.Equal(0L, (long)model.Points);
            SelfTest.Equal(1L, (long)model.LevelOf(UpgradeTable.Damage));
            // 没点了 → 买不到、也不触发事件。
            var eventsBefore = events;
            SelfTest.Equal(-1L, (long)model.TryBuy(UpgradeTable.Damage));
            SelfTest.Equal((long)eventsBefore, (long)events);
            // 满级不可再买。
            model.Apply(SelfWith(1, 5, 0, 0, 0));
            SelfTest.Equal(5L, (long)model.LevelOf(UpgradeTable.Damage));
            SelfTest.True(!model.CanAfford(UpgradeTable.Damage), "满级不可买", "还能买");
            SelfTest.Equal(-1L, (long)model.TryBuy(UpgradeTable.Damage));
            SelfTest.Equal(1L, (long)model.Points);
            // 另一张卡不受影响。
            SelfTest.True(model.CanAfford(UpgradeTable.Speed), "同一点数还能买别的卡", "买不了");
            // Hide 恢复。
            model.Hide();
            SelfTest.True(!model.Visible, "Hide 后不可见", "可见");
            SelfTest.Equal(0, (long)model.Points);
            // Apply 覆盖式刷新（权威 MatchState 每 1Hz 拉回）。
            model.Apply(SelfWith(3, 2, 1, 0, 4));
            SelfTest.Equal(3, (long)model.Points);
            SelfTest.Equal(2, (long)model.LevelOf(UpgradeTable.Damage));
            SelfTest.Equal(1, (long)model.LevelOf(UpgradeTable.Speed));
            SelfTest.Equal(0, (long)model.LevelOf(UpgradeTable.Reload));
            SelfTest.Equal(4, (long)model.LevelOf(UpgradeTable.Reserve));
        }

        private static void ChecksCodecBounds()
        {
            // 合法 id 全过；越界 id 全拒（与服务端 upgradeId<4 的校验一致）。
            for (var id = 0; id < UpgradeTable.Count; id++)
            {
                byte[] payload;
                SelfTest.True(UpgradeCodec.TryEncode((byte)id, out payload), "id " + id + " 可编码", "被拒");
                SelfTest.Equal(1, payload.Length);
                SelfTest.Equal((long)id, (long)payload[0]);
            }
            byte[] rejected;
            SelfTest.True(!UpgradeCodec.TryEncode((byte)UpgradeTable.Count, out rejected), "id=4 越界被拒", "编码成功");
            SelfTest.True(!UpgradeCodec.TryEncode(255, out rejected), "id=255 越界被拒", "编码成功");
        }

        private static void ChecksMatchStateDecode()
        {
            // 升级段全 0（新档）→ 面板全 0。
            MatchStatePayload state;
            SelfTest.Equal((long)DecodeFailure.Ok, (long)MatchStateCodec.Decode(EncodeMatchState(0, 0, 0, 0, 0), out state));
            SelfTest.Equal(0, (long)state.Players[0].UpgradePoints);
            SelfTest.Equal(0, (long)state.Players[0].UpgradeDamage);
            // 一次发 5 点、4 张卡各满/部分等级 → 逐字段解出。
            SelfTest.Equal((long)DecodeFailure.Ok,
                (long)MatchStateCodec.Decode(EncodeMatchState(5, 3, 2, 1, 4), out state));
            SelfTest.Equal(5, (long)state.Players[0].UpgradePoints);
            SelfTest.Equal(3, (long)state.Players[0].UpgradeDamage);
            SelfTest.Equal(2, (long)state.Players[0].UpgradeSpeed);
            SelfTest.Equal(1, (long)state.Players[0].UpgradeReload);
            SelfTest.Equal(4, (long)state.Players[0].UpgradeReserve);
            // 等级超过服务端上限(5) → 整包 BadValue（防线在解码器，不把脏值放进模型）。
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(EncodeMatchState(1, 0, 6, 0, 0), out state));
            SelfTest.Equal((long)DecodeFailure.BadValue,
                (long)MatchStateCodec.Decode(EncodeMatchState(1, 0, 0, 0, 9), out state));
        }

        private static void ChecksScreenFlowFeed()
        {
            var flow = new ScreenFlow();
            var state = default(MatchStatePayload);
            state.Phase = Hud.PhaseIntermission;
            state.Wave = 3;   // 波间显隐要求 wave >= 1（与 Intermission.Apply 同判据）
            state.Players = new[] { SelfWith(2, 0, 1, 0, 0), SelfWith(2, 0, 0, 0, 0) };
            flow.Apply(state, 1);
            SelfTest.True(flow.IntermissionVisible, "波间可见", "不可见");
            SelfTest.True(flow.Upgrades.Visible, "升级面板可见", "不可见");
            SelfTest.Equal(2, (long)flow.Upgrades.Points);
            SelfTest.Equal(1, (long)flow.Upgrades.LevelOf(UpgradeTable.Speed));
            // 表中没有自己 → 面板隐藏。
            flow.Apply(state, 7);
            SelfTest.True(!flow.Upgrades.Visible, "表里没有自己时隐藏", "还可见");
            // 离开波间（打下一波）面板仍可见但点数照常拉回；phase 只是驱动显隐，
            // 面板可见性本身由 self 行存在与否决定（渲染器还要求 IntermissionVisible 才画）。
            state.Phase = Hud.PhasePlaying;
            state.Players[0].UpgradePoints = 0;
            flow.Apply(state, 1);
            SelfTest.True(flow.Upgrades.Points == 0, "权威 0 点覆盖乐观扣点", flow.Upgrades.Points.ToString());
        }

        // 造一条只有 1 名玩家（pid=1）的 MatchState 载荷，升级段按参数填（与 BootSuite 的写器同布局）。
        private static byte[] EncodeMatchState(byte points, byte damage, byte speed, byte reload, byte reserve)
        {
            var bytes = new System.Collections.Generic.List<byte>(48);
            bytes.Add(Hud.PhaseIntermission);   // phase
            bytes.Add(1);                       // wave
            bytes.Add(0); bytes.Add(0);         // intermissionMs (u16)
            bytes.Add(1);                       // 1 名玩家
            bytes.Add(1); bytes.Add(0);         // pid u16 = 1
            bytes.Add(1); bytes.Add((byte)'a'); // nameLen + 'a'
            bytes.Add(1);                       // ready
            bytes.Add(1);                       // weapon
            bytes.Add(200);                     // hpRatio
            bytes.Add(0); bytes.Add(0);         // kills u16
            bytes.Add(30);                      // mag
            bytes.Add(120); bytes.Add(0);       // reserve u16
            bytes.Add(0);                       // reloadLeft10Ms
            bytes.Add(0);                       // rage
            bytes.Add(0);                       // rageLeft100Ms（u8，与服务端布局一致）
            bytes.Add(0);                       // downed
            bytes.Add(0);                       // reviveRatio255
            bytes.Add(points);
            bytes.Add(damage);
            bytes.Add(speed);
            bytes.Add(reload);
            bytes.Add(reserve);
            bytes.Add(1); bytes.Add(0);         // localPid u16 = 1
            return bytes.ToArray();
        }

        // S16 schema 一致性：两侧对"该镜像的值"是同一条线（防一边改了另一边忘）。
        private static void ChecksSchemaConsistency()
        {
            SelfTest.Equal((long)UpgradeTable.Count, (long)UpgradeModel.Count);
            SelfTest.Equal((long)UpgradeTable.MaxLevel, (long)UpgradeModel.MaxLevel);
            // 面板卡片文案顺序必须等于线上 upgradeId 顺序（画图按数组下标取文案）。
            SelfTest.True(UpgradeModel.Names.Length == UpgradeTable.Count && UpgradeModel.Effects.Length == UpgradeTable.Count,
                "卡片文案数组长度 = 升级种类数", UpgradeModel.Names.Length.ToString());
        }
    }
}
