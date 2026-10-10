namespace Ac.Sim
{
    // C06 §5(c) 的本地武器镜像：逐条复刻 `server/src/combat/weapon.cpp` 的 WeaponState 语义
    // （resetWeaponState / updateWeapon / tryFire / tryStartReload / switchSlot）。
    //
    // 它存在的唯一理由是**节奏**：权威弹匣只在 MatchState（1Hz）里，命中回执要一个 RTT；而
    // "按下去这一帧枪就响"必须当帧可见（枪口火焰、曳光、后坐、准星散布）。开火间隔、换弹时长、
    // 弹匣容量都来自 WeaponTable（＝服务端 kWeapons 的镜像），所以本地镜像的射速与服务端一致，
    // 不会出现"客户端突突突、服务端慢半拍"。
    //
    // 边界：这里**不做命中判定**（那是服务端的事，S08/S09 的射线与回溯），也不做伤害/击杀。
    // 按钮位的判读留在调用方（与 resolve.cpp 的分工一致：weapon.cpp 不认识 buttons）。
    public sealed class LocalWeapon
    {
        public const int SlotCount = WeaponTable.SlotCount;

        private readonly int[] _magInSlot = new int[SlotCount];
        private int _activeSlot;
        private int _reserveAmmo;
        private double _reloadEndsAtMs;
        private double _nextFireAllowedAtMs;
        private double _spreadDeg;
        // S16：换弹时长乘数（1.0 = 无升级）。服务端按 upgradeReloadTimeMultiplier(level) 起换弹，
        // 客户端镜像同一乘数，换弹进度条才不会比服务端快/慢半拍。
        private double _reloadTimeMultiplier = 1.0;

        public LocalWeapon()
        {
            Reset();
        }

        // resetWeaponState：满弹匣 + 初始备弹 + 无换弹无散布。
        public void Reset()
        {
            _activeSlot = 0;
            for (var slot = 0; slot < SlotCount; slot++) _magInSlot[slot] = WeaponTable.MagSize[slot];
            _reserveAmmo = WeaponTable.ReserveAmmoInitial;
            _reloadEndsAtMs = 0.0;
            _nextFireAllowedAtMs = 0.0;
            _spreadDeg = 0.0;
            _reloadTimeMultiplier = 1.0;
        }

        // S16：随 MatchState 的本地玩家升级段刷新换弹乘数（等级 0 = 1.0）。
        public void SetReloadTimeMultiplier(float multiplier)
        {
            _reloadTimeMultiplier = multiplier > 0f ? multiplier : 1f;
        }

        public int Slot { get { return _activeSlot; } }
        public int ActiveMag { get { return _magInSlot[_activeSlot]; } }
        public int Reserve { get { return _reserveAmmo; } }
        public double SpreadDeg { get { return _spreadDeg; } }
        public bool IsReloading { get { return _reloadEndsAtMs != 0.0; } }

        public double ReloadRemainingMs(double nowMs)
        {
            if (_reloadEndsAtMs == 0.0) return 0.0;
            var remaining = _reloadEndsAtMs - nowMs;
            return remaining > 0.0 ? remaining : 0.0;
        }

        // S16：本次换弹的总时长（含升级乘数），HUD 进度条的分母要跟它走，不能再用裸 ReloadMs。
        public double CurrentReloadMs
        {
            get { return WeaponTable.ReloadMs[WeaponTable.ClampSlot(_activeSlot)] * _reloadTimeMultiplier; }
        }

        // updateWeapon：换弹到点就补弹（从备弹取），散布在"距上次开火 ≥ kSpreadDecayDelayMs"后按
        // kSpreadDecayPerSecondDeg 衰减。返回本帧是否刚好补完弹（调用方据此重挂弹匣视图）。
        public bool Update(double nowMs, double dtMs, float fireRateMultiplier)
        {
            var reloadFinished = false;
            if (_reloadEndsAtMs != 0.0 && nowMs >= _reloadEndsAtMs)
            {
                var need = WeaponTable.MagSizeOf(_activeSlot) - _magInSlot[_activeSlot];
                var take = need < _reserveAmmo ? need : _reserveAmmo;
                _magInSlot[_activeSlot] += take;
                _reserveAmmo -= take;
                _reloadEndsAtMs = 0.0;
                reloadFinished = true;
            }

            if (_spreadDeg > 0.0)
            {
                var interval = WeaponTable.FireIntervalMs(_activeSlot, fireRateMultiplier);
                var lastShotAtMs = _nextFireAllowedAtMs - interval;
                if (nowMs - lastShotAtMs >= WeaponTable.SpreadDecayDelayMs)
                {
                    var decayed = _spreadDeg - (WeaponTable.SpreadDecayPerSecondDeg * dtMs) / 1000.0;
                    _spreadDeg = decayed > 0.0 ? decayed : 0.0;
                }
            }
            return reloadFinished;
        }

        // tryFire：换弹中 / 未到射速间隔 / 空弹匣一律不响。响一次就扣一发、推进射速间隔、涨散布。
        public bool TryFire(double nowMs, float fireRateMultiplier)
        {
            if (_reloadEndsAtMs != 0.0) return false;
            if (nowMs < _nextFireAllowedAtMs) return false;
            if (_magInSlot[_activeSlot] <= 0) return false;
            _magInSlot[_activeSlot] -= 1;
            _nextFireAllowedAtMs = nowMs + WeaponTable.FireIntervalMs(_activeSlot, fireRateMultiplier);
            var grown = _spreadDeg + WeaponTable.SpreadGrowthPerShotDeg;
            _spreadDeg = grown < WeaponTable.SpreadMaxDeg ? grown : WeaponTable.SpreadMaxDeg;
            return true;
        }

        public bool TryStartReload(double nowMs)
        {
            if (_reloadEndsAtMs != 0.0) return false;
            if (_magInSlot[_activeSlot] >= WeaponTable.MagSizeOf(_activeSlot)) return false;
            if (_reserveAmmo <= 0) return false;
            _reloadEndsAtMs = nowMs + WeaponTable.ReloadMs[WeaponTable.ClampSlot(_activeSlot)] * _reloadTimeMultiplier;
            return true;
        }

        public bool SwitchSlot(byte slot, double nowMs)
        {
            if (slot >= SlotCount) return false;
            if (slot == _activeSlot) return false;
            _activeSlot = slot;
            _reloadEndsAtMs = 0.0;
            if (_nextFireAllowedAtMs < nowMs) _nextFireAllowedAtMs = nowMs;
            return true;
        }

        // Apply each newly received authority once, never the cached HUD sample.
        // MatchState has no command ack: this is a coarse correction, not command replay.
        public void SyncAuthority(int mag, int reserve, int slot, int reloadLeft10Ms = 0, double nowMs = 0.0)
        {
            var clamped = WeaponTable.ClampSlot(slot);
            if (clamped != _activeSlot) SwitchSlot((byte)clamped, nowMs);
            var capacity = WeaponTable.MagSizeOf(_activeSlot);
            _magInSlot[_activeSlot] = mag < 0 ? 0 : (mag > capacity ? capacity : mag);
            _reserveAmmo = reserve < 0 ? 0 : reserve;
            _reloadEndsAtMs = reloadLeft10Ms > 0 ? nowMs + reloadLeft10Ms * 10.0 : 0.0;
        }
    }
}
