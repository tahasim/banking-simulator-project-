#include "account_store.h"

#include <windows.h>
#include <dpapi.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace {
constexpr const char* HEADER = "ATMDB|3";
constexpr const char* V2_HEADER = "ATMDB|2";
constexpr const char* BLOB_MAGIC = "NOVAATM2";

std::vector<std::string> split(const std::string& line, char separator) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, separator)) fields.push_back(field);
    if (!line.empty() && line.back() == separator) fields.emplace_back();
    return fields;
}

std::string encode(const std::string& value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (unsigned char character : value) stream << std::setw(2) << static_cast<int>(character);
    return stream.str();
}

bool decode(const std::string& value, std::string& decoded) {
    if (value.size() % 2 != 0) return false;
    decoded.clear();
    for (size_t index = 0; index < value.size(); index += 2) {
        try {
            decoded.push_back(static_cast<char>(std::stoul(value.substr(index, 2), nullptr, 16)));
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool readBinary(const std::string& path, std::vector<unsigned char>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool writeAndFlush(const std::string& path, const std::vector<unsigned char>& bytes,
                   std::string& diagnostic) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        diagnostic = "CreateFile failed: " + std::to_string(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size() && FlushFileBuffers(file);
    if (!ok) diagnostic = "Write or flush failed: " + std::to_string(GetLastError());
    CloseHandle(file);
    return ok;
}

bool protect(const std::string& plaintext, std::vector<unsigned char>& encrypted,
             std::string& diagnostic) {
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Tisu Bank encrypted simulator database", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        diagnostic = "DPAPI encryption failed: " + std::to_string(GetLastError());
        return false;
    }
    encrypted.assign(BLOB_MAGIC, BLOB_MAGIC + std::strlen(BLOB_MAGIC));
    encrypted.insert(encrypted.end(), output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool unprotect(const std::vector<unsigned char>& encrypted, std::string& plaintext,
               std::string& diagnostic) {
    const size_t magicSize = std::strlen(BLOB_MAGIC);
    if (encrypted.size() <= magicSize ||
        !std::equal(BLOB_MAGIC, BLOB_MAGIC + magicSize, encrypted.begin())) {
        diagnostic = "Missing encrypted database header.";
        return false;
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size() - magicSize),
                    const_cast<BYTE*>(encrypted.data() + magicSize)};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &output)) {
        diagnostic = "DPAPI decryption failed: " + std::to_string(GetLastError());
        return false;
    }
    plaintext.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

std::string serialize(const std::vector<StoredAccount>& accounts) {
    std::ostringstream stream;
    stream << HEADER << "\n";
    for (const auto& account : accounts) {
        stream << "ACCOUNT|" << account.number << "|" << encode(account.name) << "|"
               << encode(account.phoneNumber) << "|" << account.credential.saltHex << "|"
               << account.credential.hashHex << "|" << account.credential.iterations << "|"
               << account.balance << "\n";
        for (const auto& tx : account.history) {
            stream << "TX|" << encode(tx.timestamp) << "|" << encode(tx.description) << "|"
                   << encode(tx.type) << "|" << encode(tx.counterpartyName) << "|"
                   << encode(tx.counterpartyAccount) << "\n";
        }
        for (const auto& recipient : account.favoriteRecipients) {
            stream << "FAVORITE|" << recipient << "\n";
        }
        stream << "END\n";
    }
    stream << "ENDDB|" << accounts.size() << "\n";
    return stream.str();
}

bool parseSecure(const std::string& plaintext, std::vector<StoredAccount>& accounts,
                 std::string& diagnostic) {
    std::stringstream stream(plaintext);
    std::string line;
    if (!std::getline(stream, line) || (line != HEADER && line != V2_HEADER)) {
        diagnostic = "Unsupported database version.";
        return false;
    }
    const bool v2 = line == V2_HEADER;
    StoredAccount* current = nullptr;
    size_t declaredCount = std::numeric_limits<size_t>::max();
    while (std::getline(stream, line)) {
        const auto fields = split(line, '|');
        if (fields.empty()) continue;
        if (fields[0] == "ACCOUNT" && fields.size() == (v2 ? 7u : 8u)) {
            StoredAccount account;
            account.number = fields[1];
            if (!decode(fields[2], account.name) || (!v2 && !decode(fields[3], account.phoneNumber))) {
                diagnostic = "Invalid encoded account profile.";
                return false;
            }
            const size_t offset = v2 ? 0 : 1;
            account.credential.saltHex = fields[3 + offset];
            account.credential.hashHex = fields[4 + offset];
            try {
                account.credential.iterations = std::stoul(fields[5 + offset]);
                account.balance = std::stoll(fields[6 + offset]);
            } catch (...) {
                diagnostic = "Invalid numeric account field.";
                return false;
            }
            accounts.push_back(account);
            current = &accounts.back();
        } else if (fields[0] == "TX" && (fields.size() == 3 || fields.size() == 6) &&
                   current != nullptr) {
            Transaction tx;
            if (!decode(fields[1], tx.timestamp) || !decode(fields[2], tx.description) ||
                (fields.size() == 6 &&
                 (!decode(fields[3], tx.type) || !decode(fields[4], tx.counterpartyName) ||
                  !decode(fields[5], tx.counterpartyAccount)))) {
                diagnostic = "Invalid encoded transaction.";
                return false;
            }
            current->history.push_back(tx);
        } else if (fields[0] == "FAVORITE" && fields.size() == 2 && current != nullptr) {
            current->favoriteRecipients.push_back(fields[1]);
        } else if (fields[0] == "END" && current != nullptr) {
            current = nullptr;
        } else if (fields[0] == "ENDDB" && fields.size() == 2 && current == nullptr) {
            try {
                declaredCount = std::stoull(fields[1]);
            } catch (...) {
                diagnostic = "Invalid account count.";
                return false;
            }
        } else {
            diagnostic = "Malformed database record.";
            return false;
        }
    }
    if (current != nullptr || declaredCount != accounts.size()) {
        diagnostic = "Truncated database.";
        return false;
    }
    return true;
}
}

AccountStore::AccountStore(std::string databasePath) : databasePath_(std::move(databasePath)) {}

StoreLoadResult AccountStore::load(const CredentialHasher& hasher) {
    std::ifstream test(databasePath_, std::ios::binary);
    if (!test) return {LoadStatus::CreatedDemoDatabase, {}, "Database does not exist."};
    char magic[8]{};
    test.read(magic, 8);
    if (std::string(magic, static_cast<size_t>(test.gcount())) == BLOB_MAGIC) {
        test.close();
        StoreLoadResult loaded = loadSecureFile(databasePath_);
        if (loaded.status != LoadStatus::Loaded) return loaded;
        std::vector<unsigned char> bytes;
        std::string plaintext;
        std::string diagnostic;
        if (readBinary(databasePath_, bytes) && unprotect(bytes, plaintext, diagnostic) &&
            plaintext.rfind(std::string(V2_HEADER) + "\n", 0) == 0) {
            OperationResult upgraded = save(loaded.accounts);
            if (!upgraded.success) return {LoadStatus::IoError, {}, upgraded.diagnosticMessage};
            loaded.status = LoadStatus::MigratedLegacyDatabase;
            loaded.diagnostic = "Encrypted version-2 database upgraded to version 3.";
        }
        return loaded;
    }
    test.close();
    return migrateLegacy(hasher);
}

OperationResult AccountStore::save(const std::vector<StoredAccount>& accounts) {
    std::string diagnostic;
    if (failWritesForTesting_) {
        return {false, "Your changes were not saved. Nothing was changed.", "Injected write failure."};
    }
    if (!validate(accounts, diagnostic)) {
        return {false, "Your changes were not saved. Nothing was changed.", diagnostic};
    }
    std::vector<unsigned char> encrypted;
    if (!protect(serialize(accounts), encrypted, diagnostic)) {
        return {false, "Your changes were not saved. Nothing was changed.", diagnostic};
    }
    const std::string temporary = databasePath_ + ".tmp";
    const std::string backup = databasePath_ + ".bak";
    DeleteFileA(temporary.c_str());
    if (!writeAndFlush(temporary, encrypted, diagnostic)) {
        DeleteFileA(temporary.c_str());
        return {false, "Your changes were not saved. Nothing was changed.", diagnostic};
    }
    const StoreLoadResult validation = loadSecureFile(temporary);
    if (validation.status != LoadStatus::Loaded) {
        DeleteFileA(temporary.c_str());
        return {false, "Your changes were not saved. Nothing was changed.",
                "Temporary database validation failed: " + validation.diagnostic};
    }
    DeleteFileA(backup.c_str());
    if (GetFileAttributesA(databasePath_.c_str()) != INVALID_FILE_ATTRIBUTES &&
        !MoveFileExA(databasePath_.c_str(), backup.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary.c_str());
        return {false, "Your changes were not saved. Nothing was changed.",
                "Could not create backup: " + std::to_string(GetLastError())};
    }
    if (!MoveFileExA(temporary.c_str(), databasePath_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        MoveFileExA(backup.c_str(), databasePath_.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileA(temporary.c_str());
        return {false, "Your changes were not saved. Nothing was changed.",
                "Atomic replacement failed: " + std::to_string(GetLastError())};
    }
    return {true, "Saved successfully.", ""};
}

StoreLoadResult AccountStore::restoreBackup() {
    const std::string backup = databasePath_ + ".bak";
    StoreLoadResult loaded = loadSecureFile(backup);
    if (loaded.status != LoadStatus::Loaded) {
        return {LoadStatus::CorruptDatabase, {}, "Backup could not be validated: " + loaded.diagnostic};
    }
    DeleteFileA(databasePath_.c_str());
    OperationResult saved = save(loaded.accounts);
    if (!saved.success) return {LoadStatus::IoError, {}, saved.diagnosticMessage};
    CopyFileA(databasePath_.c_str(), backup.c_str(), FALSE);
    return loaded;
}

void AccountStore::setFailWritesForTesting(bool fail) { failWritesForTesting_ = fail; }

StoreLoadResult AccountStore::loadSecureFile(const std::string& path) const {
    std::vector<unsigned char> bytes;
    if (!readBinary(path, bytes)) return {LoadStatus::IoError, {}, "Could not read " + path};
    std::string plaintext;
    std::string diagnostic;
    if (!unprotect(bytes, plaintext, diagnostic)) return {LoadStatus::CorruptDatabase, {}, diagnostic};
    std::vector<StoredAccount> accounts;
    if (!parseSecure(plaintext, accounts, diagnostic) || !validate(accounts, diagnostic)) {
        return {LoadStatus::CorruptDatabase, {}, diagnostic};
    }
    return {LoadStatus::Loaded, accounts, ""};
}

StoreLoadResult AccountStore::migrateLegacy(const CredentialHasher& hasher) {
    std::ifstream file(databasePath_);
    std::vector<StoredAccount> accounts;
    StoredAccount* current = nullptr;
    std::string line;
    std::string diagnostic;
    while (std::getline(file, line)) {
        const auto fields = split(line, '|');
        if (fields.empty()) continue;
        if (fields[0] == "ACCOUNT" && fields.size() == 5) {
            StoredAccount account;
            account.number = fields[1];
            account.name = fields[3];
            if (!ATMSystem::parseMoney(fields[4], account.balance) ||
                !hasher.create(fields[2], account.credential, diagnostic)) {
                return {LoadStatus::CorruptDatabase, {}, "Legacy migration failed: " + diagnostic};
            }
            accounts.push_back(account);
            current = &accounts.back();
        } else if (fields[0] == "TX" && fields.size() == 3 && current != nullptr) {
            current->history.push_back({fields[1], fields[2], "", "", ""});
        } else if (fields[0] == "END" && current != nullptr) {
            current = nullptr;
        } else {
            return {LoadStatus::CorruptDatabase, {}, "Malformed legacy database."};
        }
    }
    if (current != nullptr || !validate(accounts, diagnostic)) {
        return {LoadStatus::CorruptDatabase, {}, "Invalid legacy database: " + diagnostic};
    }
    file.close();
    const std::string legacy = databasePath_ + ".legacy";
    DeleteFileA(legacy.c_str());
    if (!MoveFileExA(databasePath_.c_str(), legacy.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return {LoadStatus::IoError, {}, "Could not preserve legacy database."};
    }
    OperationResult saved = save(accounts);
    if (!saved.success) {
        MoveFileExA(legacy.c_str(), databasePath_.c_str(), MOVEFILE_REPLACE_EXISTING);
        return {LoadStatus::IoError, {}, saved.diagnosticMessage};
    }
    DeleteFileA(legacy.c_str());
    DeleteFileA((databasePath_ + ".bak").c_str());
    CopyFileA(databasePath_.c_str(), (databasePath_ + ".bak").c_str(), FALSE);
    return {LoadStatus::MigratedLegacyDatabase, accounts, ""};
}

bool AccountStore::validate(const std::vector<StoredAccount>& accounts, std::string& diagnostic) const {
    std::set<std::string> numbers;
    std::set<std::string> phones;
    for (const auto& account : accounts) {
        if (account.number.size() < 4 || account.number.size() > 10 ||
            !std::all_of(account.number.begin(), account.number.end(),
                         [](unsigned char c) { return std::isdigit(c) != 0; }) ||
            !numbers.insert(account.number).second || account.name.empty() || account.balance < 0 ||
            account.credential.saltHex.size() != 32 || account.credential.hashHex.size() != 64 ||
            account.credential.iterations < 10000) {
            diagnostic = "Invalid or duplicate account record.";
            return false;
        }
        if (!account.phoneNumber.empty() &&
            (account.phoneNumber.size() < 11 || account.phoneNumber.size() > 16 ||
             account.phoneNumber.front() != '+' ||
             !std::all_of(account.phoneNumber.begin() + 1, account.phoneNumber.end(),
                          [](unsigned char c) { return std::isdigit(c) != 0; }) ||
             !phones.insert(account.phoneNumber).second)) {
            diagnostic = "Invalid or duplicate phone number.";
            return false;
        }
        std::set<std::string> favorites;
        for (const auto& favorite : account.favoriteRecipients) {
            if (favorite == account.number || !favorites.insert(favorite).second) {
                diagnostic = "Invalid favorite recipient.";
                return false;
            }
        }
    }
    return true;
}
