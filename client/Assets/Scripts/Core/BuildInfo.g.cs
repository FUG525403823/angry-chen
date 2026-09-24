// 由 client/build.ps1 在出包时覆盖（扫描时写入 commit 与构建时间）。
// 仓库里保留占位值，保证"没构建过"也能编译；VersionInfo 不接受运行时手改。
namespace Ac.Core
{
    internal static class BuildInfo
    {
        internal const string Commit = "unknown";
        internal const string BuildTimeUtc = "1970-01-01T00:00:00Z";
    }
}
