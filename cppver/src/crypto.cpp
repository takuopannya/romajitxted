#include "crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <nlohmann/json.hpp>

#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;

// ── Base64 encode/decode ──
static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::vector<uint8_t> Base64Decode(const std::string& s) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)b64chars[i]] = i;
    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : s) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return out;
}

// ── PBKDF2 key derivation ──
static bool DeriveKey(const std::string& password, const std::vector<uint8_t>& salt,
                      std::vector<uint8_t>& keyOut) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                  BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) return false;

    keyOut.resize(32); // 256-bit key
    status = BCryptDeriveKeyPBKDF2(hAlg,
        (PUCHAR)password.c_str(), (ULONG)password.size(),
        (PUCHAR)salt.data(), (ULONG)salt.size(),
        100000, // iterations (matches JS version)
        keyOut.data(), (ULONG)keyOut.size(), 0);

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return BCRYPT_SUCCESS(status);
}

// ── AES-256-GCM encrypt ──
std::string EncryptApiKey(const std::string& plaintext, const std::string& password) {
    // Generate random salt and IV
    std::vector<uint8_t> salt(16), iv(12);
    BCryptGenRandom(nullptr, salt.data(), (ULONG)salt.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(nullptr, iv.data(), (ULONG)iv.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    // Derive key
    std::vector<uint8_t> keyBytes;
    if (!DeriveKey(password, salt, keyBytes)) return "";

    // Open AES-GCM
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

    BCRYPT_KEY_HANDLE hKey = nullptr;
    BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                               keyBytes.data(), (ULONG)keyBytes.size(), 0);

    // Encrypt
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = iv.data();
    authInfo.cbNonce = (ULONG)iv.size();

    std::vector<uint8_t> tag(16);
    authInfo.pbTag = tag.data();
    authInfo.cbTag = (ULONG)tag.size();

    ULONG ciphertextSize = 0;
    BCryptEncrypt(hKey, (PUCHAR)plaintext.c_str(), (ULONG)plaintext.size(),
                  &authInfo, nullptr, 0, nullptr, 0, &ciphertextSize, 0);

    std::vector<uint8_t> ciphertext(ciphertextSize);

    // Reset auth info for actual encryption
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = iv.data();
    authInfo.cbNonce = (ULONG)iv.size();
    authInfo.pbTag = tag.data();
    authInfo.cbTag = (ULONG)tag.size();

    BCryptEncrypt(hKey, (PUCHAR)plaintext.c_str(), (ULONG)plaintext.size(),
                  &authInfo, nullptr, 0,
                  ciphertext.data(), (ULONG)ciphertext.size(), &ciphertextSize, 0);
    ciphertext.resize(ciphertextSize);

    // Append tag to ciphertext (matches WebCrypto behavior)
    ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // Build JSON
    json j;
    j["ciphertext"] = Base64Encode(ciphertext);
    j["salt"] = Base64Encode(salt);
    j["iv"] = Base64Encode(iv);
    return j.dump();
}

// ── AES-256-GCM decrypt ──
std::string DecryptApiKey(const std::string& encryptedJson, const std::string& password) {
    try {
        json j = json::parse(encryptedJson);
        auto ciphertextWithTag = Base64Decode(j["ciphertext"].get<std::string>());
        auto salt = Base64Decode(j["salt"].get<std::string>());
        auto iv = Base64Decode(j["iv"].get<std::string>());

        if (ciphertextWithTag.size() < 16) return "";

        // Separate tag from ciphertext
        std::vector<uint8_t> tag(ciphertextWithTag.end() - 16, ciphertextWithTag.end());
        std::vector<uint8_t> ciphertext(ciphertextWithTag.begin(), ciphertextWithTag.end() - 16);

        // Derive key
        std::vector<uint8_t> keyBytes;
        if (!DeriveKey(password, salt, keyBytes)) return "";

        // Open AES-GCM
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

        BCRYPT_KEY_HANDLE hKey = nullptr;
        BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                   keyBytes.data(), (ULONG)keyBytes.size(), 0);

        // Decrypt
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = iv.data();
        authInfo.cbNonce = (ULONG)iv.size();
        authInfo.pbTag = tag.data();
        authInfo.cbTag = (ULONG)tag.size();

        ULONG plaintextSize = 0;
        NTSTATUS status = BCryptDecrypt(hKey,
            ciphertext.data(), (ULONG)ciphertext.size(),
            &authInfo, nullptr, 0,
            nullptr, 0, &plaintextSize, 0);

        std::vector<uint8_t> plaintext(plaintextSize);

        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = iv.data();
        authInfo.cbNonce = (ULONG)iv.size();
        authInfo.pbTag = tag.data();
        authInfo.cbTag = (ULONG)tag.size();

        status = BCryptDecrypt(hKey,
            ciphertext.data(), (ULONG)ciphertext.size(),
            &authInfo, nullptr, 0,
            plaintext.data(), (ULONG)plaintext.size(), &plaintextSize, 0);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (!BCRYPT_SUCCESS(status)) return "";

        plaintext.resize(plaintextSize);
        return std::string(plaintext.begin(), plaintext.end());
    } catch (...) {
        return "";
    }
}
