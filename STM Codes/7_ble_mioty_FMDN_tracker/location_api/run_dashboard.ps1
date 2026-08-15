<#
.SYNOPSIS
    Launches the FMDN Tracker location dashboard.

.DESCRIPTION
    Wrapper around `python app.py` using the local venv created by
    setup.ps1. Run setup.ps1 first if you haven't already (and completed
    the one-time Google login via GoogleFindMyTools\main.py).
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$VenvPython = Join-Path $ScriptDir "venv\Scripts\python.exe"

if (-not (Test-Path $VenvPython)) {
    Write-Error "No venv found at .\venv -- run .\setup.ps1 first."
    exit 1
}

# Best-effort lookup of the LAN-facing IPv4 address (the interface with a
# default gateway, i.e. the one actually connected to a network) -- purely
# informational for the banner below, never used for anything functional.
$LanIp = $null
try {
    $LanIp = (Get-NetIPConfiguration -ErrorAction Stop |
        Where-Object { $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up' } |
        Select-Object -First 1 -ExpandProperty IPv4Address).IPAddress
} catch {
    # Leave $LanIp as $null -- banner below falls back to a generic message
}

Write-Host ""
Write-Host "===================================================================="
Write-Host "Starting dashboard. Flask will print its own startup banner below,"
Write-Host "including a 'WARNING: This is a development server...' line -- that"
Write-Host "is normal boilerplate and safe to ignore for local/personal use."
Write-Host ""
Write-Host "It will then list TWO addresses. Use the one that matches what"
Write-Host "you're doing:"
Write-Host ""
Write-Host "  http://127.0.0.1:5000/    -> only reachable from THIS computer."
Write-Host "                               Use this for normal local use."
Write-Host ""
if ($LanIp) {
    Write-Host "  http://${LanIp}:5000/  -> reachable from ANY device on the"
} else {
    Write-Host "  http://<other IP>:5000/  -> reachable from ANY device on the"
}
Write-Host "                               same WiFi/network (e.g. to check it"
Write-Host "                               from your phone). This dashboard"
Write-Host "                               shows real device location data --"
Write-Host "                               only use this address on a network"
Write-Host "                               you trust, never on shared/public"
Write-Host "                               WiFi (university, cafe, etc.)."
Write-Host ""
Write-Host "Press CTRL+C to stop the server."
Write-Host "===================================================================="
Write-Host ""
& $VenvPython "app.py"
