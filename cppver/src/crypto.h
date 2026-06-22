#pragma once
#include <string>

// Encrypt plaintext with password using AES-256-GCM via Windows BCrypt
// Returns JSON string: {"ciphertext":"...","salt":"...","iv":"..."}
std::string EncryptApiKey(const std::string& plaintext, const std::string& password);

// Decrypt using the JSON object from EncryptApiKey
// Returns plaintext on success, empty string on failure
std::string DecryptApiKey(const std::string& encryptedJson, const std::string& password);
