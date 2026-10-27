. "$PSScriptRoot/lib/selftest-result.ps1"
$cases = @(
    @{Name="complete"; Pass=$true; Lines=@("SELFTEST START cases=1", "PASS a", "SELFTEST OK cases=1")},
    @{Name="empty"; Pass=$false; Lines=@("SELFTEST START cases=0", "SELFTEST OK cases=0")},
    @{Name="interrupted"; Pass=$false; Lines=@("SELFTEST START cases=1", "PASS a")},
    @{Name="count mismatch"; Pass=$false; Lines=@("SELFTEST START cases=2", "PASS a", "SELFTEST OK cases=1")},
    @{Name="missing execution"; Pass=$false; Lines=@("SELFTEST START cases=2", "PASS a", "SELFTEST OK cases=2")},
    @{Name="duplicate execution"; Pass=$false; Lines=@("SELFTEST START cases=2", "PASS a", "PASS a", "SELFTEST OK cases=2")},
    @{Name="multiple runs"; Pass=$false; Lines=@("SELFTEST START cases=1", "SELFTEST START cases=1", "PASS a", "SELFTEST OK cases=1")},
    @{Name="failure and ok"; Pass=$false; Lines=@("SELFTEST START cases=1", "PASS a", "FAIL b", "SELFTEST OK cases=1")}
)
foreach ($case in $cases) {
    if ((Test-SelfTestResult $case.Lines) -ne $case.Pass) { throw "contract failed: $($case.Name)" }
    Write-Output "PASS selftest result $($case.Name)"
}
