#ifndef ATM_LOGIC_H
#define ATM_LOGIC_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using Money = std::int64_t;

enum class LoadStatus {
    Loaded,
    CreatedDemoDatabase,
    MigratedLegacyDatabase,
    CorruptDatabase,
    IoError
};

enum class AuthStatus {
    Success,
    InvalidCredentials,
    TemporarilyLocked
};

struct OperationResult {
    bool success = false;
    std::string userMessage;
    std::string diagnosticMessage;
};

struct LoadResult {
    LoadStatus status = LoadStatus::IoError;
    std::string userMessage;
    std::string diagnosticMessage;
};

struct AuthResult {
    AuthStatus status = AuthStatus::InvalidCredentials;
    std::string accountNumber;
    int attemptsRemaining = 0;
    int lockoutSeconds = 0;
};

struct Transaction {
    std::string timestamp;
    std::string description;
    std::string type;
    std::string counterpartyName;
    std::string counterpartyAccount;
};

struct AccountView {
    std::string number;
    std::string name;
    std::string phoneNumber;
    std::string initials;
    Money balance = 0;
    std::vector<Transaction> history;
};

struct ProfileView {
    std::string name;
    std::string phoneNumber;
    std::string initials;
};

struct RecipientView {
    std::string accountNumber;
    std::string maskedAccountNumber;
    std::string name;
    bool favorite = false;
};

struct TransactionPreview {
    enum class Type { Deposit, Withdraw, Transfer };
    Type type = Type::Deposit;
    Money amount = 0;
    Money currentBalance = 0;
    Money resultingBalance = 0;
    std::string recipientName;
    std::string recipientAccount;
    std::string maskedRecipientAccount;
};

class AccountStore;
class CredentialHasher;

class ATMSystem {
public:
    explicit ATMSystem(std::string databasePath);
    ~ATMSystem();

    LoadResult load();
    OperationResult restoreBackup();

    AuthResult authenticate(const std::string& accountNumber, const std::string& pin);
    OperationResult createAccount(const std::string& name, const std::string& pin,
                                  Money openingDeposit, std::string& generatedAccountNumber);
    OperationResult updateProfile(const std::string& accountNumber, const std::string& name,
                                  const std::string& phoneNumber);
    OperationResult changePin(const std::string& accountNumber, const std::string& currentPin,
                              const std::string& newPin);
    OperationResult setRecipientFavorite(const std::string& accountNumber,
                                         const std::string& recipientAccount, bool favorite);
    std::optional<RecipientView> findRecipient(const std::string& query,
                                               const std::string& requesterAccount = "") const;
    std::vector<RecipientView> listRecentRecipients(const std::string& accountNumber) const;

    OperationResult previewDeposit(const std::string& accountNumber, Money amount,
                                   TransactionPreview& preview) const;
    OperationResult previewWithdraw(const std::string& accountNumber, Money amount,
                                    TransactionPreview& preview) const;
    OperationResult previewTransfer(const std::string& accountNumber,
                                    const std::string& destinationNumber, Money amount,
                                    TransactionPreview& preview) const;
    OperationResult commit(const std::string& accountNumber, const TransactionPreview& preview);

    bool getAccountView(const std::string& accountNumber, AccountView& view) const;
    void setFailWritesForTesting(bool fail);

    static bool parseMoney(const std::string& text, Money& amount);
    static std::string formatMoney(Money amount);
    static bool normalizePhone(const std::string& text, std::string& normalized);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
