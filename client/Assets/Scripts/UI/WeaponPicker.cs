using System;

namespace Ac.UI
{
    // C12 §4 任务 3：三档备战选择，切换时只回调槽位号。
    public sealed class WeaponPicker
    {
        public const int SlotCount = 3;
        public const int DefaultSlot = Roster.WeaponPistol;   // 服务端加入对局默认槽位 0（room.cpp）

        public int Selected { get; private set; }
        public int ChangeCount { get; private set; }
        public int RejectedCount { get; private set; }

        public event Action<int> Changed;

        public WeaponPicker() { Selected = DefaultSlot; }

        public bool Select(int slot)
        {
            if (slot < 0 || slot >= SlotCount) { RejectedCount += 1; return false; }
            if (slot == Selected) return false;
            Selected = slot;
            ChangeCount += 1;
            var handler = Changed;
            if (handler != null) handler(slot);
            return true;
        }

        public string Label { get { return Roster.WeaponLabel(Selected); } }
    }
}
