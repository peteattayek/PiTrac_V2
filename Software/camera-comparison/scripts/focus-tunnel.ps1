# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]*$')]
    [string]$PiAddress = 'pitrac.local',

    [ValidatePattern('^[A-Za-z0-9_][A-Za-z0-9_.-]*$')]
    [string]$UserName = 'pitrac',

    [string]$IdentityFile = (Join-Path $env:USERPROFILE '.ssh\id_ed25519_personal'),

    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]*$')]
    [string]$HostKeyAlias = 'pitrac.local',

    [ValidateRange(1024, 65535)]
    [int]$LocalPort = 8765,

    [ValidateRange(1024, 65535)]
    [int]$RemotePort = 8765,

    [ValidateRange(5, 60)]
    [int]$RetrySeconds = 10
)

$ErrorActionPreference = 'Stop'
$ssh = Get-Command ssh.exe -CommandType Application -ErrorAction Stop
if (!(Test-Path -LiteralPath $IdentityFile -PathType Leaf)) {
    throw "SSH identity file not found: $IdentityFile"
}
$resolvedKey = (Resolve-Path -LiteralPath $IdentityFile -ErrorAction Stop).Path

function Assert-PortAvailable {
    $listener = @(Get-NetTCPConnection -State Listen -ErrorAction Stop |
        Where-Object { $_.LocalPort -eq $LocalPort })
    if ($listener.Count -gt 0) {
        $owners = ($listener | Select-Object -ExpandProperty OwningProcess -Unique) -join ', '
        throw "Local port $LocalPort is already in use (PID $owners). Keep that tunnel or close it yourself; this script will not kill it."
    }
}

$sshArguments = @(
    '-4', '-N', '-T',
    '-i', $resolvedKey,
    '-o', 'IdentitiesOnly=yes',
    '-o', 'BatchMode=yes',
    '-o', 'StrictHostKeyChecking=yes',
    '-o', "HostKeyAlias=$HostKeyAlias",
    '-o', 'ExitOnForwardFailure=yes',
    '-o', 'ConnectTimeout=10',
    '-o', 'ServerAliveInterval=15',
    '-o', 'ServerAliveCountMax=4',
    '-L', "127.0.0.1:${LocalPort}:127.0.0.1:${RemotePort}",
    "${UserName}@${PiAddress}"
)

Write-Host "Private preview: http://127.0.0.1:$LocalPort/"
Write-Host "Using the system's current IPv4 route to $PiAddress; no VPN or routing settings are changed."
Write-Host 'Leave this terminal open. Press Ctrl+C to stop. Closing the tunnel does not stop Pi camera capture.'
Write-Host 'SSH key authentication and a previously verified host key are required; passwords will not be prompted.'

while ($true) {
    Assert-PortAvailable
    Write-Host ("[{0}] Connecting to {1}@{2}..." -f (Get-Date -Format 'HH:mm:ss'), $UserName, $PiAddress)
    $messages = New-Object 'System.Collections.Generic.Queue[string]'
    $savedPreference = $ErrorActionPreference
    try {
        # Windows PowerShell represents native stderr as ErrorRecords. A normal
        # SSH network failure must reach the bounded reconnect loop, not abort it.
        $ErrorActionPreference = 'Continue'
        & $ssh.Source @sshArguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            Write-Host $line
            $messages.Enqueue($line)
            if ($messages.Count -gt 20) {
                [void]$messages.Dequeue()
            }
        }
        $sshExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }

    $details = $messages -join "`n"
    if ($details -match '(?i)host key verification failed|REMOTE HOST IDENTIFICATION HAS CHANGED|Permission denied|bad permissions|UNPROTECTED PRIVATE KEY|Load key |bind .*failed|cannot listen to port|Could not request local forwarding') {
        throw "SSH requires operator attention (exit $sshExit). Check the key, host identity or local port; automatic retry stopped."
    }
    Write-Warning ("[{0}] SSH exited {1}; retrying in {2}s without changing the network." -f (Get-Date -Format 'HH:mm:ss'), $sshExit, $RetrySeconds)
    Start-Sleep -Seconds $RetrySeconds
}
