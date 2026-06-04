#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <codecvt>
#include <cstring>
#include <locale>
#include <string>
#include <vector>

#include "atm_logic.h"

namespace {
constexpr int WIDTH = 940;
constexpr int HEIGHT = 700;
constexpr COLORREF BRAND = RGB(25, 42, 136);
constexpr COLORREF BRAND_MID = RGB(45, 63, 166);
constexpr COLORREF BRAND_LIGHT = RGB(232, 235, 249);
constexpr COLORREF SURFACE = RGB(248, 249, 255);
constexpr COLORREF GREEN = RGB(22, 163, 74);
constexpr COLORREF RED = RGB(220, 38, 38);
constexpr COLORREF INK = RGB(25, 42, 136);
constexpr COLORREF MUTED = RGB(107, 114, 128);
constexpr COLORREF BACKGROUND = RGB(255, 255, 255);
constexpr COLORREF WHITE = RGB(255, 255, 255);
constexpr COLORREF CREAM = RGB(250, 248, 243);
constexpr COLORREF BORDER = RGB(214, 218, 238);

constexpr int ID_ACCOUNT = 101;
constexpr int ID_PIN = 102;
constexpr int ID_LOGIN = 103;
constexpr int ID_SIGNUP_PAGE = 104;
constexpr int ID_NAME = 105;
constexpr int ID_NEW_PIN = 106;
constexpr int ID_CONFIRM_PIN = 107;
constexpr int ID_OPENING = 108;
constexpr int ID_CREATE = 109;
constexpr int ID_DEPOSIT = 201;
constexpr int ID_WITHDRAW = 202;
constexpr int ID_TRANSFER = 203;
constexpr int ID_HISTORY = 204;
constexpr int ID_LOGOUT = 205;
constexpr int ID_PROFILE = 206;
constexpr int ID_AMOUNT = 301;
constexpr int ID_DESTINATION = 302;
constexpr int ID_PREVIEW = 303;
constexpr int ID_COMMIT = 304;
constexpr int ID_BACK = 305;
constexpr int ID_RESTORE = 401;
constexpr int ID_EXIT = 402;
constexpr int ID_STATUS = 500;
constexpr int ID_PHONE = 501;
constexpr int ID_CURRENT_PIN = 502;
constexpr int ID_PROFILE_PIN = 503;
constexpr int ID_PROFILE_CONFIRM = 504;
constexpr int ID_SAVE_PROFILE = 505;
constexpr int ID_CHANGE_PIN = 506;
constexpr int ID_RECIPIENT_QUERY = 507;
constexpr int ID_FIND_RECIPIENT = 508;
constexpr int ID_RECIPIENT_LIST = 509;
constexpr int ID_USE_RECIPIENT = 510;
constexpr int ID_FAVORITE = 511;
constexpr int ID_HISTORY_LIST = 512;
constexpr int ID_HISTORY_SEARCH = 513;
constexpr int ID_FILTER_HISTORY = 514;
constexpr int ID_COPY_RECEIPT = 515;

enum class Screen { Loading, Login, Signup, Dashboard, Profile, Recipients, Action, Review, History, Recovery };

ATMSystem atm("accounts.db");
HWND window = nullptr;
HWND statusLabel = nullptr;
HFONT heroFont = nullptr;
HFONT titleFont = nullptr;
HFONT headingFont = nullptr;
HFONT bodyFont = nullptr;
HFONT smallFont = nullptr;
HBRUSH backgroundBrush = nullptr;
HBRUSH whiteBrush = nullptr;
Screen screen = Screen::Login;
TransactionPreview::Type actionType = TransactionPreview::Type::Deposit;
TransactionPreview pendingPreview;
std::string currentAccount;
std::chrono::steady_clock::time_point lockoutUntil{};
bool highContrast = false;
std::string selectedRecipient;

std::wstring widen(const std::string& text) {
    if (text.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size,
                        nullptr, nullptr);
    return utf8;
}

std::string getText(int id) {
    HWND control = GetDlgItem(window, id);
    if (control == nullptr) {
        return "";
    }
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, buffer.data(), length + 1);
    return narrow(buffer.data());
}

void font(HWND control, HFONT value) {
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(value), TRUE);
}

