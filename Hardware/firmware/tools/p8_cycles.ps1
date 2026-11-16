<#
.SYNOPSIS
    BENCH_P8_PI.md 8.5: run N full boot/shutdown cycles of the Pi 5 seated on the
    board, through the board's USB CLI, checking and logging every step.

.DESCRIPTION
    Each cycle: 'on', wait for RUNNING with the Pi's userspace up, let it settle,
    optionally check the Pi over SSH, 'off', wait for STANDBY with the latch open,
    rest. Every 'stat' on the way is checked, and 'hist' before and after is
    compared with what the completed cycles must add.

    It sends only: id, stat, hist, on, off. Never reset, bootsel, forceoff or a
    'force' variant (Send-Cmd refuses anything else). On anything unexpected it
    STOPS and sends nothing more; the board's own state machine finishes whatever
    it was doing. A stop with the Pi up leaves it running: end it with 'off'.

    Before running: SW2 taped over, the Pi's USB-C unplugged, nothing on J4, beam
    off, the board in STANDBY, and the VS Code Serial Monitor CLOSED (the COM port
    has one owner).

    Writes captures\p8_8.5_cycles_<time>.log (every exchange) and .csv (one row
    per cycle). Exit code: 0 pass, 1 fail or stop, 2 not started.

.PARAMETER PiHost
    Optional, e.g. pitrac2@pitrac2.local. Each cycle it reads the Pi's boot id,
    'vcgencmd get_throttled' and kernel ext4/mmc/I-O warnings over SSH. Needs key
    login: the script uses BatchMode and never asks for a password.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\p8_cycles.ps1 -Port COM5 -Cycles 1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\p8_cycles.ps1 -Port COM5 -PiHost pitrac2@pitrac2.local
#>
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [int]$Cycles = 20,
    [int]$SettleSeconds = 20,          # after RUNNING, before 'off': the Pi finishes booting
    [int]$RestSeconds = 15,            # in STANDBY between cycles (telemetry saves at 10 s)
    [string]$PiHost = '',
    [string]$OutDir = '',              # default: ..\captures, next to this script's folder
    [int]$BootTimeoutSeconds = 120,    # the board's own PI_BOOT_TIMEOUT_MS is 90 s
    [int]$ShutdownTimeoutSeconds = 75, # the board's own PI_SHUTDOWN_MAX_WAIT_MS is 60 s
    [double]$HoldoffSeconds = 10,      # the board's PI_SHUTDOWN_MIN_HOLDOFF_MS: 10 s from 1.3, 15 s before
    [switch]$DefineOnly                # tests: define the functions and return
)

# The latch must not open sooner than the holdoff after 'off'; 0.1 s is the
# slack of timing it from the host.
$script:MinHoldoffS = $HoldoffSeconds - 0.1
$script:Allowed = @('', 'id', 'stat', 'hist', 'on', 'off')
$script:LogPath = $null
$script:CsvPath = $null
$script:PiCheck = $false

function Write-Log([string]$text) {
    if (-not $script:LogPath) { return }
    $line = '{0}  {1}' -f (Get-Date -Format 'HH:mm:ss.fff'), $text
    [IO.File]::AppendAllText($script:LogPath, $line + "`r`n")
}

function Say([string]$text, [string]$color = 'Gray') {
    Write-Host $text -ForegroundColor $color
    Write-Log $text
}

# Reads until the CLI's prompt ("> " at the end of the output).
function Read-UntilPrompt([int]$timeoutMs) {
    $sb = New-Object System.Text.StringBuilder
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        $chunk = $script:sp.ReadExisting()
        if ($chunk) {
            [void]$sb.Append($chunk)
            if ($sb.ToString() -match '(?:^|\n)> $') { return $sb.ToString() }
        } else {
            Start-Sleep -Milliseconds 20
        }
    }
    throw "no prompt from the board within $timeoutMs ms (got: '$($sb.ToString())')"
}

function Send-Cmd([string]$cmd, [int]$timeoutMs = 3000) {
    if ($script:Allowed -notcontains $cmd) {
        throw "refusing to send '$cmd': this script sends only id, stat, hist, on and off"
    }
    $pending = $script:sp.ReadExisting()
    if ($pending) { Write-Log ('(unsolicited) ' + $pending.Trim()) }
    $script:sp.Write($cmd + "`r")
    $resp = Read-UntilPrompt $timeoutMs
    Write-Log ("sent '" + $cmd + "':`r`n" + $resp.TrimEnd())
    return $resp
}

