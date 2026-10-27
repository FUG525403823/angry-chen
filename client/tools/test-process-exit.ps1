$ErrorActionPreference = "Stop"
foreach ($expected in @(0, 7)) {
    $child = Start-Process -FilePath "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -ArgumentList @("-NoProfile", "-Command", "exit $expected") -PassThru -NoNewWindow
    $null = $child.Handle
    $child.WaitForExit()
    $child.Refresh()
    if ($null -eq $child.ExitCode -or $child.ExitCode -ne $expected) { throw "expected exit $expected got $($child.ExitCode)" }
    Write-Output "PASS retained process exit $expected"
    $child.Dispose()
}
