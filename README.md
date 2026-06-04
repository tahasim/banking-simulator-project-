# Tisu Bank

![Tisu Bank logo](tisu-bank-logo.svg)

Tisu Bank is a native Windows ATM and banking simulator built in C++17 for a CS100 project. It
combines a polished Win32 graphical interface with secure local storage, exact-cent transactions,
customer profiles and private recipient discovery.

> **Educational simulator:** Tisu Bank does not connect to real banking services and never uses
> real money.

## Highlights

- Native Win32 GUI with a custom indigo Tisu Bank design
- Secure login and new-account signup
- Generated account numbers and editable customer profiles
- Normalized, unique phone numbers for private recipient lookup
- Initials-based profile avatars
- PIN changes requiring the current PIN
- Deposits, withdrawals, and reviewed transfers
- Recent and favorite transfer recipients
- Searchable transaction history and copyable receipts
- Encrypted local database, atomic saves, backups, and recovery
- Keyboard navigation, DPI awareness, and high-contrast support

## Security Design

Tisu Bank treats every transaction as a durable operation:

1. The request is validated and applied to an in-memory copy.
2. The complete database is validated and encrypted with Windows DPAPI.
3. A temporary encrypted file is written, flushed, decrypted, and validated.
4. The previous database becomes the encrypted backup.
5. The validated temporary file atomically replaces the live database.
6. Only then does the application report success.

Additional protections include:

- Money stored as signed 64-bit integer cents instead of floating point
- Salted PBKDF2-HMAC-SHA256 PIN hashes using Windows CNG
- Unique random salt for every PIN
- Three-attempt login lockout for 30 seconds
- Rollback when persistence fails
- Strict database parsing and corruption detection
- Automatic migration from plaintext and encrypted version-2 databases
- Private recipient lookup instead of exposing every account

DPAPI protects the database for the Windows user who created it. This project is a secure
educational prototype, not a production banking system.

## Demo Accounts

The app creates these accounts when no database exists:

| Account number | PIN | Starting balance |
|---|---|---:|
| `1001` | `1234` | `$1,500.00` |
| `1002` | `5678` | `$850.00` |

## Requirements

- Windows 10 or newer
- CMake 3.16 or newer
- MinGW-w64 with C++17 support
- PowerShell
- Windows `bcrypt` and `crypt32` system libraries

Confirm the required tools are available:

```powershell
cmake --version
g++ --version
```

## Build And Run

From the project directory:

```powershell
.\scripts\build.ps1
.\scripts\run.ps1
```

Manual commands:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\atm_simulator.exe
```

## Project Structure

| Path | Purpose |
|---|---|
| `main.cpp` | Native Win32 GUI, screens, controls, and visual theme |
| `atm_logic.*` | Validation, authentication, profiles, recipients, and transactions |
| `account_store.*` | DPAPI encryption, versioned storage, backups, and migration |
| `credential_hasher.*` | Windows CNG PBKDF2 PIN hashing |
| `scripts/` | PowerShell build and run helpers |
| `ARCHITECTURE.md` | Trust boundaries and durable-save design |
| `FEATURES.txt` | Full feature summary |

## Local Data And Recovery

The following runtime files are intentionally excluded from Git:

- `accounts.db`: current encrypted database
- `accounts.db.bak`: last validated encrypted backup
- `accounts.db.tmp`: temporary commit file

If the live database becomes unreadable, the application offers to validate and restore the
encrypted backup. Deleting the database and backup resets the simulator and recreates the demo
accounts.

## Documentation

- [Architecture](ARCHITECTURE.md)
- [Feature list](FEATURES.txt)
- [Project specification](SPEC.md)
- [Contributing guide](CONTRIBUTING.md)

## License

This project is provided for educational use. Add a license file before redistributing or accepting
external contributions.
