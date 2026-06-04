#ifndef CREDENTIAL_HASHER_H
#define CREDENTIAL_HASHER_H

#include <string>

struct HashedCredential {
    std::string saltHex;
    std::string hashHex;
    unsigned long iterations = 120000;
};

class CredentialHasher {
public:
    bool create(const std::string& pin, HashedCredential& credential,
                std::string& diagnostic) const;
    bool verify(const std::string& pin, const HashedCredential& credential,
                std::string& diagnostic) const;

private:
    bool derive(const std::string& pin, const HashedCredential& credential,
                std::string& hashHex, std::string& diagnostic) const;
};

#endif
