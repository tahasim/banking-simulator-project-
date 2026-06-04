#include "atm_logic.h"

#include "account_store.h"
#include "credential_hasher.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <sstream>

namespace {
struct LoginState {
    int failures = 0;
    std::chrono::steady_clock::time_point lockedUntil{};
};

bool digitsOnly(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

bool validName(const std::string& name) {
    if (name.size() < 3 || name.size() > 50 || name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    bool hasLetter = false;
    for (unsigned char character : name) {
        if (std::isalpha(character) || character >= 128) {
            hasLetter = true;
        } else if (character != ' ' && character != '-' && character != '\'' && character < 128) {
            return false;
        }
    }
    return hasLetter;
}

std::string initialsFor(const std::string& name) {
    std::string initials;
    bool newWord = true;
    for (unsigned char character : name) {
        if (character == ' ' || character == '-' || character == '\'') {
            newWord = true;
        } else if (newWord) {
            initials.push_back(static_cast<char>(std::toupper(character)));
            newWord = false;
            if (initials.size() == 2) break;
        }
    }
    return initials.empty() ? "TB" : initials;
}

std::string masked(const std::string& account) {
    return account.size() <= 4 ? account : "****" + account.substr(account.size() - 4);
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M");
    return stream.str();
}

OperationResult failure(const std::string& userMessage, const std::string& diagnostic = "") {
    return {false, userMessage, diagnostic};
}
}

struct ATMSystem::Impl {
    explicit Impl(std::string databasePath) : store(std::move(databasePath)) {}

    CredentialHasher hasher;
    AccountStore store;
    std::vector<StoredAccount> accounts;
    std::map<std::string, LoginState> loginStates;

    StoredAccount* find(const std::string& number) {
        for (auto& account : accounts) {
            if (account.number == number) {
                return &account;
            }
        }
        return nullptr;
    }

    const StoredAccount* find(const std::string& number) const {
        for (const auto& account : accounts) {
            if (account.number == number) {
                return &account;
            }
        }
        return nullptr;
    }

    std::string generateAccountNumber() const {
        std::random_device device;
        std::uniform_int_distribution<int> distribution(10000000, 99999999);
        for (int attempt = 0; attempt < 1000; ++attempt) {
            const std::string candidate = std::to_string(distribution(device));
            if (find(candidate) == nullptr) {
                return candidate;
            }
        }
        return "";
    }

    bool addDemo(const std::string& number, const std::string& name, const std::string& pin,
                 Money balance, std::string& diagnostic) {
        HashedCredential credential;
        if (!hasher.create(pin, credential, diagnostic)) {
            return false;
        }
        StoredAccount account;
        account.number = number;
        account.name = name;
        account.credential = credential;
        account.balance = balance;
        account.history.push_back({timestamp(), "Demo account opened with " +
                                                    ATMSystem::formatMoney(balance),
                                   "opening", "", ""});
        accounts.push_back(account);
        return true;
    }
};

ATMSystem::ATMSystem(std::string databasePath)
    : impl_(std::make_unique<Impl>(std::move(databasePath))) {}

ATMSystem::~ATMSystem() = default;

LoadResult ATMSystem::load() {
    StoreLoadResult loaded = impl_->store.load(impl_->hasher);
    if (loaded.status == LoadStatus::Loaded || loaded.status == LoadStatus::MigratedLegacyDatabase) {
        impl_->accounts = std::move(loaded.accounts);
        return {loaded.status,
                loaded.status == LoadStatus::MigratedLegacyDatabase
                    ? "Your existing accounts were securely upgraded."
                    : "Accounts loaded.",
                loaded.diagnostic};
    }
    if (loaded.status == LoadStatus::CreatedDemoDatabase) {
        std::string diagnostic;
        impl_->accounts.clear();
        if (!impl_->addDemo("1001", "Alex Johnson", "1234", 150000, diagnostic) ||
            !impl_->addDemo("1002", "Samira Khan", "5678", 85000, diagnostic)) {
            return {LoadStatus::IoError, "Demo accounts could not be created.", diagnostic};
        }
        OperationResult saved = impl_->store.save(impl_->accounts);
        if (!saved.success) {
            return {LoadStatus::IoError, saved.userMessage, saved.diagnosticMessage};
        }
        return {LoadStatus::CreatedDemoDatabase, "Encrypted demo accounts created.", ""};
    }
    return {loaded.status,
            loaded.status == LoadStatus::CorruptDatabase
                ? "Stored account data could not be read. Restore the encrypted backup or exit."
                : "Account data could not be loaded.",
            loaded.diagnostic};
}

OperationResult ATMSystem::restoreBackup() {
    StoreLoadResult restored = impl_->store.restoreBackup();
    if (restored.status != LoadStatus::Loaded) {
        return failure("The encrypted backup could not be restored.", restored.diagnostic);
    }
    impl_->accounts = std::move(restored.accounts);
    return {true, "The encrypted backup was restored.", ""};
}

AuthResult ATMSystem::authenticate(const std::string& accountNumber, const std::string& pin) {
    LoginState& state = impl_->loginStates[accountNumber];
    const auto now = std::chrono::steady_clock::now();
    if (state.lockedUntil > now) {
        const int seconds =
            static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(state.lockedUntil - now)
                                 .count()) +
            1;
        return {AuthStatus::TemporarilyLocked, "", 0, seconds};
    }

    const StoredAccount* account = impl_->find(accountNumber);
    std::string diagnostic;
    if (account != nullptr && impl_->hasher.verify(pin, account->credential, diagnostic)) {
        state = {};
        return {AuthStatus::Success, accountNumber, 3, 0};
    }
    ++state.failures;
    if (state.failures >= 3) {
        state.failures = 0;
        state.lockedUntil = now + std::chrono::seconds(30);
        return {AuthStatus::TemporarilyLocked, "", 0, 30};
    }
    return {AuthStatus::InvalidCredentials, "", 3 - state.failures, 0};
}

OperationResult ATMSystem::createAccount(const std::string& name, const std::string& pin,
                                         Money openingDeposit,
                                         std::string& generatedAccountNumber) {
    if (!validName(name)) {
        return failure("Enter a full name using letters, spaces, apostrophes, or hyphens.");
    }
    if (pin.size() != 4 || !digitsOnly(pin)) {
        return failure("PIN must contain exactly four digits.");
    }
    if (openingDeposit < 0) {
        return failure("Opening deposit cannot be negative.");
    }

    generatedAccountNumber = impl_->generateAccountNumber();
    if (generatedAccountNumber.empty()) {
        return failure("A unique account number could not be generated.");
    }
    HashedCredential credential;
    std::string diagnostic;
    if (!impl_->hasher.create(pin, credential, diagnostic)) {
        return failure("The PIN could not be protected. Account creation was cancelled.", diagnostic);
    }

    std::vector<StoredAccount> staged = impl_->accounts;
    StoredAccount account;
    account.number = generatedAccountNumber;
    account.name = name;
    account.credential = credential;
    account.balance = openingDeposit;
    account.history.push_back(
        {timestamp(), "Account opened with " + formatMoney(openingDeposit), "opening", "", ""});
    staged.push_back(account);
    OperationResult saved = impl_->store.save(staged);
    if (!saved.success) {
        return saved;
    }
    impl_->accounts = std::move(staged);
    return {true, "Account created successfully.", ""};
}

OperationResult ATMSystem::updateProfile(const std::string& accountNumber, const std::string& name,
                                         const std::string& phoneNumber) {
    if (!validName(name)) {
        return failure("Enter a full name using letters, spaces, apostrophes, or hyphens.");
    }
    std::string normalized;
    if (!phoneNumber.empty() && !normalizePhone(phoneNumber, normalized)) {
        return failure("Phone number must contain 10 to 15 digits and may start with +.");
    }
    for (const auto& account : impl_->accounts) {
        if (!normalized.empty() && account.number != accountNumber &&
            account.phoneNumber == normalized) {
            return failure("That phone number is already linked to another account.");
        }
    }
    std::vector<StoredAccount> staged = impl_->accounts;
    auto found = std::find_if(staged.begin(), staged.end(),
                              [&](const StoredAccount& account) { return account.number == accountNumber; });
    if (found == staged.end()) return failure("The signed-in account no longer exists.");
    found->name = name;
    found->phoneNumber = normalized;
    OperationResult saved = impl_->store.save(staged);
    if (!saved.success) return saved;
    impl_->accounts = std::move(staged);
    return {true, "Profile updated and securely saved.", ""};
}

OperationResult ATMSystem::changePin(const std::string& accountNumber, const std::string& currentPin,
                                     const std::string& newPin) {
    StoredAccount* current = impl_->find(accountNumber);
    if (current == nullptr) return failure("The signed-in account no longer exists.");
    std::string diagnostic;
    if (!impl_->hasher.verify(currentPin, current->credential, diagnostic)) {
        return failure("The current PIN is incorrect.");
    }
    if (newPin.size() != 4 || !digitsOnly(newPin)) {
        return failure("New PIN must contain exactly four digits.");
    }
    HashedCredential credential;
    if (!impl_->hasher.create(newPin, credential, diagnostic)) {
        return failure("The new PIN could not be protected. Nothing was changed.", diagnostic);
    }
    std::vector<StoredAccount> staged = impl_->accounts;
    auto found = std::find_if(staged.begin(), staged.end(),
                              [&](const StoredAccount& account) { return account.number == accountNumber; });
    found->credential = credential;
    OperationResult saved = impl_->store.save(staged);
    if (!saved.success) return saved;
    impl_->accounts = std::move(staged);
    impl_->loginStates[accountNumber] = {};
    return {true, "PIN changed and securely saved.", ""};
}

OperationResult ATMSystem::setRecipientFavorite(const std::string& accountNumber,
                                                const std::string& recipientAccount, bool favorite) {
    if (accountNumber == recipientAccount || impl_->find(recipientAccount) == nullptr) {
        return failure("Recipient could not be saved.");
    }
    std::vector<StoredAccount> staged = impl_->accounts;
    auto source = std::find_if(staged.begin(), staged.end(),
                               [&](const StoredAccount& account) { return account.number == accountNumber; });
    if (source == staged.end()) return failure("The signed-in account no longer exists.");
    auto& favorites = source->favoriteRecipients;
    const auto existing = std::find(favorites.begin(), favorites.end(), recipientAccount);
    if (favorite && existing == favorites.end()) favorites.push_back(recipientAccount);
    if (!favorite && existing != favorites.end()) favorites.erase(existing);
    OperationResult saved = impl_->store.save(staged);
    if (!saved.success) return saved;
    impl_->accounts = std::move(staged);
    return {true, favorite ? "Recipient saved as a favorite." : "Recipient removed from favorites.", ""};
}

std::optional<RecipientView> ATMSystem::findRecipient(const std::string& query,
                                                      const std::string& requesterAccount) const {
    std::string phone;
    const bool phoneQuery = normalizePhone(query, phone);
    for (const auto& account : impl_->accounts) {
        if (account.number == requesterAccount) continue;
        if (account.number == query || (phoneQuery && !account.phoneNumber.empty() &&
                                        account.phoneNumber == phone)) {
            bool favorite = false;
            const StoredAccount* requester = impl_->find(requesterAccount);
            if (requester != nullptr) {
                favorite = std::find(requester->favoriteRecipients.begin(),
                                     requester->favoriteRecipients.end(), account.number) !=
                           requester->favoriteRecipients.end();
            }
            return RecipientView{account.number, masked(account.number), account.name, favorite};
        }
    }
    return std::nullopt;
}

std::vector<RecipientView> ATMSystem::listRecentRecipients(const std::string& accountNumber) const {
    std::vector<RecipientView> result;
    const StoredAccount* source = impl_->find(accountNumber);
    if (source == nullptr) return result;
    auto add = [&](const std::string& number, bool favorite) {
        const StoredAccount* account = impl_->find(number);
        if (account == nullptr || number == accountNumber ||
            std::any_of(result.begin(), result.end(),
                        [&](const RecipientView& item) { return item.accountNumber == number; })) return;
        result.push_back({number, masked(number), account->name, favorite});
    };
    for (const auto& favorite : source->favoriteRecipients) add(favorite, true);
    for (auto iterator = source->history.rbegin(); iterator != source->history.rend(); ++iterator) {
        if (iterator->type == "transfer-out") add(iterator->counterpartyAccount, false);
        if (result.size() >= 5) break;
    }
    return result;
}

OperationResult ATMSystem::previewDeposit(const std::string& accountNumber, Money amount,
                                          TransactionPreview& preview) const {
    const StoredAccount* account = impl_->find(accountNumber);
    if (account == nullptr) {
        return failure("The signed-in account no longer exists.");
    }
    if (amount <= 0 || account->balance > std::numeric_limits<Money>::max() - amount) {
        return failure("Enter a positive amount that does not exceed the supported balance.");
    }
    preview = {TransactionPreview::Type::Deposit, amount, account->balance,
               account->balance + amount, "", "", ""};
    return {true, "Deposit ready for review.", ""};
}

OperationResult ATMSystem::previewWithdraw(const std::string& accountNumber, Money amount,
                                           TransactionPreview& preview) const {
    const StoredAccount* account = impl_->find(accountNumber);
    if (account == nullptr) {
        return failure("The signed-in account no longer exists.");
    }
    if (amount <= 0) {
        return failure("Enter an amount greater than zero.");
    }
    if (amount > account->balance) {
        return failure("Insufficient funds.");
    }
    preview = {TransactionPreview::Type::Withdraw, amount, account->balance,
               account->balance - amount, "", "", ""};
    return {true, "Withdrawal ready for review.", ""};
}

OperationResult ATMSystem::previewTransfer(const std::string& accountNumber,
                                           const std::string& destinationNumber, Money amount,
                                           TransactionPreview& preview) const {
    const StoredAccount* source = impl_->find(accountNumber);
    const StoredAccount* destination = impl_->find(destinationNumber);
    if (source == nullptr) {
        return failure("The signed-in account no longer exists.");
    }
    if (destination == nullptr) {
        return failure("Destination account was not found.");
    }
    if (accountNumber == destinationNumber) {
        return failure("You cannot transfer to the same account.");
    }
    if (amount <= 0 || amount > source->balance ||
        destination->balance > std::numeric_limits<Money>::max() - amount) {
        return failure(amount > source->balance ? "Insufficient funds." : "Enter a valid amount.");
    }
    preview = {TransactionPreview::Type::Transfer, amount, source->balance,
               source->balance - amount, destination->name, destination->number,
               masked(destination->number)};
    return {true, "Transfer ready for review.", ""};
}

OperationResult ATMSystem::commit(const std::string& accountNumber,
                                  const TransactionPreview& preview) {
    TransactionPreview fresh;
    OperationResult check;
    if (preview.type == TransactionPreview::Type::Deposit) {
        check = previewDeposit(accountNumber, preview.amount, fresh);
    } else if (preview.type == TransactionPreview::Type::Withdraw) {
        check = previewWithdraw(accountNumber, preview.amount, fresh);
    } else {
        check = previewTransfer(accountNumber, preview.recipientAccount, preview.amount, fresh);
    }
    if (!check.success || fresh.currentBalance != preview.currentBalance ||
        fresh.resultingBalance != preview.resultingBalance) {
        return failure("Account information changed. Review the transaction again.");
    }

    std::vector<StoredAccount> staged = impl_->accounts;
    auto findStaged = [&staged](const std::string& number) -> StoredAccount* {
        for (auto& account : staged) {
            if (account.number == number) {
                return &account;
            }
        }
        return nullptr;
    };
    StoredAccount* source = findStaged(accountNumber);
    if (source == nullptr) {
        return failure("The signed-in account no longer exists.");
    }

    const std::string now = timestamp();
    if (preview.type == TransactionPreview::Type::Deposit) {
        source->balance += preview.amount;
        source->history.push_back({now, "Deposited " + formatMoney(preview.amount), "deposit", "", ""});
    } else if (preview.type == TransactionPreview::Type::Withdraw) {
        source->balance -= preview.amount;
        source->history.push_back({now, "Withdrew " + formatMoney(preview.amount), "withdraw", "", ""});
    } else {
        StoredAccount* destination = findStaged(preview.recipientAccount);
        if (destination == nullptr) {
            return failure("Destination account was not found.");
        }
        source->balance -= preview.amount;
        destination->balance += preview.amount;
        source->history.push_back({now, "Transferred " + formatMoney(preview.amount) + " to " +
                                           destination->name + " (" + masked(destination->number) + ")",
                                   "transfer-out", destination->name, destination->number});
        destination->history.push_back({now, "Received " + formatMoney(preview.amount) + " from " +
                                                source->name + " (" + masked(source->number) + ")",
                                        "transfer-in", source->name, source->number});
    }

    OperationResult saved = impl_->store.save(staged);
    if (!saved.success) {
        return saved;
    }
    impl_->accounts = std::move(staged);
    return {true, "Transaction completed and securely saved.", ""};
}

bool ATMSystem::getAccountView(const std::string& accountNumber, AccountView& view) const {
    const StoredAccount* account = impl_->find(accountNumber);
    if (account == nullptr) {
        return false;
    }
    view = {account->number, account->name, account->phoneNumber, initialsFor(account->name),
            account->balance, account->history};
    return true;
}

void ATMSystem::setFailWritesForTesting(bool fail) {
    impl_->store.setFailWritesForTesting(fail);
}

bool ATMSystem::parseMoney(const std::string& text, Money& amount) {
    if (text.empty() || text.front() == '-' || text.front() == '+') {
        return false;
    }
    const size_t dot = text.find('.');
    if (dot != std::string::npos && text.find('.', dot + 1) != std::string::npos) {
        return false;
    }
    const std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
    std::string fraction = dot == std::string::npos ? "" : text.substr(dot + 1);
    if (whole.empty() || !digitsOnly(whole) || fraction.size() > 2 ||
        (!fraction.empty() && !digitsOnly(fraction))) {
        return false;
    }
    while (fraction.size() < 2) {
        fraction.push_back('0');
    }
    try {
        const Money dollars = std::stoll(whole);
        const Money cents = fraction.empty() ? 0 : std::stoll(fraction);
        if (dollars > (std::numeric_limits<Money>::max() - cents) / 100) {
            return false;
        }
        amount = dollars * 100 + cents;
        return true;
    } catch (...) {
        return false;
    }
}

std::string ATMSystem::formatMoney(Money amount) {
    const bool negative = amount < 0;
    const Money absolute = negative ? -amount : amount;
    std::ostringstream stream;
    stream << (negative ? "-$" : "$") << absolute / 100 << "." << std::setw(2)
           << std::setfill('0') << absolute % 100;
    return stream.str();
}

bool ATMSystem::normalizePhone(const std::string& text, std::string& normalized) {
    std::string digits;
    for (unsigned char character : text) {
        if (std::isdigit(character)) {
            digits.push_back(static_cast<char>(character));
        } else if (character != '+' && character != ' ' && character != '-' &&
                   character != '(' && character != ')') {
            return false;
        }
    }
    if (digits.size() < 10 || digits.size() > 15) return false;
    normalized = "+" + digits;
    return true;
}
