<#
.SYNOPSIS
Checks whether traffic escapes while Throned switches profiles.

.DESCRIPTION
While a profile switch is in flight the core is briefly down and the tun is
gone. Without the transition guard, packets take the physical interface with the
real address; with it, they are dropped. That difference is observable: probe a
fixed external host as fast as possible and watch what happens during a switch.

  every probe succeeded  -> traffic kept flowing outside the tunnel: LEAK
  a run of failures      -> the guard held

The switch is triggered through Throned's own control interface, so the run is
unattended and repeatable.

.PARAMETER Throned
Path to throned.exe. Defaults to the one on PATH.

.PARAMETER To
Profile id to switch to. Defaults to the first profile that is not running.

.EXAMPLE
powershell -File script/test_transition_leak.ps1
#>
param(
    [string]$Throned = "throned",
    [int]$To = -1,
    [string]$ProbeHost = "1.1.1.1",
    [int]$ProbePort = 443,
    [int]$ProbeIntervalMs = 40,
    [int]$SettleSeconds = 12
)

$ErrorActionPreference = "Stop"

function Invoke-Cli([string]$Json) {
    $out = & $Throned --cli $Json 2>&1 | Out-String
    try { return $out | ConvertFrom-Json } catch { throw "control interface did not answer JSON: $out" }
}

Write-Host "== preflight =="
$status = Invoke-Cli '{"cmd":"status"}'
if (-not $status.ok) { throw "status failed: $($status.error)" }
if (-not $status.data.running) { throw "no profile is running - start one first" }
if (-not $status.data.tun_enabled) { throw "TUN is off - this test is about the tun gap" }
Write-Host ("running: {0} (id {1}), tun: on" -f $status.data.running_profile_name, $status.data.running_profile_id)

if ($To -lt 0) {
    $profiles = Invoke-Cli '{"cmd":"profiles.list"}'
    $other = $profiles.data.profiles | Where-Object { $_.id -ne $status.data.running_profile_id } | Select-Object -First 1
    if (-not $other) { throw "need a second profile to switch to" }
    $To = $other.id
    Write-Host ("switching to: {0} (id {1})" -f $other.name, $To)
}

# A TCP connect is the cheapest thing that proves a packet left the machine.
$probe = {
    param($h, $p, $intervalMs, $stopFile)
    $results = New-Object System.Collections.ArrayList
    while (-not (Test-Path $stopFile)) {
        $client = New-Object System.Net.Sockets.TcpClient
        $ok = $false
        try {
            $ok = $client.ConnectAsync($h, $p).Wait(700)
        } catch { $ok = $false } finally { $client.Dispose() }
        [void]$results.Add([pscustomobject]@{ At = [DateTime]::UtcNow; Ok = $ok })
        Start-Sleep -Milliseconds $intervalMs
    }
    return $results
}

$stopFile = Join-Path $env:TEMP ("throned-leak-stop-" + [guid]::NewGuid())
$job = Start-Job -ScriptBlock $probe -ArgumentList $ProbeHost, $ProbePort, $ProbeIntervalMs, $stopFile

Write-Host "== baseline (2s) =="
Start-Sleep -Seconds 2

Write-Host "== switching =="
$switchStart = [DateTime]::UtcNow
$null = Invoke-Cli ("{`"cmd`":`"profile.start`",`"id`":$To}")

for ($i = 0; $i -lt $SettleSeconds; $i++) {
    Start-Sleep -Seconds 1
    $s = Invoke-Cli '{"cmd":"status"}'
    if ($s.data.running -and $s.data.running_profile_id -eq $To) { break }
}
$switchEnd = [DateTime]::UtcNow
Start-Sleep -Seconds 2

New-Item -ItemType File $stopFile | Out-Null
$samples = Receive-Job $job -Wait
Remove-Job $job -Force
Remove-Item $stopFile -Force -ErrorAction SilentlyContinue

$during = $samples | Where-Object { $_.At -ge $switchStart -and $_.At -le $switchEnd }
$failed = @($during | Where-Object { -not $_.Ok })
$passed = @($during | Where-Object { $_.Ok })

Write-Host ""
Write-Host "== result =="
Write-Host ("probes total: {0}, during switch: {1}" -f $samples.Count, $during.Count)
Write-Host ("during switch - blocked: {0}, got through: {1}" -f $failed.Count, $passed.Count)
Write-Host ("switch took: {0:n1}s" -f ($switchEnd - $switchStart).TotalSeconds)

if ($during.Count -lt 5) {
    Write-Host "INCONCLUSIVE - too few probes landed in the switch window" -ForegroundColor Yellow
    exit 2
}
if ($failed.Count -eq 0) {
    Write-Host "LEAK - every probe succeeded while the tunnel was down" -ForegroundColor Red
    exit 1
}
Write-Host ("GUARDED - traffic was blocked for {0:n1}s" -f ($failed.Count * $ProbeIntervalMs / 1000.0)) -ForegroundColor Green
exit 0
