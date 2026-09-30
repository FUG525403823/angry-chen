namespace Ac.Net
{
    // 协议 2：身份只来自已通过 MatchStateCodec 校验的权威 localPid，与昵称无关。
    public sealed class LocalIdentity
    {
        public const ushort NoPid = 0;
        public ushort Pid { get; private set; }
        public int ResolveCount { get; private set; }
        public int MatchedCount { get; private set; }

        public bool Resolve(ushort localPid)
        {
            ResolveCount += 1;
            if (localPid == Pid) return false;
            Pid = localPid;
            if (localPid != NoPid) MatchedCount += 1;
            return true;
        }
    }
}