HWND label(const std::string& text, int x, int y, int width, int height, HFONT value = bodyFont,
           DWORD style = SS_LEFT, int id = 0) {
    const std::wstring wideText = widen(text);
    HWND control = CreateWindowW(L"STATIC", wideText.c_str(), WS_VISIBLE | WS_CHILD | style, x, y,
                                 width, height, window,
                                 id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
                                 nullptr, nullptr);
    font(control, value);
    return control;
}

HWND input(int id, int x, int y, int width, bool password = false, bool numeric = false) {
    DWORD style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (password) style |= ES_PASSWORD;
    if (numeric) style |= ES_NUMBER;
    HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style, x, y, width, 42, window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    font(control, bodyFont);
    return control;
}

HWND button(const std::string& text, int id, int x, int y, int width, int height) {
    const std::wstring wideText = widen(text);
    HWND control = CreateWindowW(L"BUTTON", wideText.c_str(),
                                 WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, x, y, width,
                                 height, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    font(control, bodyFont);
    return control;
}

BOOL CALLBACK destroyChild(HWND child, LPARAM) {
    DestroyWindow(child);
    return TRUE;
}

void begin(Screen next, const std::string& title, const std::string& subtitle) {
    screen = next;
    EnumChildWindows(window, destroyChild, 0);
    InvalidateRect(window, nullptr, TRUE);
    label("Tisu Bank", 72, 18, 250, 32, headingFont);
    label(title, 65, 78, 810, 48, titleFont, SS_CENTER);
    label(subtitle, 67, 126, 810, 30, bodyFont, SS_CENTER);
    label("Educational simulator  |  Encrypted local storage  |  No real money", 65, 156, 810, 24,
          smallFont, SS_CENTER);
    statusLabel = label("", 160, 610, 620, 35, smallFont, SS_CENTER, ID_STATUS);
}

void setStatus(const std::string& message) {
    if (statusLabel != nullptr) {
        const std::wstring wideMessage = widen(message);
        SetWindowTextW(statusLabel, wideMessage.c_str());
    }
}

void field(const std::string& text, int x, int y, int width) {
    label(text, x, y, width, 22, smallFont);
}

void showLogin() {
    currentAccount.clear();
    begin(Screen::Login, "Banking, made calm.", "Sign in to your encrypted Tisu Bank simulator account");
    field("ACCOUNT NUMBER", 300, 210, 340);
    HWND first = input(ID_ACCOUNT, 300, 237, 340, false, true);
    field("4-DIGIT PIN", 300, 300, 340);
    input(ID_PIN, 300, 327, 340, true, true);
    button("Sign In", ID_LOGIN, 300, 405, 340, 52);
    button("Create an Account", ID_SIGNUP_PAGE, 300, 470, 340, 52);
    label("Demo accounts: 1001 / 1234  |  1002 / 5678", 250, 550, 440, 25, smallFont, SS_CENTER);
    SetFocus(first);
}

void showSignup() {
    begin(Screen::Signup, "Open your Tisu account",
          "A unique eight-digit account number will be generated for you");
    field("FULL NAME (REQUIRED)", 170, 210, 280);
    HWND first = input(ID_NAME, 170, 237, 280);
    field("4-DIGIT PIN (REQUIRED)", 490, 210, 280);
    input(ID_NEW_PIN, 490, 237, 280, true, true);
    field("CONFIRM PIN (REQUIRED)", 170, 310, 280);
    input(ID_CONFIRM_PIN, 170, 337, 280, true, true);
    field("OPENING DEPOSIT (OPTIONAL)", 490, 310, 280);
    input(ID_OPENING, 490, 337, 280);
    label("PINs are hashed. The complete local database is encrypted for this Windows user.", 170,
          415, 600, 28, smallFont, SS_CENTER);
    button("Create Account", ID_CREATE, 250, 490, 210, 52);
    button("Back to Sign In", ID_BACK, 480, 490, 210, 52);
    SetFocus(first);
}

void showDashboard() {
    AccountView view;
    if (!atm.getAccountView(currentAccount, view)) {
        showLogin();
        return;
    }
    begin(Screen::Dashboard, "Welcome back, " + view.name, "Tisu account " + view.number);
    label(view.initials, 735, 210, 90, 52, titleFont, SS_CENTER);
    label(view.phoneNumber.empty() ? "Add a phone number in Profile" : view.phoneNumber,
          610, 265, 250, 25, smallFont, SS_CENTER);
    button("Profile & Settings", ID_PROFILE, 625, 300, 220, 42);
    label("AVAILABLE SIMULATED BALANCE", 90, 205, 380, 25, smallFont);
    label(ATMSystem::formatMoney(view.balance), 90, 235, 500, 65, heroFont);
    label("Everyday banking", 90, 325, 500, 30, headingFont);
    button("Deposit Funds", ID_DEPOSIT, 90, 370, 340, 62);
    button("Withdraw Funds", ID_WITHDRAW, 510, 370, 340, 62);
    button("Send Money", ID_TRANSFER, 90, 455, 340, 62);
    button("Transaction History", ID_HISTORY, 510, 455, 340, 62);
    button("Log Out", ID_LOGOUT, 290, 545, 360, 50);
}

void showProfile() {
    AccountView view;
    if (!atm.getAccountView(currentAccount, view)) return showLogin();
    begin(Screen::Profile, "Profile & settings", "Update your identity details or securely change your PIN");
    label(view.initials, 95, 205, 100, 60, titleFont, SS_CENTER);
    field("FULL NAME", 230, 190, 280);
    HWND name = input(ID_NAME, 230, 217, 280);
    SetWindowTextW(name, widen(view.name).c_str());
    field("PHONE NUMBER", 540, 190, 280);
    HWND phone = input(ID_PHONE, 540, 217, 280);
    SetWindowTextW(phone, widen(view.phoneNumber).c_str());
    label("Account " + view.number + " cannot be changed.", 230, 270, 590, 24, smallFont);
    button("Save Profile", ID_SAVE_PROFILE, 350, 315, 240, 48);
    field("CURRENT PIN", 150, 400, 180);
    input(ID_CURRENT_PIN, 150, 427, 180, true, true);
    field("NEW PIN", 380, 400, 180);
    input(ID_PROFILE_PIN, 380, 427, 180, true, true);
    field("CONFIRM NEW PIN", 610, 400, 180);
    input(ID_PROFILE_CONFIRM, 610, 427, 180, true, true);
    button("Change PIN", ID_CHANGE_PIN, 270, 505, 190, 48);
    button("Back to Dashboard", ID_BACK, 480, 505, 230, 48);
    SetFocus(name);
}

void showRecipients() {
    begin(Screen::Recipients, "Choose a recipient",
          "Search by an exact account number or phone number, or use a recent recipient");
    field("ACCOUNT NUMBER OR PHONE", 170, 190, 420);
    HWND query = input(ID_RECIPIENT_QUERY, 170, 217, 420);
    button("Find Recipient", ID_FIND_RECIPIENT, 610, 217, 170, 42);
    label("RECENT & FAVORITE RECIPIENTS", 170, 285, 500, 24, smallFont);
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                    LBS_NOINTEGRALHEIGHT,
                                170, 315, 610, 150, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RECIPIENT_LIST)),
                                nullptr, nullptr);
    font(list, bodyFont);
    for (const auto& recipient : atm.listRecentRecipients(currentAccount)) {
        const std::string line = (recipient.favorite ? "* " : "  ") + recipient.name + "  " +
                                 recipient.maskedAccountNumber;
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(line).c_str()));
        SendMessageW(list, LB_SETITEMDATA, SendMessageW(list, LB_GETCOUNT, 0, 0) - 1,
                     reinterpret_cast<LPARAM>(new std::string(recipient.accountNumber)));
    }
    button("Use Selected", ID_USE_RECIPIENT, 210, 500, 180, 48);
    button("Toggle Favorite", ID_FAVORITE, 410, 500, 180, 48);
    button("Cancel", ID_BACK, 610, 500, 130, 48);
    SetFocus(query);
}

