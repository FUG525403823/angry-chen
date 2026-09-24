# C01 §5.5 零素材静态校验：扩展名黑名单 + 前 8192 字节 NUL 嗅探。
# 通过：打印 OK：client 零外部素材，退出 0；命中：逐行打印 BANNED <仓库相对路径>，退出 1。
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$clientRoot = Split-Path -Parent $PSScriptRoot
$scanRoots = @((Join-Path $clientRoot 'Assets'), (Join-Path $clientRoot 'Packages'))
$excludedDirs = @('Library', 'Temp', 'obj', 'Logs', 'Build', 'Builds', 'UserSettings')

# 与 tools/check-assets.mjs 的 BANNED_ASSET 逐字相同：任何增删都要同步 mjs 与 C01 §5.5
$bannedExtensions = @(
    '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.tga', '.psd', '.webp', '.ico', '.svg',
    '.wav', '.mp3', '.ogg', '.m4a', '.aiff', '.flac', '.ttf', '.otf', '.woff', '.woff2',
    '.fbx', '.obj', '.gltf', '.glb', '.blend', '.dae', '.mp4', '.mov', '.webm',
    '.unitypackage', '.assetbundle', '.dll', '.so', '.dylib', '.zip', '.7z', '.rar'
)

$hits = @()
foreach ($root in $scanRoots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File -Force)) {
        $relative = $file.FullName.Substring($clientRoot.Length + 1).Replace([char]92, '/')
        $skipped = $false
        foreach ($part in $relative.Split('/')) {
            if ($excludedDirs -contains $part) { $skipped = $true; break }
        }
        if ($skipped) { continue }

        if ($bannedExtensions -contains $file.Extension.ToLowerInvariant()) {
            $hits += 'BANNED client/' + $relative
            continue
        }
        $head = @(Get-Content -LiteralPath $file.FullName -Encoding Byte -TotalCount 8192 -ErrorAction SilentlyContinue)
        if ($head -contains 0) { $hits += 'BANNED client/' + $relative }
    }
}

if ($hits.Count -gt 0) {
    foreach ($hit in $hits) { Write-Output $hit }
    exit 1
}
Write-Output 'OK：client 零外部素材'
exit 0
