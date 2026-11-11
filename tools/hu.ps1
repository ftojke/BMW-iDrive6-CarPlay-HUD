<#
.SYNOPSIS
  Runs one command on the NBT Evo head unit over SSH (plink), without typing the password.

.DESCRIPTION
  Password is read from %USERPROFILE%\.bmw-hu\root.pw (ACL: current user only).
  Host keys are pinned in %USERPROFILE%\.bmw-hu\hostkey_<ip>.txt; the first
  connection to a board saves its fingerprint (trust on first use).
  Project rule: read-only commands only unless the user explicitly approves a write.

.EXAMPLE
  .\tools\hu.ps1 'uname -a'
  .\tools\hu.ps1 -Board jacinto 'pidin info'
  .\tools\hu.ps1 'cat /fs/sda0/opt/mm/etc/pf_miracast_9873.conf'
  .\tools\hu.ps1 'cat /fs/sda0/opt/conn/lib/libmiracastservice.so' -OutFile .\hu_snapshot\mirror\libmiracastservice.so
  .\tools\hu.ps1 'cat > /tmp/nbt-aad' -InFile .\android_auto\install_v1.8.39\nbt-aad   # upload (a write: needs approval)
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Command,
    [ValidateSet('omap', 'jacinto')][string]$Board = 'omap',
    [string]$OutFile,
    [string]$InFile,
    [int]$TimeoutSec = 60,
    [switch]$DryRun,
    [string]$Target,
    [string]$PasswordFile
)

$ErrorActionPreference = 'Stop'
$boards = @{ omap = '169.254.199.119'; jacinto = '169.254.199.99' }
$cfgDir = Join-Path $env:USERPROFILE '.bmw-hu'
$plink = Join-Path $env:LOCALAPPDATA 'Programs\PuTTY\plink.exe'
if (-not $Target) { $Target = 'root@' + $boards[$Board] }
if (-not $PasswordFile) { $PasswordFile = Join-Path $cfgDir 'root.pw' }
$hostPart = ($Target -split '@')[-1]
$keyFile = Join-Path $cfgDir ('hostkey_{0}.txt' -f ($hostPart -replace '[^A-Za-z0-9.\-]', '_'))

foreach ($p in @($plink, $PasswordFile)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Missing: $p" }
}
if ($InFile) {
    if (-not (Test-Path -LiteralPath $InFile -PathType Leaf)) { throw "Missing: $InFile" }
    $InFile = (Resolve-Path -LiteralPath $InFile).ProviderPath
}

