// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_CIPHER_H
#define RDK_CLEARKEY_CIPHER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "error.h"
#include "license.h"

namespace rdk::clearkey {

enum class CipherMode { Unencrypted, AesCtr, AesCbcCts, AesCbc };

struct SubSampleLength {
    uint32_t clearBytes = 0;
    uint32_t encryptedBytes = 0;
};

struct SampleParams {
    CipherMode mode = CipherMode::Unencrypted;
    Bytes key;
    Bytes iv;
    // Pattern is used only when both values are non-zero (cens / cbcs).
    uint32_t encryptBlocks = 0;
    uint32_t skipBlocks = 0;
    std::vector<SubSampleLength> subSamples;
};

// Decrypts a CENC style sample described by subsamples.
//   AesCtr : counter continues across subsamples; optional pattern (cens).
//   AesCbc : IV restarts for each subsample; optional pattern (cbcs); a trailing
//            partial block is left clear.
// src and dst may be the same memory. Returns FrameTooLarge when the output
// does not fit and Decrypt when the input is shorter than the subsamples.
Err decryptSample(const SampleParams& params, const uint8_t* src, size_t srcSize,
        uint8_t* dst, size_t dstSize, size_t* written);

// Generic crypto used by IDrmPlugin::encrypt/decrypt/sign/verify. Algorithm
// names are JCA style and case insensitive; the canonical functions return an
// empty string for unsupported names.
//   Cipher algorithms: "AES/CTR/NoPadding", "AES/CBC/NoPadding"
//   MAC algorithm    : "HmacSHA256"
std::string canonicalCipherAlgorithm(const std::string& algorithm);
std::string canonicalMacAlgorithm(const std::string& algorithm);
Err aesCrypt(bool encrypt, const std::string& algorithm, const Bytes& key,
        const Bytes& iv, const Bytes& input, Bytes* output);
Err hmacSha256(const Bytes& key, const Bytes& message, Bytes* mac);
bool constantTimeEquals(const Bytes& a, const Bytes& b);

}  // namespace rdk::clearkey

#endif
