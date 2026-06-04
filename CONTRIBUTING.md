# Contributing

Tisu Bank is a Windows-only C++17 educational project. Contributions should preserve its secure
local-storage behavior and clear educational-simulator labeling.

## Development Setup

Install:

- Windows 10 or newer
- CMake 3.16 or newer
- MinGW-w64 with C++17 support
- PowerShell

Confirm the tools are available:

```powershell
cmake --version
g++ --version
```

Build from the project directory:

```powershell
.\scripts\build.ps1
```

Launch the application:

```powershell
.\scripts\run.ps1
```

## Project Guidelines

- Keep money as signed 64-bit integer cents.
- Do not expose mutable account records or PIN hashes to the GUI.
- Stage changes and persist them successfully before updating live in-memory records.
- Keep recipient discovery private: recent/favorites and exact lookup only.
- Validate new serialized fields and preserve backward-compatible migration.
- Never commit `accounts.db`, backups, temporary databases, executables, or build output.

## Pull Request Checklist

- The project builds with CMake and MinGW.
- README, architecture, specification, and feature documentation remain accurate.
- No runtime account data or generated build files are staged.
