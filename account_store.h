#ifndef ACCOUNT_STORE_H
#define ACCOUNT_STORE_H

#include <cstdint>
#include <string>
#include <vector>

#include "atm_logic.h"
#include "credential_hasher.h"

struct StoredAccount {
    std::string number;
    std::string name;
    std::string phoneNumber;
    HashedCredential credential;
    Money balance = 0;
    std::vector<Transaction> history;
    std::vector<std::string> favoriteRecipients;
};

struct StoreLoadResult {
    LoadStatus status = LoadStatus::IoError;
    std::vector<StoredAccount> accounts;
    std::string diagnostic;
};

class AccountStore {
public:
    explicit AccountStore(std::string databasePath);

    StoreLoadResult load(const CredentialHasher& hasher);
    OperationResult save(const std::vector<StoredAccount>& accounts);
    StoreLoadResult restoreBackup();

    void setFailWritesForTesting(bool fail);

private:
    std::string databasePath_;
    bool failWritesForTesting_ = false;

    StoreLoadResult loadSecureFile(const std::string& path) const;
    StoreLoadResult migrateLegacy(const CredentialHasher& hasher);
    bool validate(const std::vector<StoredAccount>& accounts, std::string& diagnostic) const;
};

#endif
