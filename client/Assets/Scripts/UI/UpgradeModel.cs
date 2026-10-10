using System;
using Ac.Net;

namespace Ac.UI
{
    // S16 波间升级面板的模型：显示与购买的纯逻辑，不引用 UnityEngine（与 Intermission/Lobby 同类）。
    // 数据唯一真相是 MatchState 里**本地玩家那一行**的升级段（points + 4 级）；本类只缓存它、
    // 按它算"能不能买"，购买动作是**乐观**的：点一下先在本地扣点/加级（面板立刻响应），
    // 上行 UpgradeSelect 由事件交给帧回路发，服务端裁决（点数/等级上限）通过下一帧 MatchState 拉回。
    public sealed class UpgradeModel
    {
        public const int Count = 4;   // kUpgradeCount
        public const int MaxLevel = 5;   // kUpgradeMaxLevel（与服务端同值，见 Sim.UpgradeTable）

        // 卡片的展示文案（顺序 = 线上 upgradeId：0 伤害 / 1 移速 / 2 换弹 / 3 备弹）。
        public static readonly string[] Names = { "伤害强化", "移速强化", "换弹加速", "备弹扩容" };
        public static readonly string[] Effects = { "每级 +15% 伤害", "每级 +8% 移速", "每级换弹 -12%", "备弹上限 +40" };

        private readonly int[] _levels = new int[Count];

        public event Action<int> OnUpgrade;   // 乐观购买成功 → 帧回路据此上行 UpgradeSelect

        public bool Visible { get; private set; }
        public int Points { get; private set; }
        public int ApplyCount { get; private set; }

        public int LevelOf(int upgradeId)
        {
            if (upgradeId < 0 || upgradeId >= Count) return 0;
            return _levels[upgradeId];
        }

        public bool CanAfford(int upgradeId)
        {
            if (upgradeId < 0 || upgradeId >= Count) return false;
            return Visible && Points > 0 && _levels[upgradeId] < MaxLevel;
        }

        public void Apply(MatchStatePlayer self)
        {
            ApplyCount += 1;
            Visible = true;
            Points = self.UpgradePoints;
            _levels[0] = self.UpgradeDamage;
            _levels[1] = self.UpgradeSpeed;
            _levels[2] = self.UpgradeReload;
            _levels[3] = self.UpgradeReserve;
        }

        public void Hide()
        {
            Visible = false;
            Points = 0;
        }

        // 乐观购买：可买则扣点加级并触发 OnUpgrade，返回 upgradeId；否则返回 -1（不产生任何副作用）。
        // 等级/点数越界不可买（服务端同样拒收）；购买失败由下一帧权威 MatchState 拉回。
        public int TryBuy(int upgradeId)
        {
            if (!CanAfford(upgradeId)) return -1;
            Points -= 1;
            _levels[upgradeId] += 1;
            var handler = OnUpgrade;
            if (handler != null) handler(upgradeId);
            return upgradeId;
        }
    }
}
