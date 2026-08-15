<#
.SYNOPSIS
    One-time environment setup for the FMDN Tracker location dashboard.

.DESCRIPTION
    Creates a local virtual environment (venv/, inside this folder -- nothing
    written outside location_api/) and installs everything needed to run
    both this dashboard (app.py) and the vendored GoogleFindMyTools tool
    (../../GoogleFindMyTools, two levels up -- at the repo root, not a
    sibling of location_api), since app.py imports GoogleFindMyTools's own
    modules directly and needs its dependencies in the same interpreter.

    Safe to re-run: skips venv creation if one already exists, and pip
    install is a no-op for already-satisfied requirements.

    What this script does NOT do, because it can't be automated: the
    first-time Google login. That's an interactive, Chrome-based OAuth flow
    tied to a real Google account -- run it yourself after this script
    finishes (see the printed instructions at the end).
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$VenvDir = Join-Path $ScriptDir "venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$GfmtRequirements = Join-Path $ScriptDir "..\..\GoogleFindMyTools\requirements.txt"
$OwnRequirements = Join-Path $ScriptDir "requirements.txt"

if (-not (Test-Path $GfmtRequirements)) {
    Write-Error "Could not find $GfmtRequirements -- is GoogleFindMyTools present as a sibling of 7_ble_mioty_FMDN_tracker at the repo root?"
    exit 1
}

if (-not (Test-Path $VenvPython)) {
    Write-Host "Creating virtual environment at $VenvDir ..."
    python -m venv $VenvDir
} else {
    Write-Host "Virtual environment already exists at $VenvDir -- skipping creation."
}

Write-Host "Installing GoogleFindMyTools dependencies..."
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install -r $GfmtRequirements

Write-Host "Installing dashboard dependencies..."
& $VenvPython -m pip install -r $OwnRequirements

Write-Host ""
Write-Host "===================================================================="
Write-Host "Setup complete. Next steps:"
Write-Host ""
Write-Host "1. First-time Google login (interactive, opens Chrome -- can't be"
Write-Host "   scripted, log in with your own Google account):"
Write-Host "     .\venv\Scripts\python.exe ..\..\GoogleFindMyTools\main.py"
Write-Host ""
Write-Host "   The same command registers a new tracker (press 'r' when"
Write-Host "   prompted) -- see FMDN_tracker.md for what happens next with"
Write-Host "   the EID it gives you."
Write-Host ""
Write-Host "2. Launch the dashboard:"
Write-Host "     .\run_dashboard.ps1"
Write-Host "===================================================================="
