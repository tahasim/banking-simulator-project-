$ErrorActionPreference = "Stop"

foreach ($tool in @("cmake", "g++")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool was not found. Install MinGW-w64 and CMake, then add them to PATH."
    }
}

$root = Split-Path -Parent $PSScriptRoot
cmake -S $root -B "$root\build" -G "MinGW Makefiles"
cmake --build "$root\build"
