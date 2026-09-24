using System.IO;
using UnityEngine;

namespace Ac.Sim
{
    // 共享产物（角度表、对拍 fixture）都住在仓库根下。编辑器里工程根是 client/，所以往上退一级；
    // 手工批处理的工作目录可能已经是仓库根，两处候选都试一遍——定位规则只写这一份。
    public static class RepoPaths
    {
        public static string UnderRepoRoot(string relativePath)
        {
            if (!string.IsNullOrEmpty(Application.dataPath))
            {
                var dataParent = Path.GetDirectoryName(Application.dataPath);
                if (!string.IsNullOrEmpty(dataParent))
                {
                    return Path.GetFullPath(Path.Combine(dataParent, "..", relativePath));
                }
            }
            return Path.GetFullPath(relativePath);
        }

        public static string Locate(string relativePath)
        {
            var underRepo = UnderRepoRoot(relativePath);
            if (File.Exists(underRepo) || Directory.Exists(underRepo)) return underRepo;
            var fromWorkingDirectory = Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), relativePath));
            if (File.Exists(fromWorkingDirectory) || Directory.Exists(fromWorkingDirectory)) return fromWorkingDirectory;
            return underRepo;
        }
    }
}
