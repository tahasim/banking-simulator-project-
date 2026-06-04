#include "credential_hasher.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {
constexpr size_t SALT_BYTES = 16;
constexpr size_t HASH_BYTES = 32;

std::string toHex(const std::vector<unsigned char>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        stream << std::setw(2) << static_cast<int>(byte);
    }
    return stream.str();
}

bool fromHex(const std::string& hex, std::vector<unsigned char>& bytes) {
    if (hex.size() % 2 != 0) {
        return false;
    }
    bytes.clear();
    for (size_t index = 0; index < hex.size(); index += 2) {
        try {
            bytes.push_back(static_cast<unsigned char>(std::stoul(hex.substr(index, 2), nullptr, 16)));
        } catch (...) {
            return false;
        }
    }
    return true;
}

std::string statusText(NTSTATUS status) {
    return "Windows CNG error " + std::to_string(static_cast<long>(status));
}
}

bool CredentialHasher::create(const std::string& pin, HashedCredential& credential,
                              std::string& diagnostic) const {
    std::vector<unsigned char> salt(SALT_BYTES);
    NTSTATUS status = BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        diagnostic = statusText(status);
        return false;
    }
    credential.saltHex = toHex(salt);
    credential.iterations = 120000;
    return derive(pin, credential, credential.hashHex, diagnostic);
}

bool CredentialHasher::verify(const std::string& pin, const HashedCredential& credential,
                              std::string& diagnostic) const {
    std::string candidate;
    if (!derive(pin, credential, candidate, diagnostic) || candidate.size() != credential.hashHex.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t index = 0; index < candidate.size(); ++index) {
        difference |= static_cast<unsigned char>(candidate[index] ^ credential.hashHex[index]);
    }
    return difference == 0;
}

bool CredentialHasher::derive(const std::string& pin, const HashedCredential& credential,
                              std::string& hashHex, std::string& diagnostic) const {
    std::vector<unsigned char> salt;
    if (!fromHex(credential.saltHex, salt) || salt.size() != SALT_BYTES) {
        diagnostic = "Invalid credential salt.";
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                  BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) {
        diagnostic = statusText(status);
        return false;
    }

    std::vector<unsigned char> output(HASH_BYTES);
    status = BCryptDeriveKeyPBKDF2(
        algorithm, reinterpret_cast<PUCHAR>(const_cast<char*>(pin.data())),
        static_cast<ULONG>(pin.size()), salt.data(), static_cast<ULONG>(salt.size()),
        credential.iterations, output.data(), static_cast<ULONG>(output.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(status)) {
        diagnostic = statusText(status);
        return false;
    }
    hashHex = toHex(output);
    return true;
}
