function Test-SelfTestResult([string[]]$Lines) {
    $starts = @($Lines | Where-Object { $_ -match "^SELFTEST START cases=([0-9]+)$" })
    $ends = @($Lines | Where-Object { $_ -match "^SELFTEST OK cases=([0-9]+)$" })
    if ($starts.Count -ne 1 -or $ends.Count -ne 1) { return $false }
    $registered = [int]($starts[0] -replace "^SELFTEST START cases=", "")
    $executed = [int]($ends[0] -replace "^SELFTEST OK cases=", "")
    $passes = @($Lines | Where-Object { $_ -match "^PASS " })
    $unique = @($passes | Sort-Object -Unique)
    return $registered -gt 0 -and $registered -eq $executed -and $passes.Count -eq $executed -and $unique.Count -eq $executed -and @($Lines | Where-Object { $_ -match "^FAIL |^SELFTEST FAIL " }).Count -eq 0
}
