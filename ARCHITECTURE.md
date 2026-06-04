# Secure Prototype Architecture

## Trust Boundary

The Win32 GUI never receives mutable account records or PIN hashes. It identifies the signed-in
user only by account number and communicates with `ATMSystem` using typed results and read-only
views.

```text
Win32 GUI
   |
   | AuthResult, AccountView, ProfileView, RecipientView,
   | TransactionPreview, OperationResult
   v
ATMSystem
   |- validates profiles, phones, PIN changes, and exact-cent amounts
   |- generates account numbers
   |- tracks login lockouts
   |- performs private recipient lookup and manages favorites
   |- stages and revalidates transactions
   |
   +--> CredentialHasher: salted PBKDF2-HMAC-SHA256 using Windows CNG
   |
   +--> AccountStore: version-3 parsing, DPAPI encryption, atomic writes, backups, migration
```

## Storage Commit

1. `ATMSystem` copies the current records and applies a transaction to the copies.
2. `AccountStore` validates and encrypts the complete new database using Windows DPAPI.
3. It writes and flushes `accounts.db.tmp`, then decrypts and validates that temporary file.
4. The current encrypted database becomes `accounts.db.bak`.
5. The validated temporary file atomically replaces `accounts.db`.
6. Only then does `ATMSystem` replace its live in-memory records and report success.

## Migration And Recovery

- An existing plaintext database is strictly parsed, converted to cents, and its PINs are hashed.
- Encrypted version-2 databases are upgraded to version 3 with empty profile phone and favorite
  fields.
- The legacy file remains unchanged until the encrypted replacement validates successfully.
- No plaintext backup remains after migration.
- Corrupted encrypted data is never silently replaced. The GUI offers to restore the last
  validated encrypted backup or exit.

## Security Boundaries

DPAPI protects the database for the current Windows user. This prototype does not defend against
an attacker controlling the unlocked Windows account, and it is not a real banking system.

Recipient discovery deliberately avoids listing every account. The GUI can only display saved or
recent recipients, or resolve an exact account number or normalized phone number. Phone numbers,
favorite recipients, profiles, and transfer metadata remain inside the encrypted database.