function ConvertTo-WinArg([string]$a) {
    if ($a.Length -gt 0 -and $a -notmatch '[\s"]') { return $a }
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    $bs = 0
    foreach ($ch in $a.ToCharArray()) {
        if ($ch -eq [char]'\') { $bs++; continue }
        if ($ch -eq [char]'"') {
            [void]$sb.Append((New-Object string ([char]'\'), ($bs * 2 + 1))).Append('"')
        } else {
            if ($bs) { [void]$sb.Append((New-Object string ([char]'\'), $bs)) }
            [void]$sb.Append($ch)
        }
        $bs = 0
    }
    if ($bs) { [void]$sb.Append((New-Object string ([char]'\'), ($bs * 2))) }
    [void]$sb.Append('"')
    return $sb.ToString()
}

function Invoke-Plink([string[]]$argv, [string]$outPath, [int]$timeoutMs, [string]$inPath) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $plink
    $psi.Arguments = (($argv | ForEach-Object { ConvertTo-WinArg $_ }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $psi.StandardOutputEncoding = $utf8
    $psi.StandardErrorEncoding = $utf8
    $proc = [System.Diagnostics.Process]::Start($psi)
    $errTask = $proc.StandardError.ReadToEndAsync()
    $fs = $null
    if ($outPath) {
        $fs = [System.IO.File]::Create($outPath)
        $outTask = $proc.StandardOutput.BaseStream.CopyToAsync($fs)
    } else {
        $outTask = $proc.StandardOutput.ReadToEndAsync()
    }
    $inError = $null
    if ($inPath) {
        # Raw bytes to plink's stdin (binary-safe); the remote command reads them, e.g. 'cat > /tmp/file'.
        # A stdin encoding with a byte-order mark would already have written it into the stream.
        if ($proc.StandardInput.Encoding.GetPreamble().Length -gt 0) {
            $inError = 'stdin encoding adds a byte-order mark; upload aborted'
            try { $proc.Kill() } catch { }
        } else {
            $src = [System.IO.File]::OpenRead($inPath)
            try {
                $src.CopyTo($proc.StandardInput.BaseStream)
                $proc.StandardInput.BaseStream.Flush()
            } catch {
                $inError = $_.Exception.Message
            } finally {
                $src.Close()
            }
        }
    }
    try { $proc.StandardInput.Close() } catch { }
    $timedOut = $false
    if (-not $proc.WaitForExit($timeoutMs)) {
        $timedOut = $true
        try { $proc.Kill() } catch { }
    }
    $proc.WaitForExit()
    $outTask.Wait()
    $stdout = $null
    if ($fs) { $fs.Close() } else { $stdout = $outTask.Result }
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Out      = $stdout
        Err      = $errTask.Result
        TimedOut = $timedOut
        InError  = $inError
    }
}

$baseArgs = @('-ssh', '-batch', '-noagent', '-pwfile', $PasswordFile)

$hostKey = $null
if (Test-Path -LiteralPath $keyFile) { $hostKey = ([IO.File]::ReadAllText($keyFile)).Trim() }

if ($DryRun) {
    $argv = $baseArgs
    if ($hostKey) { $argv += @('-hostkey', $hostKey) }
    $argv += @($Target, $Command)
    Write-Output ('"{0}" {1}' -f $plink, (($argv | ForEach-Object { ConvertTo-WinArg $_ }) -join ' '))
    if ($InFile) { Write-Output ('stdin <- {0} ({1} bytes)' -f $InFile, (Get-Item -LiteralPath $InFile).Length) }
    return
}

if (-not $hostKey) {
    # First contact: in -batch mode plink prints the fingerprint and stops before logging in.
    $probe = Invoke-Plink ($baseArgs + @($Target, 'exit')) $null 30000
    $m = [regex]::Match($probe.Err, 'SHA256:[A-Za-z0-9+/]{43}')
    if ($m.Success) {
        $hostKey = $m.Value
        [IO.File]::WriteAllText($keyFile, $hostKey, (New-Object System.Text.UTF8Encoding $false))
        [Console]::Error.WriteLine("First connection to ${hostPart}: pinned host key $hostKey")
    } elseif ($probe.ExitCode -ne 0) {
        throw ("Could not get host key for {0}:`n{1}" -f $hostPart, $probe.Err.Trim())
    }
}

$argv = $baseArgs
if ($hostKey) { $argv += @('-hostkey', $hostKey) }
$argv += @($Target, $Command)
$res = Invoke-Plink $argv $OutFile ($TimeoutSec * 1000) $InFile

if ($res.Out) { Write-Output $res.Out.TrimEnd("`r", "`n") }
if ($res.Err -and $res.Err.Trim()) { [Console]::Error.WriteLine($res.Err.TrimEnd()) }
if ($OutFile) { [Console]::Error.WriteLine(('Saved {0} bytes to {1}' -f (Get-Item -LiteralPath $OutFile).Length, $OutFile)) }
if ($InFile) {
    if ($res.InError) { [Console]::Error.WriteLine("Upload of $InFile failed: $($res.InError)") }
    else { [Console]::Error.WriteLine(('Sent {0} bytes from {1}' -f (Get-Item -LiteralPath $InFile).Length, $InFile)) }
}
if ($res.TimedOut) { [Console]::Error.WriteLine("Timed out after $TimeoutSec s (plink stopped).") }
if ($res.InError -and $res.ExitCode -eq 0) { exit 1 }
exit $res.ExitCode