void showAction(TransactionPreview::Type type) {
    actionType = type;
    const std::string title = type == TransactionPreview::Type::Deposit
                                  ? "Deposit Funds"
                                  : type == TransactionPreview::Type::Withdraw ? "Withdraw Funds"
                                                                               : "Transfer Funds";
    begin(Screen::Action, title, "Enter the details, then review everything before saving");
    int amountY = 260;
    if (type == TransactionPreview::Type::Transfer) {
        field("SELECTED RECIPIENT ACCOUNT", 300, 185, 340);
        HWND destination = input(ID_DESTINATION, 300, 212, 340, false, true);
        SetWindowTextW(destination, widen(selectedRecipient).c_str());
        EnableWindow(destination, FALSE);
        amountY = 325;
    }
    field("AMOUNT", 300, amountY, 340);
    HWND first = input(ID_AMOUNT, 300, amountY + 27, 340);
    button("Review Transaction", ID_PREVIEW, 250, amountY + 105, 230, 52);
    button("Cancel", ID_BACK, 500, amountY + 105, 190, 52);
    SetFocus(type == TransactionPreview::Type::Transfer ? GetDlgItem(window, ID_DESTINATION) : first);
}

void showReview() {
    begin(Screen::Review, "Review your transaction", "A final calm check before anything is changed");
    std::string kind = pendingPreview.type == TransactionPreview::Type::Deposit
                           ? "Deposit"
                           : pendingPreview.type == TransactionPreview::Type::Withdraw ? "Withdrawal"
                                                                                      : "Transfer";
    int y = 180;
    label("Transaction: " + kind, 260, y, 420, 30, headingFont, SS_CENTER);
    label("Amount: " + ATMSystem::formatMoney(pendingPreview.amount), 260, y + 55, 420, 30,
          bodyFont, SS_CENTER);
    if (pendingPreview.type == TransactionPreview::Type::Transfer) {
        label("Recipient: " + pendingPreview.recipientName + " (" +
                  pendingPreview.maskedRecipientAccount + ")",
              200, y + 100, 540, 30, bodyFont, SS_CENTER);
        y += 45;
    }
    label("Current balance: " + ATMSystem::formatMoney(pendingPreview.currentBalance), 260, y + 100,
          420, 30, bodyFont, SS_CENTER);
    label("Resulting balance: " + ATMSystem::formatMoney(pendingPreview.resultingBalance), 260,
          y + 145, 420, 35, headingFont, SS_CENTER);
    button("Confirm and Save", ID_COMMIT, 250, 500, 230, 52);
    button("Go Back", ID_BACK, 500, 500, 190, 52);
}

