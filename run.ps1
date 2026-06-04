$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

& "$PSScriptRoot\build.ps1"
Start-Process -FilePath "$root\build\atm_simulator.exe" -WorkingDirectory $root