function ConvertFrom-Stat([string]$r) {
    $s = @{ State = ''; StateMs = -1; Fault = ''; Latch = -1; Present = -1; V3 = -1
            User = -1; IsDown = -1; V5 = [double]::NaN
            Unrequested = ($r -match 'WITHOUT A REQUEST') }
    $m = [regex]::Match($r, 'state\s*:\s*([A-Z_]+)\s*\((\d+) ms\)')
    if ($m.Success) { $s.State = $m.Groups[1].Value; $s.StateMs = [long]$m.Groups[2].Value }
    $m = [regex]::Match($r, '(?m)^fault\s*:\s*(\S+)')
    if ($m.Success) { $s.Fault = $m.Groups[1].Value }
    $m = [regex]::Match($r, '(?m)^latch\s*:\s*(\d)')
    if ($m.Success) { $s.Latch = [int]$m.Groups[1].Value }
    $m = [regex]::Match($r, 'present\s+(\d)\s+3v3\s+(\d)\s+userspace\s+(\d)\s+isdown\s+(\d)')
    if ($m.Success) {
        $s.Present = [int]$m.Groups[1].Value; $s.V3 = [int]$m.Groups[2].Value
        $s.User = [int]$m.Groups[3].Value; $s.IsDown = [int]$m.Groups[4].Value
    }
    $m = [regex]::Match($r, '5V_IN\s*:\s*([0-9.]+)\s*V')
    if ($m.Success) {
        $s.V5 = [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    }
    return $s
}

function ConvertFrom-Hist([string]$r) {
    $pairs = @(
        @('boots',       '(?m)^\s*boots\s{2,}(\d+)\s*$'),
        @('watchdog',    'boots after a watchdog hang\s+(\d+)'),
        @('requests',    'Pi shutdown requests\s+(\d+)'),
        @('in_time',     'read down in time\s+(\d+)'),
        @('timed_out',   'timed out \(latch opened anyway\)\s+(\d+)'),
        @('unrequested', 'Pi went down unrequested\s+(\d+)'),
        @('came_back',   'and came back \(Pi reboot\)\s+(\d+)'),
        @('cuts',        'power cuts to a powered Pi\s+(\d+)'),
        @('faults',      'faults latched\s+(\d+)')
    )
    $h = [ordered]@{}
    foreach ($p in $pairs) {
        $m = [regex]::Match($r, $p[1])
        if (-not $m.Success) { return $null }      # fail closed: an unreadable hist stops the run
        $h[$p[0]] = [int]$m.Groups[1].Value
    }
    return $h
}

function Get-Stat { return ConvertFrom-Stat (Send-Cmd 'stat') }

function Get-Hist {
    $h = ConvertFrom-Hist (Send-Cmd 'hist')
    if ($null -eq $h) { throw "could not read every 'hist' count" }
    return $h
}

# '' when the stat is fine; otherwise why the run must stop.
function Get-StatProblem($s) {
    if (-not $s.State -or -not $s.Fault -or $s.Latch -lt 0 -or $s.User -lt 0) { return 'could not read stat' }
    if ($s.Fault -ne 'none') { return "fault latched: $($s.Fault)" }
    if ($s.State -eq 'BENCH_RUNNING') {
        return 'BENCH_RUNNING: the board does not see the Pi. Press NOTHING (the button is a hard power cut there).'
    }
    if ($s.State -eq 'FAULT' -or $s.State -eq 'FORCE_OFF') { return "state $($s.State)" }
    if ($s.Unrequested) { return 'the Pi went down without a request from the board' }
    return ''
}

# The Pi's view over SSH, or $null when it does not answer.
function Get-PiCheck {
    $remote = "cat /proc/sys/kernel/random/boot_id; vcgencmd get_throttled; " +
              "journalctl -b -k -p warning -q --no-pager | grep -iE 'ext4|mmc|corrupt|i/o error' | head -n 5; " +
              "echo PITRAC_END"
    for ($try = 1; $try -le 3; $try++) {
        $out = @(& ssh -o BatchMode=yes -o ConnectTimeout=10 $PiHost $remote 2>$null |
                 ForEach-Object { "$_".Trim() })
        if ($LASTEXITCODE -eq 0 -and $out.Count -ge 3 -and ($out -contains 'PITRAC_END')) {
            $end = [Array]::IndexOf($out, 'PITRAC_END')
            $errs = @()
            if ($end -gt 2) { $errs = @($out[2..($end - 1)]) }
            return @{ BootId = $out[0]; Throttled = $out[1]; Errors = $errs }
        }
        Start-Sleep -Seconds 5
    }
    return $null
}

function Invoke-Run {
    Say "PiTrac BENCH_P8_PI.md 8.5: $Cycles boot/shutdown cycle(s) on $Port."
    Say "Log: $script:LogPath"
    Say 'Check: SW2 taped over, the Pi''s USB-C unplugged, nothing on J4, beam off.' 'Yellow'

    try { [void](Send-Cmd '') } catch { Write-Log 'no prompt for an empty line; carrying on' }
    $id = Send-Cmd 'id'
    $built = [regex]::Match($id, 'built\s*:\s*([^\r\n]+)').Groups[1].Value.Trim()
    $ver = [regex]::Match($id, 'version\s*:\s*([^\r\n]+)').Groups[1].Value.Trim()
    if (-not $built) { Say "STOP: no build stamp in 'id'." 'Red'; return 2 }
    Say "board: built $built; version $ver"

    $h0 = Get-Hist
    $s = Get-Stat
    if ($s.State -ne 'STANDBY' -or $s.Latch -ne 0 -or $s.Fault -ne 'none') {
        Say "STOP: start in STANDBY, latch 0, no fault (now $($s.State), latch $($s.Latch), fault $($s.Fault))." 'Red'
        return 2
    }
    $script:PiCheck = [bool]$PiHost
    [IO.File]::AppendAllText($script:CsvPath,
        "cycle,start,on_to_running_s,v5in_min_V,pi_boot_id,pi_throttled,pi_errors,off_to_latch_open_s`r`n")

    $done = 0
    $lastBootId = ''
    $result = 0
    for ($c = 1; $c -le $Cycles; $c++) {
        $start = Get-Date -Format 'HH:mm:ss'
        $minV5 = 99.0

        # --- on -> RUNNING with the Pi's userspace up ------------------------------
        $r = Send-Cmd 'on'
        if ($r -notmatch 'requested on') { Say "STOP (cycle $c): 'on' was not accepted." 'Red'; $result = 1; break }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $up = $false
        $why = ''
        while ($sw.Elapsed.TotalSeconds -lt $BootTimeoutSeconds) {
            Start-Sleep -Milliseconds 1000
            $s = Get-Stat
            if ($s.V5 -lt $minV5) { $minV5 = $s.V5 }
            $why = Get-StatProblem $s
            if ($why) { break }
            if ($s.State -eq 'RUNNING' -and $s.User -eq 1 -and $s.IsDown -eq 0) { $up = $true; break }
        }
        if (-not $up) {
            if (-not $why) { $why = "no RUNNING with the Pi's userspace up within $BootTimeoutSeconds s" }
            Say "STOP (cycle $c, booting): $why" 'Red'; $result = 1; break
        }
        $toRunning = [math]::Round($sw.Elapsed.TotalSeconds, 1)

        # --- settle ----------------------------------------------------------------
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $SettleSeconds) {
            Start-Sleep -Seconds 2
            $s = Get-Stat
            if ($s.V5 -lt $minV5) { $minV5 = $s.V5 }
            $why = Get-StatProblem $s
            if (-not $why -and ($s.State -ne 'RUNNING' -or $s.User -ne 1)) { $why = "left RUNNING ($($s.State), userspace $($s.User))" }
            if ($why) { break }
        }
        if ($why) { Say "STOP (cycle $c, running): $why" 'Red'; $result = 1; break }

        # --- the Pi's own view (optional) -------------------------------------------
        $bootId = ''; $thr = ''; $nErr = ''
        if ($script:PiCheck) {
            $pi = Get-PiCheck
            if ($null -eq $pi) {
                Say "WARN: no SSH answer from $PiHost; Pi checks are off for the rest of the run." 'Yellow'
                $script:PiCheck = $false
            } else {
                $bootId = $pi.BootId; $thr = $pi.Throttled; $nErr = $pi.Errors.Count
                if ($bootId -eq $lastBootId) { $why = "the Pi's boot id did not change: it did not boot afresh" }
                elseif ($thr -ne 'throttled=0x0') { $why = "the Pi reports $thr (under-voltage or throttling since this boot)" }
                elseif ($nErr -gt 0) { $why = "the Pi's kernel log has storage warnings: " + ($pi.Errors -join ' | ') }
                $lastBootId = $bootId
                if ($why) { Say "STOP (cycle $c, the Pi is up; end it with 'off'): $why" 'Red'; $result = 1; break }
            }
        }

        # --- off -> STANDBY, latch open, not before the holdoff --------------------
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $r = Send-Cmd 'off'
        if ($r -notmatch 'requested shutdown') { Say "STOP (cycle $c): 'off' was not accepted." 'Red'; $result = 1; break }
        $holdoff = -1.0
        while ($sw.Elapsed.TotalSeconds -lt $ShutdownTimeoutSeconds) {
            Start-Sleep -Milliseconds 500
            $s = Get-Stat
            if ($s.State -eq 'STANDBY' -and $s.Latch -eq 0) {
                # When the board changed state, by the host's clock: now, less the state's age.
                $holdoff = [math]::Round($sw.Elapsed.TotalSeconds - $s.StateMs / 1000.0, 2)
                break
            }
            $why = Get-StatProblem $s
            if (-not $why -and $s.State -ne 'SHUTTING_DOWN') { $why = "unexpected state $($s.State) during the shutdown" }
            if ($why) { break }
        }
        if ($holdoff -lt 0) {
            if (-not $why) { $why = "no STANDBY within $ShutdownTimeoutSeconds s of 'off'" }
            Say "STOP (cycle $c, shutting down): $why" 'Red'; $result = 1; break
        }
        if ($holdoff -lt $script:MinHoldoffS) {
            Say "STOP (cycle $c): the latch opened $holdoff s after 'off', inside the $HoldoffSeconds s holdoff." 'Red'
            $result = 1; break
        }

        $done = $c
        [IO.File]::AppendAllText($script:CsvPath,
            ('{0},{1},{2},{3},{4},{5},{6},{7}' -f $c, $start, $toRunning, $minV5, $bootId, $thr, $nErr, $holdoff) + "`r`n")
        $piText = ''
        if ($thr) { $piText = "; Pi $thr, $nErr storage warnings" }
        Say ("cycle {0}/{1}: RUNNING after {2} s; latch open {4} s after 'off'; 5V_IN min {3} V{5}" -f `
             $c, $Cycles, $toRunning, $minV5, $holdoff, $piText) 'Green'

        # --- rest in STANDBY ---------------------------------------------------------
        if ($c -lt $Cycles) {
            $sw = [Diagnostics.Stopwatch]::StartNew()
            while ($sw.Elapsed.TotalSeconds -lt $RestSeconds) {
                Start-Sleep -Seconds 5
                $s = Get-Stat
                $why = Get-StatProblem $s
                if (-not $why -and $s.State -ne 'STANDBY') { $why = "left STANDBY ($($s.State))" }
                if ($why) { break }
            }
            if ($why) { Say "STOP (cycle $c, resting): $why" 'Red'; $result = 1; break }
        }
    }

    # --- the board's counts against what $done clean cycles must add ---------------
    $h1 = Get-Hist
    $expect = [ordered]@{ boots = 0; watchdog = 0; requests = $done; in_time = $done; timed_out = 0
                          unrequested = 0; came_back = 0; cuts = 0; faults = 0 }
    Say "hist: $done completed cycle(s); change in each count, expected in brackets:"
    foreach ($k in $expect.Keys) {
        $d = $h1[$k] - $h0[$k]
        $ok = ($d -eq $expect[$k])
        $color = 'Green'
        if (-not $ok) { $color = 'Red'; $result = 1 }
        Say ('  {0,-12} {1,4}  [{2}]' -f $k, $d, $expect[$k]) $color
    }
    if ($done -lt $Cycles) { $result = 1 }
    if ($result -eq 0) { Say "PASS: $done/$Cycles cycles." 'Green' } else { Say "FAIL or STOPPED after $done/$Cycles cycles. Send the log." 'Red' }
    Say "CSV: $script:CsvPath"
    return $result
}

if ($DefineOnly) { return }

if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'captures' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$script:LogPath = Join-Path $OutDir "p8_8.5_cycles_$stamp.log"
$script:CsvPath = Join-Path $OutDir "p8_8.5_cycles_$stamp.csv"

# 115200: any rate but 1200 (the old images' reset-to-BOOTSEL trigger). DTR on:
# the RP2354's USB console writes only to a host that asserts it.
$script:sp = New-Object System.IO.Ports.SerialPort -ArgumentList $Port, 115200,
    ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$script:sp.DtrEnable = $true
$script:sp.RtsEnable = $true
$script:sp.Encoding = [System.Text.Encoding]::ASCII
$code = 2
try {
    $script:sp.Open()
    Start-Sleep -Milliseconds 300
    $code = Invoke-Run
} catch {
    Say ('STOP: ' + $_.Exception.Message) 'Red'
    $code = 1
} finally {
    if ($script:sp.IsOpen) { $script:sp.Close() }
    Say "Port closed. Log: $script:LogPath"
}
exit $code