void showHistory() {
    AccountView view;
    if (!atm.getAccountView(currentAccount, view)) {
        showLogin();
        return;
    }
    begin(Screen::History, "Transaction history", "Your newest securely saved activity appears first");
    field("SEARCH TRANSACTIONS", 80, 175, 400);
    input(ID_HISTORY_SEARCH, 80, 202, 400);
    button("Filter", ID_FILTER_HISTORY, 500, 202, 120, 42);
    button("Copy Receipt", ID_COPY_RECEIPT, 640, 202, 180, 42);
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_TABSTOP |
                                    LBS_NOINTEGRALHEIGHT,
                                80, 265, 780, 265, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_LIST)),
                                nullptr, nullptr);
    font(list, bodyFont);
    for (auto iterator = view.history.rbegin(); iterator != view.history.rend(); ++iterator) {
        const std::string line = iterator->timestamp + "     " + iterator->description;
        const std::wstring wideLine = widen(line);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideLine.c_str()));
    }
    button("Back to Dashboard", ID_BACK, 290, 550, 360, 50);
}

void filterHistory() {
    AccountView view;
    if (!atm.getAccountView(currentAccount, view)) return;
    HWND list = GetDlgItem(window, ID_HISTORY_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    std::string query = getText(ID_HISTORY_SEARCH);
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto iterator = view.history.rbegin(); iterator != view.history.rend(); ++iterator) {
        std::string searchable = iterator->timestamp + " " + iterator->description + " " + iterator->type;
        std::transform(searchable.begin(), searchable.end(), searchable.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!query.empty() && searchable.find(query) == std::string::npos) continue;
        const std::wstring line = widen(iterator->timestamp + "     " + iterator->description);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
}

void showRecovery(const LoadResult& result) {
    begin(Screen::Recovery, "Your data needs attention",
          "Tisu Bank protected the unreadable file instead of silently replacing it");
    label(result.userMessage, 160, 215, 620, 65, headingFont, SS_CENTER);
    label("Choose Restore Backup to validate and restore the last encrypted copy.", 170, 315, 600,
          45, bodyFont, SS_CENTER);
    button("Restore Encrypted Backup", ID_RESTORE, 240, 410, 260, 55);
    button("Exit", ID_EXIT, 520, 410, 180, 55);
    setStatus(result.diagnosticMessage);
}

void showLoading(const std::string& state) {
    begin(Screen::Loading, "Opening Tisu Bank", state);
    label("Your encrypted local account data is being prepared.", 180, 285, 580, 40,
          bodyFont, SS_CENTER);
    label("Please wait...", 320, 355, 300, 35, headingFont, SS_CENTER);
    UpdateWindow(window);
}

void handleLogin() {
    const auto now = std::chrono::steady_clock::now();
    if (lockoutUntil > now) {
        return;
    }
    AuthResult result = atm.authenticate(getText(ID_ACCOUNT), getText(ID_PIN));
    if (result.status == AuthStatus::Success) {
        currentAccount = result.accountNumber;
        showDashboard();
    } else if (result.status == AuthStatus::TemporarilyLocked) {
        lockoutUntil = now + std::chrono::seconds(result.lockoutSeconds);
        EnableWindow(GetDlgItem(window, ID_LOGIN), FALSE);
        setStatus("Sign-in temporarily locked. Try again in 30 seconds.");
    } else {
        setStatus("Account number or PIN is incorrect. " +
                  std::to_string(result.attemptsRemaining) + " attempts remaining.");
    }
}

void handleSignup() {
    if (getText(ID_NEW_PIN) != getText(ID_CONFIRM_PIN)) {
        setStatus("PIN confirmation does not match.");
        SetFocus(GetDlgItem(window, ID_CONFIRM_PIN));
        return;
    }
    Money opening = 0;
    const std::string openingText = getText(ID_OPENING);
    if (!openingText.empty() && !ATMSystem::parseMoney(openingText, opening)) {
        setStatus("Opening deposit must be a valid amount with no more than two decimals.");
        SetFocus(GetDlgItem(window, ID_OPENING));
        return;
    }
    std::string generated;
    OperationResult result = atm.createAccount(getText(ID_NAME), getText(ID_NEW_PIN), opening, generated);
    if (!result.success) {
        setStatus(result.userMessage);
        return;
    }
    currentAccount = generated;
    const std::wstring message = widen("Account created. Your account number is " + generated);
    MessageBoxW(window, message.c_str(), L"Account Created", MB_OK | MB_ICONINFORMATION);
    showDashboard();
}

void handlePreview() {
    Money amount = 0;
    if (!ATMSystem::parseMoney(getText(ID_AMOUNT), amount) || amount <= 0) {
        setStatus("Enter a positive amount with no more than two decimal places.");
        SetFocus(GetDlgItem(window, ID_AMOUNT));
        return;
    }
    OperationResult result;
    if (actionType == TransactionPreview::Type::Deposit) {
        result = atm.previewDeposit(currentAccount, amount, pendingPreview);
    } else if (actionType == TransactionPreview::Type::Withdraw) {
        result = atm.previewWithdraw(currentAccount, amount, pendingPreview);
    } else {
        result = atm.previewTransfer(currentAccount, getText(ID_DESTINATION), amount, pendingPreview);
    }
    if (!result.success) {
        setStatus(result.userMessage);
        return;
    }
    showReview();
}

void handleCommit() {
    OperationResult result = atm.commit(currentAccount, pendingPreview);
    if (!result.success) {
        setStatus(result.userMessage);
        return;
    }
    MessageBoxW(window, widen(result.userMessage).c_str(), L"Transaction Saved",
                MB_OK | MB_ICONINFORMATION);
    showDashboard();
}

void handleProfileSave() {
    if (MessageBoxW(window, L"Save these profile changes to the encrypted database?",
                    L"Confirm Profile Update", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    OperationResult result = atm.updateProfile(currentAccount, getText(ID_NAME), getText(ID_PHONE));
    if (!result.success) return setStatus(result.userMessage);
    MessageBoxW(window, widen(result.userMessage).c_str(), L"Profile Saved", MB_OK | MB_ICONINFORMATION);
    showProfile();
}

void handlePinChange() {
    if (getText(ID_PROFILE_PIN) != getText(ID_PROFILE_CONFIRM)) {
        return setStatus("New PIN confirmation does not match.");
    }
    if (MessageBoxW(window, L"Change your PIN now?", L"Confirm PIN Change",
                    MB_YESNO | MB_ICONWARNING) != IDYES) return;
    OperationResult result =
        atm.changePin(currentAccount, getText(ID_CURRENT_PIN), getText(ID_PROFILE_PIN));
    if (!result.success) return setStatus(result.userMessage);
    MessageBoxW(window, widen(result.userMessage).c_str(), L"PIN Changed", MB_OK | MB_ICONINFORMATION);
    showProfile();
}

void selectRecipient(const std::string& accountNumber) {
    selectedRecipient = accountNumber;
    showAction(TransactionPreview::Type::Transfer);
}

void handleRecipientLookup() {
    auto recipient = atm.findRecipient(getText(ID_RECIPIENT_QUERY), currentAccount);
    if (!recipient) return setStatus("No recipient matched that exact account number or phone number.");
    const std::wstring message =
        widen("Send money to " + recipient->name + " (" + recipient->maskedAccountNumber + ")?");
    if (MessageBoxW(window, message.c_str(), L"Recipient Found", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        selectRecipient(recipient->accountNumber);
    }
}

std::string selectedRecentRecipient() {
    HWND list = GetDlgItem(window, ID_RECIPIENT_LIST);
    const LRESULT index = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) return "";
    auto* value = reinterpret_cast<std::string*>(SendMessageW(list, LB_GETITEMDATA, index, 0));
    return value == reinterpret_cast<std::string*>(LB_ERR) ? "" : *value;
}

void copySelectedReceipt() {
    HWND list = GetDlgItem(window, ID_HISTORY_LIST);
    const LRESULT index = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) return setStatus("Select a transaction to copy its receipt.");
    const LRESULT length = SendMessageW(list, LB_GETTEXTLEN, index, 0);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(list, LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(length));
    const std::wstring receipt = L"Tisu Bank educational simulator receipt\r\n" + text;
    if (!OpenClipboard(window)) return setStatus("Clipboard could not be opened.");
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (receipt.size() + 1) * sizeof(wchar_t));
    if (memory != nullptr) {
        std::memcpy(GlobalLock(memory), receipt.c_str(), (receipt.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
        setStatus("Receipt copied to the clipboard.");
    }
    CloseClipboard();
}

COLORREF colorFor(int id) {
    if (id == ID_LOGOUT || id == ID_EXIT) return RED;
    if (id == ID_RESTORE) return GREEN;
    return BRAND;
}

bool outlinedButton(int id) {
    return id == ID_SIGNUP_PAGE || id == ID_BACK || id == ID_DEPOSIT || id == ID_WITHDRAW ||
           id == ID_TRANSFER || id == ID_HISTORY || id == ID_PROFILE || id == ID_LOGOUT ||
           id == ID_EXIT || id == ID_FAVORITE;
}

void roundedBox(HDC dc, const RECT& rectangle, COLORREF fill, COLORREF border, int radius,
                int borderWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawLogo(HDC dc, int x, int y, int size) {
    RECT tile{x, y, x + size, y + size};
    roundedBox(dc, tile, BRAND, BRAND, 12);

    HPEN creamPen = CreatePen(PS_SOLID, std::max(2, size / 12), CREAM);
    HGDIOBJ oldPen = SelectObject(dc, creamPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int left = x + size / 4;
    const int right = x + size * 3 / 4;
    const int top = y + size / 3;
    const int bottom = y + size * 3 / 4;
    MoveToEx(dc, left, top, nullptr);
    LineTo(dc, right, top);
    MoveToEx(dc, x + size / 2, top, nullptr);
    LineTo(dc, x + size / 2, bottom);
    MoveToEx(dc, left, bottom, nullptr);
    LineTo(dc, right, bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(creamPen);
}

void paintChrome(HDC dc) {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, backgroundBrush);

    if (screen == Screen::Loading || screen == Screen::Login || screen == Screen::Signup ||
        screen == Screen::Recovery) {
        HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(239, 241, 251));
        HGDIOBJ oldPen = SelectObject(dc, dotPen);
        for (int x = 14; x < client.right; x += 28) {
            for (int y = 70; y < client.bottom; y += 28) {
                SetPixel(dc, x, y, RGB(232, 235, 249));
            }
        }
        SelectObject(dc, oldPen);
        DeleteObject(dotPen);
    }

    RECT nav{0, 0, client.right, 57};
    FillRect(dc, &nav, whiteBrush);
    HPEN borderPen = CreatePen(PS_SOLID, 1, BORDER);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    MoveToEx(dc, 0, 56, nullptr);
    LineTo(dc, client.right, 56);
    SelectObject(dc, oldPen);
    DeleteObject(borderPen);
    drawLogo(dc, 28, 12, 34);

    if (screen == Screen::Loading) {
        roundedBox(dc, RECT{210, 205, 730, 470}, WHITE, BORDER, 20);
    } else if (screen == Screen::Login) {
        roundedBox(dc, RECT{260, 190, 680, 585}, WHITE, BORDER, 20);
    } else if (screen == Screen::Signup) {
        roundedBox(dc, RECT{130, 190, 810, 575}, WHITE, BORDER, 20);
    } else if (screen == Screen::Action || screen == Screen::Review) {
        roundedBox(dc, RECT{220, 185, 720, 585}, SURFACE, BORDER, 16);
    } else if (screen == Screen::Recovery) {
        roundedBox(dc, RECT{150, 195, 790, 535}, WHITE, BORDER, 20);
    } else if (screen == Screen::Dashboard) {
        roundedBox(dc, RECT{65, 195, 875, 310}, SURFACE, BORDER, 16);
    } else if (screen == Screen::History) {
        roundedBox(dc, RECT{65, 170, 875, 545}, WHITE, BORDER, 16);
    } else if (screen == Screen::Profile || screen == Screen::Recipients) {
        roundedBox(dc, RECT{65, 175, 875, 575}, SURFACE, BORDER, 16);
    }
}

void drawButton(const DRAWITEMSTRUCT* item) {
    RECT rectangle = item->rcItem;
    const int id = static_cast<int>(item->CtlID);
    const bool outlined = outlinedButton(id);
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF color = highContrast ? GetSysColor(COLOR_HIGHLIGHT) : colorFor(id);
    COLORREF fill = outlined ? (pressed ? BRAND_LIGHT : WHITE) : (pressed ? BRAND_MID : color);
    COLORREF border = outlined ? (id == ID_LOGOUT || id == ID_EXIT ? RGB(254, 202, 202) : BORDER)
                               : color;
    COLORREF textColor = outlined ? color : WHITE;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, outlined ? 2 : 1, border);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, 12, 12);
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    if (item->itemState & ODS_FOCUS) {
        RECT focus = rectangle;
        InflateRect(&focus, -5, -5);
        DrawFocusRect(item->hDC, &focus);
    }
    wchar_t text[100]{};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor);
    SelectObject(item->hDC, bodyFont);
    DrawTextW(item->hDC, text, -1, &rectangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

int primaryAction() {
    if (screen == Screen::Login) return ID_LOGIN;
    if (screen == Screen::Signup) return ID_CREATE;
    if (screen == Screen::Action) return ID_PREVIEW;
    if (screen == Screen::Review) return ID_COMMIT;
    if (screen == Screen::Profile) return ID_SAVE_PROFILE;
    if (screen == Screen::Recipients) return ID_FIND_RECIPIENT;
    return 0;
}

LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == ID_LOGIN) handleLogin();
            else if (id == ID_SIGNUP_PAGE) showSignup();
            else if (id == ID_CREATE) handleSignup();
            else if (id == ID_DEPOSIT) showAction(TransactionPreview::Type::Deposit);
            else if (id == ID_WITHDRAW) showAction(TransactionPreview::Type::Withdraw);
            else if (id == ID_TRANSFER) showRecipients();
            else if (id == ID_HISTORY) showHistory();
            else if (id == ID_PROFILE) showProfile();
            else if (id == ID_LOGOUT) showLogin();
            else if (id == ID_PREVIEW) handlePreview();
            else if (id == ID_COMMIT) handleCommit();
            else if (id == ID_SAVE_PROFILE) handleProfileSave();
            else if (id == ID_CHANGE_PIN) handlePinChange();
            else if (id == ID_FIND_RECIPIENT) handleRecipientLookup();
            else if (id == ID_USE_RECIPIENT) {
                const std::string recipient = selectedRecentRecipient();
                if (recipient.empty()) setStatus("Select a recent recipient first.");
                else selectRecipient(recipient);
            } else if (id == ID_FAVORITE) {
                const std::string recipient = selectedRecentRecipient();
                auto view = atm.findRecipient(recipient, currentAccount);
                if (!view) setStatus("Select a recent recipient first.");
                else {
                    OperationResult result =
                        atm.setRecipientFavorite(currentAccount, recipient, !view->favorite);
                    if (result.success) showRecipients();
                    else setStatus(result.userMessage);
                }
            } else if (id == ID_FILTER_HISTORY) filterHistory();
            else if (id == ID_COPY_RECEIPT) copySelectedReceipt();
            else if (id == ID_BACK) {
                if (screen == Screen::Signup) showLogin();
                else if (screen == Screen::Review) showAction(actionType);
                else if (screen == Screen::Action && actionType == TransactionPreview::Type::Transfer)
                    showRecipients();
                else showDashboard();
            } else if (id == ID_RESTORE) {
                OperationResult result = atm.restoreBackup();
                if (result.success) showLogin();
                else setStatus(result.userMessage);
            } else if (id == ID_EXIT) DestroyWindow(window);
            return 0;
        }
        case WM_DRAWITEM:
            drawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            paintChrome(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_TIMER: {
            const auto now = std::chrono::steady_clock::now();
            if (lockoutUntil > now && screen == Screen::Login) {
                const int seconds = static_cast<int>(
                                        std::chrono::duration_cast<std::chrono::seconds>(lockoutUntil - now)
                                            .count()) +
                                    1;
                setStatus("Sign-in temporarily locked. Try again in " + std::to_string(seconds) +
                          " seconds.");
            } else if (screen == Screen::Login) {
                EnableWindow(GetDlgItem(window, ID_LOGIN), TRUE);
            }
            return 0;
        }
        case WM_SYSCOLORCHANGE: {
            HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
            SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
            highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetTextColor(dc, control == statusLabel ? MUTED : INK);
            return reinterpret_cast<LRESULT>(backgroundBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, WHITE);
            SetTextColor(dc, INK);
            return reinterpret_cast<LRESULT>(whiteBrush);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int command) {
    SetProcessDPIAware();
    heroFont = CreateFontW(46, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Georgia");
    titleFont = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Georgia");
    headingFont = CreateFontW(24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Georgia");
    bodyFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
    smallFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI");
    backgroundBrush = CreateSolidBrush(BACKGROUND);
    whiteBrush = CreateSolidBrush(WHITE);

    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;

    WNDCLASSW klass{};
    klass.lpfnWndProc = procedure;
    klass.hInstance = instance;
    klass.lpszClassName = L"SecureTisuBank";
    klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    klass.hbrBackground = backgroundBrush;
    RegisterClassW(&klass);
    window = CreateWindowW(klass.lpszClassName, L"Tisu Bank | Secure Local Prototype",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           (GetSystemMetrics(SM_CXSCREEN) - WIDTH) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - HEIGHT) / 2, WIDTH, HEIGHT, nullptr,
                           nullptr, instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, command == SW_HIDE ? SW_SHOW : command);
    showLoading("Opening secure database...");
    LoadResult loaded = atm.load();
    if (loaded.status == LoadStatus::CorruptDatabase || loaded.status == LoadStatus::IoError) {
        showLoading("Checking encrypted backup...");
        showRecovery(loaded);
    } else {
        showLogin();
        if (loaded.status == LoadStatus::MigratedLegacyDatabase) setStatus(loaded.userMessage);
    }
    SetTimer(window, 1, 1000, nullptr);
    UpdateWindow(window);

    MSG message{};
    while (GetMessage(&message, nullptr, 0, 0)) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            const int id = primaryAction();
            if (id) SendMessage(window, WM_COMMAND, id, 0);
        } else if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
                   screen != Screen::Login && screen != Screen::Recovery) {
            SendMessage(window, WM_COMMAND, ID_BACK, 0);
        } else {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }
    return 0;
}
