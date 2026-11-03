# Guest-side runner: runs the listed test exes and frames every line on COM1.
#   W32K begin <mode-list>        W32K mode <adm|std|user>
#   W32K userkind <real-user|unavailable>   (user mode only: a throwaway local standard account was/was not usable)
#   W32K start <exe>              W32K out <exe> | <line>
#   W32K rc <exe> <code|TIMEOUT> <secs>
#   W32K done                     (only when every exe was attempted)
# Parameters: -Payload <dir>  -Modes adm,std,user  -Timeout <secs per exe>  -Full (also send RUN lines)
param([string]$Payload, [string]$Modes = 'adm', [int]$Timeout = 300, [switch]$Full)
$ErrorActionPreference = 'Continue'
$port = New-Object System.IO.Ports.SerialPort 'COM1', 115200, 'None', 8, 'One'
$port.WriteTimeout = 30000
$port.Open()
function Send([string]$s) {
    $s = ($s -replace '[^\x20-\x7e]', '?')
    $port.Write($s + "`r`n")
}
$work = 'C:\w32kt'
$bin = Join-Path $work 'bin'; $res = Join-Path $work 'results'; $cwd = Join-Path $work 'cwd'
New-Item -ItemType Directory -Force -Path $bin, $res, $cwd | Out-Null
Copy-Item -Recurse -Force (Join-Path $Payload 'bin\*') $bin
& icacls $work /grant 'Users:(OI)(CI)M' /T /C /Q | Out-Null
$list = @(Get-Content (Join-Path $Payload 'list.txt') | Where-Object { $_.Trim() })
$filter = ''
if (Test-Path (Join-Path $Payload 'filter.txt')) { $filter = (Get-Content (Join-Path $Payload 'filter.txt') -Raw).Trim() }
# optional per-exe timeout floors ("<exe> <secs>" per line): slow areas keep their own limit, others use -Timeout
$tmo = @{}
if (Test-Path (Join-Path $Payload 'timeouts.txt')) {
    foreach ($l in (Get-Content (Join-Path $Payload 'timeouts.txt'))) { $f = $l.Trim() -split '\s+'; if ($f.Count -eq 2) { $tmo[$f[0]] = [int]$f[1] } }
}
Send ("W32K begin modes=$Modes exes=$($list.Count) timeout=$Timeout")
$os = (Get-CimInstance Win32_OperatingSystem)
Send ("W32K os build=$($os.BuildNumber) version=$($os.Version) sku=$($os.OperatingSystemSKU) arch=$($os.OSArchitecture)")

# user mode: a throwaway local standard account (Users group only), random password, created here in the guest.
$uName = 'testuser1'; $uPass = $null; $uReal = $false
function New-StdAccount {
    $chars = 'abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789'
    $script:uPass = 'Aa1-' + (-join (1..20 | ForEach-Object { $chars[(Get-Random -Maximum $chars.Length)] }))
    & net user $uName /delete 2>&1 | Out-Null
    & net user $uName $script:uPass /add /y /expires:never 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Send "W32K userdiag | net user /add failed rc=$LASTEXITCODE"; return $false }
    & net user $uName /passwordchg:no 2>&1 | Out-Null
    # verify the launch really gives a separate, non-elevated account in the interactive session
    $of = Join-Path $cwd 'userprobe.txt'; Remove-Item $of -ErrorAction SilentlyContinue
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "$env:ComSpec"
    $psi.Arguments = "/c `"`"$bin\w32k_runas.exe`" --user $uName --pass $($script:uPass) `"$bin\w32k_probe.exe`" --no-whoami > `"$of`" 2>&1 < nul`""
    $psi.WorkingDirectory = $cwd; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $p = [Diagnostics.Process]::Start($psi)
    if (-not $p.WaitForExit(90000)) { & taskkill /F /T /PID $p.Id 2>&1 | Out-Null; Send 'W32K userdiag | probe launch timed out' }
    $o = ''; if (Test-Path $of) { $o = (Get-Content $of | Out-String) }
    $ok = ($o -match 'PROBE account=other-account') -and ($o -match 'is_elevated=0') -and ($o -match 'window_station=WinSta0')
    if (-not $ok) { foreach ($l in ($o -split "`n")) { if ($l.Trim()) { Send ("W32K userdiag | " + $l.Trim()) } } }
    return $ok
}
foreach ($mode in ($Modes -split ',')) {
    Send "W32K mode $mode"
    if ($mode -eq 'user') {
        $uReal = New-StdAccount
        Send ("W32K userkind " + $(if ($uReal) { 'real-user' } else { 'unavailable' }))
        if (-not $uReal) { continue }
    }
    foreach ($exe in $list) {
        $exe = $exe.Trim()
        $path = Join-Path $bin "$exe.exe"
        $outf = Join-Path $res "$exe.$mode.txt"
        Send "W32K start $exe"
        if (-not (Test-Path $path)) { Send "W32K rc $exe MISSING 0"; continue }
        $xargs = if ($filter) { " $filter" } else { '' }
        if ($mode -eq 'user') { $inner = "`"$bin\w32k_runas.exe`" --user $uName --pass $uPass `"$path`"$xargs" }
        elseif ($mode -eq 'std') { $inner = "`"$bin\w32k_runas.exe`" `"$path`"$xargs" } else { $inner = "`"$path`"$xargs" }
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "$env:ComSpec"
        $nul = if ($mode -eq 'user') { ' < nul' } else { '' }
        $psi.Arguments = "/c `"$inner > `"$outf`" 2>&1$nul`""
        $psi.WorkingDirectory = $cwd
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = [Diagnostics.Process]::Start($psi)
        $limit = $Timeout; if ($tmo.ContainsKey($exe) -and $tmo[$exe] -gt $limit) { $limit = $tmo[$exe] }
        $done = $p.WaitForExit($limit * 1000)
        $code = 'TIMEOUT'
        if ($done) { $code = $p.ExitCode } else { & taskkill /F /T /PID $p.Id 2>&1 | Out-Null; Start-Sleep -Milliseconds 500 }
        $secs = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        if (Test-Path $outf) {
            $fs = [IO.File]::Open($outf, 'Open', 'Read', 'ReadWrite')
            $sr = New-Object IO.StreamReader($fs)
            while (($line = $sr.ReadLine()) -ne $null) {
                if (-not $Full -and $line.StartsWith('RUN ')) { continue }
                Send "W32K out $exe | $line"
            }
            $sr.Close()
        }
        Send "W32K rc $exe $code $secs"
    }
}
if ($uReal) { & net user $uName /delete 2>&1 | Out-Null }
Send 'W32K done'
$port.Close()
exit 0
