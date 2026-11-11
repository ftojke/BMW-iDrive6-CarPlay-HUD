# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Host test runner for the CarPlay->HUD translation layer.
# Uses the portable w64devkit g++ and the strict QNX-compatible dialect (-std=gnu++98).
$ErrorActionPreference = 'Continue'
$base = Split-Path -Parent $MyInvocation.MyCommand.Path
$gpp  = Join-Path $base '..\_toolchain\w64devkit\bin\g++.exe'
if (-not (Test-Path $gpp)) { Write-Error "g++ not found at $gpp"; exit 2 }

$bin = Join-Path $base 'build'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
$flags = @('-std=gnu++98','-Wall','-Wextra','-Wno-long-long','-O1')
$tests = @('test_iap2_rgi','test_cp_maneuver_map','test_cp_rgi_state','test_ident_patch','test_cp_got_patch','test_cp_bridge')

$fail = 0
foreach ($tname in $tests) {
    $src = Join-Path $base "tests\$tname.cpp"
    $exe = Join-Path $bin "$tname.exe"
    $log = Join-Path $bin "$tname.build.log"
    Write-Host "=== compiling $tname"
    if ($tname -eq 'test_ident_patch') {
        & $gpp @flags $src (Join-Path $base 'hook\ident_patch.c') -o $exe 2> $log
    } elseif ($tname -eq 'test_cp_got_patch') {
        & $gpp @flags $src (Join-Path $base 'hook\cp_got_patch.c') -o $exe 2> $log
    } elseif ($tname -eq 'test_cp_bridge') {
        & $gpp @flags $src (Join-Path $base 'hook\cp_bridge.cpp') -o $exe 2> $log
    } else {
        & $gpp @flags $src -o $exe 2> $log
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  COMPILE FAILED:"; Get-Content $log | ForEach-Object { "    $_" }
        $fail = 1; continue
    }
    $warn = (Get-Content $log -ErrorAction SilentlyContinue | Where-Object { $_ -match 'warning:' }).Count
    & $exe
    if ($LASTEXITCODE -ne 0) { Write-Host "  TEST FAILED ($tname)"; $fail = 1 }
    if ($warn) { Write-Host "  ($warn warning line(s) in $tname.build.log)" }
}
Write-Host "=== OVERALL: $(if ($fail) { 'FAIL' } else { 'PASS' })"
exit $fail
