<#
.SYNOPSIS
    Flash the LED bank, paced, so a free-running camera sees the scene: a light for
    focusing the lenses. Live current, under the firmware's guards and stricter ones here.

.DESCRIPTION
    The board cannot light the LEDs continuously: U5 ends every pulse by ~137 us, and
    the shot path has no live current yet. Live mode accepts only strobe commands, so
    it cannot trigger a camera either. So the camera free-runs with a long exposure
    (focus-preview.sh --hold-flash shows the latest lit frame) and this script fires
    one short pulse every -PeriodMs. A pulse lands in whichever frame is exposing.

    Sequence (BRINGUP_NEW_BOARD.md 12.3-12.4, the firmware's part of it):
      stat RUNNING and no fault; 'strobe' shows U5 armed and 'may arm : yes';
      beam off; detect disarm; adc 0 4096 <= 12 codes; adc ringtest 100 PASS;
      strobe gate 0; strobe live on confirm; strobe pulse 20 at gate 0 -> no current;
      a 3 % staircase (strobe gate 3, 6, ...; strobe pulse 20) to the first step that
      measures 0.5-2.5 A -- nothing by 30 % is an open load, and it stops;
      strobe gate 0; strobe cal <Amps> -> SOLVED ... PASS, at most 60 % (the R65/R66 check);
      then 'strobe pulse <WidthUs>' every -PeriodMs at the solved level.
    It stops at once, and sends 'strobe live off', on anything unexpected:
      a verdict other than 'within limits'; NOT CLEAN; LIVE MODE ENDED; REFUSED;
      a plateau over Amps x 1.06 + 0.1 A; an on-time off WidthUs by more than 4 us;
      more or fewer than one current pulse; no prompt.
    Ctrl-C or 'q' ends it the same way. 'strobe live off' is always sent last.

    Limits here, on top of the firmware's: Amps 1-9 (the LED bank's pulsed rating,
    owner 2026-11-24); WidthUs 5-100; the charge per 10 s at most 24 mC (80 % of the
    firmware's 30 mC budget), which sets the shortest period. It never raises a
    firmware limit and sends no other command.

    Before running: the board RUNNING ('on'), J3 connected (with the PSU and USB off),
    the VS Code Serial Monitor CLOSED (the COM port has one owner), nothing conductive
    near D12, and nobody looking into the LED bank.

    Writes captures\strobe_focus_<time>.log (every exchange). Exit 0 when stopped by
    the operator or -Count, 1 on a stop condition, 2 if it never armed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\strobe_focus.ps1 -Port COM10
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\strobe_focus.ps1 -Port COM10 -Amps 5 -WidthUs 50 -PeriodMs 400
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\strobe_focus.ps1 -Port COM10 -WidthUs "100,20,10" -Count 6
    Six pulses at each width, in that order, after one staircase and cal: a brightness comparison.
#>
[CmdletBinding()]
param(
    [string]$Port = 'COM10',
    [double]$Amps = 9.0,
    [string]$WidthUs = '100',     # one width, or several ("100,20,10"): -Count pulses at each
    [int]$PeriodMs = 500,
    [int]$Count = 0,              # pulses to fire (per width); 0 = until Ctrl-C or 'q' (one width only)
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
if ($Amps -lt 1.0 -or $Amps -gt 9.0) { Write-Output 'Amps must be 1-9 (the LED bank: 9 A pulsed at most).'; exit 2 }
# A string, split on commas or spaces: "powershell -File" passes 100,20,10 as one string, and an
# [int[]] parameter would read it as 1002010.
$parts = @($WidthUs -split '[,\s]+' | Where-Object { $_ -ne '' })
foreach ($p in $parts) { if ($p -notmatch '^\d{1,3}$') { Write-Output "WidthUs: '$p' is not a whole number of us."; exit 2 } }
[int[]]$script:Widths = @($parts | ForEach-Object { [int]$_ })   # not $WidthUs: that stays [string]
if ($Widths.Count -lt 1) { Write-Output 'WidthUs is empty.'; exit 2 }
foreach ($w in $Widths) { if ($w -lt 5 -or $w -gt 100) { Write-Output 'WidthUs must be 5-100.'; exit 2 } }
if ($Widths.Count -gt 1 -and $Count -lt 1) { Write-Output 'Several widths need -Count (pulses per width).'; exit 2 }
$wMax = ($Widths | Measure-Object -Maximum).Maximum
$mcPer10s = $Amps * $wMax * 1e-3 * (10000.0 / [Math]::Max(1, $PeriodMs))
$minPeriod = [Math]::Ceiling($Amps * $wMax * 1e-3 * 10000.0 / 24.0)
$minPeriod = [Math]::Max(200, $minPeriod)
if ($PeriodMs -lt $minPeriod) {
    Write-Output ("PeriodMs {0} is too short: {1:N1} mC per 10 s. At {2} A x {3} us the shortest is {4} ms (24 mC per 10 s)." -f $PeriodMs, $mcPer10s, $Amps, $wMax, $minPeriod)
    exit 2
}

if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'captures' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$script:LogPath = Join-Path $OutDir ('strobe_focus_{0}.log' -f (Get-Date -Format 'yyyy-MM-dd_HHmmss'))

function Write-Log([string]$text) {
    [IO.File]::AppendAllText($script:LogPath, ('{0}  {1}' -f (Get-Date -Format 'HH:mm:ss.fff'), $text) + "`r`n")
}
function Say([string]$text) { Write-Host $text; Write-Log $text }

$sp = New-Object System.IO.Ports.SerialPort -ArgumentList $Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.DtrEnable = $true; $sp.RtsEnable = $true; $sp.Encoding = [Text.Encoding]::ASCII

function Send-Cmd([string]$c, [int]$timeoutMs = 5000) {
    $sp.Write($c + "`r")
    $sb = New-Object System.Text.StringBuilder
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        $chunk = $sp.ReadExisting()
        if ($chunk) {
            [void]$sb.Append($chunk)
            if ($sb.ToString() -match '(?:^|\n)> $') { break }
        } else { Start-Sleep -Milliseconds 5 }
    }
    $r = $sb.ToString()
    Write-Log (">>> $c`r`n" + $r.TrimEnd())
    if ($r -notmatch '(?:^|\n)> $') { throw "no prompt within $timeoutMs ms after '$c'" }
    return $r
}

function Stop-Key {
    try {
        if ([Console]::KeyAvailable) { return ([Console]::ReadKey($true).KeyChar -eq 'q') }
    } catch { }
    return $false
}

# The session. Returns the exit code; every early return happens before or after arming,
# and the caller's finally sends 'strobe live off' whenever live mode was armed.
function Invoke-Session {
    try { $sp.Open() } catch { Say ("Cannot open ${Port}: " + $_.Exception.Message + ' (close the Serial Monitor).'); return 2 }
    Start-Sleep -Milliseconds 300
    $pending = $sp.ReadExisting(); if ($pending) { Write-Log ('(pending) ' + $pending.Trim()) }
    Say ("strobe_focus: {0} A, {1} us every {2} ms (at most {3:N1} mC per 10 s), log {4}" -f $Amps, ($Widths -join '/'), $PeriodMs, $mcPer10s, $script:LogPath)

    $r = Send-Cmd 'stat'
    if ($r -notmatch 'state\s*:\s*RUNNING' -or $r -notmatch 'fault\s*:\s*none') { Say 'STOP: the board is not RUNNING with no fault. Send on, then retry.'; return 2 }
    $r = Send-Cmd 'strobe'
    if ($r -match 'live\s*:\s*ARMED') { Say 'Live mode was left armed: leaving it first.'; [void](Send-Cmd 'strobe live off'); $r = Send-Cmd 'strobe' }
    if ($r -notmatch 'U5 pulse limiter armed') { Say 'STOP: strobe does not show GPIO27 0 / U5 pulse limiter armed.'; return 2 }
    if ($r -notmatch 'may arm\s*:\s*yes') { Say ('STOP: live mode may not arm: ' + (($r -split "`n" | Where-Object { $_ -match 'may arm' }) -join ' ').Trim()); return 2 }
    [void](Send-Cmd 'beam off'); [void](Send-Cmd 'detect disarm')
    $r = Send-Cmd 'adc 0 4096'
    if ($r -notmatch 'ch0 = (\d+)' -or [int]$Matches[1] -gt 12) { Say ('STOP: adc 0 4096 is not <= 12 codes: ' + $r.Trim()); return 2 }
    Say ("ADC0 at rest: {0} codes (<= 12)" -f $Matches[1])
    $r = Send-Cmd 'adc ringtest 100' 30000
    if ($r -notmatch 'PASS: every pair covered') { Say 'STOP: adc ringtest 100 did not PASS.'; return 2 }
    Say 'adc ringtest 100: PASS'

    [void](Send-Cmd 'strobe gate 0')
    Start-Sleep -Milliseconds 100
    $script:armed = $true    # from here on, the finally sends 'strobe live off'
    $r = Send-Cmd 'strobe live on confirm'
    if ($r -notmatch 'LIVE: ARMED') { Say ('STOP: live mode did not arm: ' + $r.Trim()); return 2 }
    Say 'LIVE: ARMED'
    Start-Sleep -Milliseconds 150
    $r = Send-Cmd 'strobe pulse 20'
    if ($r -notmatch 'Verdict: within limits' -or $r -notmatch 'no current pulse above') { Say 'STOP: current at gate 0 (or no usable readback). Q9 may be conducting with its gate at 0.'; return 1 }
    Say 'gate 0, 20 us: within limits, no current (as expected)'

    # BRINGUP 12.3 part 3 before any cal: 3 % steps, one 20 us pulse each, until the first
    # step that measures 0.5-2.5 A. Nothing by 30 % is an open load: stop. (Added 2026-11-24:
    # the first version went straight to 'strobe cal', which climbed to the 70 % ceiling
    # into an open load.)
    $prevA = 0.0; $found = $false
    for ($p = 3; $p -le 30; $p += 3) {
        $r = Send-Cmd ('strobe gate {0}' -f $p)
        if ($r -notmatch 'gate <- level') { Say ('STOP: strobe gate {0} was not taken: ' -f $p + $r.Trim()); return 1 }
        Start-Sleep -Milliseconds 300
        $r = Send-Cmd 'strobe pulse 20'
        if ($r -notmatch 'Verdict: within limits' -or $r -match 'NOT CLEAN|LIVE MODE ENDED|REFUSED|OVERCURRENT') { Say ("STOP: staircase {0} %: not within limits. See the log." -f $p); return 1 }
        $m = [regex]::Matches($r, 'pulse\s+\d+:\s+([\d.]+) A plateau')
        if ($m.Count -gt 1) { Say ("STOP: staircase {0} %: {1} current pulses for one fired." -f $p, $m.Count); return 1 }
        $a = 0.0
        if ($m.Count -eq 1) { $a = [double]::Parse($m[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) }
        if ($a - $prevA -gt 2.0) { Say ("STOP: staircase {0} %: a jump from {1:N3} A to {2:N3} A in one step." -f $p, $prevA, $a); return 1 }
        Say ("staircase {0} %: {1}" -f $p, $(if ($a -gt 0) { '{0:N3} A' -f $a } else { 'no current' }))
        if ($a -ge 0.5) {
            if ($a -gt 2.5) { Say 'STOP: the first current is over 2.5 A.'; return 1 }
            $found = $true; break
        }
        $prevA = $a
    }
    if (-not $found) {
        Say 'STOP: nothing has flowed by 30 % -- an open load. Power down (PSU and USB off), then check J3: seated, and the LED bank''s polarity (BRINGUP_NEW_BOARD.md 12.3).'
        return 1
    }
    [void](Send-Cmd 'strobe gate 0')
    Start-Sleep -Milliseconds 300

    $r = Send-Cmd ('strobe cal {0}' -f $Amps.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture)) 90000
    if ($r -notmatch 'SOLVED: level (\d+) \(([\d.]+) %' ) { Say ('STOP: strobe cal did not solve: ' + (($r -split "`n" | Select-Object -Last 4) -join ' ').Trim()); return 1 }
    $level = [int]$Matches[1]; $pct = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
    if ($r -notmatch 'every pulse within 5 % -- PASS') { Say 'STOP: the confirmation pulses are not all within 5 %.'; return 1 }
    if ($pct -gt 60.0) { Say ("STOP: solved at {0:N2} % (over 60 %): check R65/R66 before using this current." -f $pct); return 1 }
    $cap = if ($r -match 'Session cap: level (\d+)') { $Matches[1] } else { '?' }
    $mean = if ($r -match 'confirmation mean ([\d.]+) A') { $Matches[1] } else { '?' }
    Say ("strobe cal {0}: SOLVED level {1} ({2:N2} %), confirmation mean {3} A, session cap {4}" -f $Amps, $level, $pct, $mean, $cap)

    $limitA = $Amps * 1.06 + 0.1
    $next = [Diagnostics.Stopwatch]::StartNew()
    foreach ($width in $Widths) {
    $n = 0
    Say ("Flashing: {0} us every {1} ms. Ctrl-C or 'q' stops." -f $width, $PeriodMs)
    while ($Count -eq 0 -or $n -lt $Count) {
        if (Stop-Key) { Say 'Stopped by the operator (q).'; return 0 }
        $wait = $PeriodMs - $next.ElapsedMilliseconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
        $next.Restart()
        $r = Send-Cmd ('strobe pulse {0}' -f $width)
        $n++
        $bad = @()
        if ($r -notmatch 'Verdict: within limits') { $bad += 'verdict' }
        if ($r -match 'NOT CLEAN|LIVE MODE ENDED|REFUSED|OVERCURRENT') { $bad += 'stop text' }
        $pulses = [regex]::Matches($r, 'pulse\s+\d+:\s+([\d.]+) A plateau\s+peak\s+([\d.]+) A\s+on\s+(\d+) us')
        $a = 0.0; $on = 0
        if ($pulses.Count -ne 1) { $bad += ("{0} current pulses" -f $pulses.Count) }
        else {
            $a = [double]::Parse($pulses[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
            $on = [int]$pulses[0].Groups[3].Value
            if ($a -gt $limitA) { $bad += ("plateau {0:N3} A over {1:N2} A" -f $a, $limitA) }
            if ([Math]::Abs($on - $width) -gt 4) { $bad += ("on-time {0} us" -f $on) }
        }
        if ($bad.Count) { Say ("STOP at pulse {0} ({1} us): {2}. See the log." -f $n, $width, ($bad -join '; ')); return 1 }
        if ($n -le 3 -or $n % 20 -eq 0) { Say ("pulse {0} ({1} us): {2:N3} A plateau, on {3} us" -f $n, $width, $a, $on) }
    }
    }
    return 0
}

$script:armed = $false
$code = 1
try {
    $code = Invoke-Session
} catch {
    Say ('STOP: ' + $_.Exception.Message)
    $code = 1
} finally {
    if ($sp.IsOpen) {
        if ($script:armed) {
            try { [void](Send-Cmd 'strobe live off'); Say 'strobe live off: gate 0, live mode off.' }
            catch { Say ('strobe live off FAILED: ' + $_.Exception.Message + ' -- send it by hand, or off.') }
        }
        $sp.Close()
    }
}
exit $code
