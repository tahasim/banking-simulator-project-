# ATM Simulator Specification

## Goal

Create a beginner-friendly C++ secure local ATM prototype with a native Windows GUI. The app is
clearly labeled as an educational simulator and never claims to provide real banking services.

## User Flow

1. User logs in with an account number and PIN.
2. A new user can sign up with a name, generated unique account number, PIN, and optional deposit.
3. Dashboard displays the user's profile, initials avatar, phone number, and current balance.
4. User can update their profile or change their PIN after entering the current PIN.
5. User can deposit, withdraw, transfer money through private recipient lookup, or search history.
6. Every successful transaction or profile change is saved to disk.
7. User can log out and another account can log in.

## Functional Requirements

- Reject invalid account numbers and PINs.
- Reject duplicate account numbers during signup.
- Validate the user's full name, PIN confirmation, and opening deposit.
- Normalize phone numbers, enforce uniqueness, and allow exact recipient lookup by phone.
- Do not expose a public directory of all accounts.
- Support recent and favorite transfer recipients.
- Require the current PIN before changing it.
- Reject non-positive transaction amounts.
- Reject withdrawals and transfers that exceed the account balance.
- Reject transfers to missing accounts or the same account.
- Record successful deposits, withdrawals, and transfers.
- Persist balances and transaction history between application runs.
- Hash PINs and encrypt the complete local database.
- Use exact integer cents and atomically commit transactions.
- Lock login for 30 seconds after three failed attempts.
- Review and revalidate transactions before committing them.
- Recover from the last encrypted backup instead of silently replacing corrupted data.
- Upgrade existing encrypted version-2 databases to version 3.

## Acceptance Checks

- Demo account `1001` / PIN `1234` can log in.
- Demo account `1002` / PIN `5678` can log in.
- Depositing increases the balance.
- Withdrawing decreases the balance.
- Transferring updates both accounts.
- Closing and reopening the app preserves changes.
- Invalid actions show helpful GUI messages without crashing.
- Failed saves leave balances and histories unchanged.
- Existing plaintext accounts migrate without changing their login credentials.
- Existing encrypted version-2 accounts migrate without changing their login credentials.
- Corrupted account data offers backup recovery or exit.
- Profile updates, PIN changes, and favorite changes roll back after failed saves.

## Technical Design

- Language: C++17
- GUI: Native Win32 API
- Build: CMake with MinGW
- Storage: Version-3 DPAPI-encrypted file with an encrypted backup
- Authentication: Salted PBKDF2-HMAC-SHA256 PIN hashes using Windows CNG
- Money: Signed 64-bit integer cents
