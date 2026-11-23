// SPDX-License-Identifier: Apache-2.0
#include "cipher.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

namespace rdk::clearkey {

namespace {

constexpr size_t kBlockSize = 16;

struct CtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using Ctx = std::unique_ptr<EVP_CIPHER_CTX, CtxDeleter>;

Ctx makeDecryptor(const EVP_CIPHER* cipher, const Bytes& key, const Bytes& iv) {
    Ctx ctx(EVP_CIPHER_CTX_new());
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, key.data(), iv.data()) != 1) {
        return nullptr;
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    return ctx;
}

bool update(EVP_CIPHER_CTX* ctx, const uint8_t* in, uint8_t* out, size_t size) {
    while (size > 0) {
        const int chunk = static_cast<int>(std::min<size_t>(size, 1u << 20));
        int produced = 0;
        if (EVP_DecryptUpdate(ctx, out, &produced, in, chunk) != 1 || produced != chunk) {
            return false;
        }
        in += chunk;
        out += chunk;
        size -= static_cast<size_t>(chunk);
    }
    return true;
}

bool ctrRegion(EVP_CIPHER_CTX* ctx, const SampleParams& p, const uint8_t* in, uint8_t* out,
        size_t size) {
    if (p.encryptBlocks == 0 || p.skipBlocks == 0) {
        return update(ctx, in, out, size);
    }
    const size_t encryptRun = static_cast<size_t>(p.encryptBlocks) * kBlockSize;
    const size_t skipRun = static_cast<size_t>(p.skipBlocks) * kBlockSize;
    while (size > 0) {
        const size_t crypt = std::min(size, encryptRun);
        if (!update(ctx, in, out, crypt)) {
            return false;
        }
        in += crypt;
        out += crypt;
        size -= crypt;
        const size_t skip = std::min(size, skipRun);
        std::memmove(out, in, skip);
        in += skip;
        out += skip;
        size -= skip;
    }
    return true;
}

bool cbcRegion(EVP_CIPHER_CTX* ctx, const SampleParams& p, const uint8_t* in, uint8_t* out,
        size_t size) {
    size_t blocks = size / kBlockSize;
    const size_t tail = size % kBlockSize;
    if (p.encryptBlocks == 0 || p.skipBlocks == 0) {
        if (!update(ctx, in, out, blocks * kBlockSize)) {
            return false;
        }
        in += blocks * kBlockSize;
        out += blocks * kBlockSize;
    } else {
        while (blocks > 0) {
            const size_t crypt = std::min<size_t>(blocks, p.encryptBlocks);
            if (!update(ctx, in, out, crypt * kBlockSize)) {
                return false;
            }
            in += crypt * kBlockSize;
            out += crypt * kBlockSize;
            blocks -= crypt;
            const size_t skip = std::min<size_t>(blocks, p.skipBlocks);
            std::memmove(out, in, skip * kBlockSize);
            in += skip * kBlockSize;
            out += skip * kBlockSize;
            blocks -= skip;
        }
    }
    std::memmove(out, in, tail);
    return true;
}

}  // namespace

Err decryptSample(const SampleParams& params, const uint8_t* src, size_t srcSize,
        uint8_t* dst, size_t dstSize, size_t* written) {
    *written = 0;
    uint64_t total = 0;
    for (const SubSampleLength& s : params.subSamples) {
        total += static_cast<uint64_t>(s.clearBytes) + s.encryptedBytes;
    }
    if (total > srcSize) {
        return Err::Decrypt;
    }
    if (total > dstSize) {
        return Err::FrameTooLarge;
    }
    if (src == nullptr || dst == nullptr) {
        return Err::Decrypt;
    }

    if (params.mode == CipherMode::Unencrypted) {
        std::memmove(dst, src, static_cast<size_t>(total));
        *written = static_cast<size_t>(total);
        return Err::Ok;
    }
    if (params.mode == CipherMode::AesCbcCts) {
        return Err::CannotHandle;
    }
    if (params.key.size() != kBlockSize || params.iv.size() != kBlockSize) {
        return Err::Decrypt;
    }

    const bool ctr = params.mode == CipherMode::AesCtr;
    Ctx ctrCtx;
    if (ctr) {
        ctrCtx = makeDecryptor(EVP_aes_128_ctr(), params.key, params.iv);
        if (!ctrCtx) {
            return Err::Decrypt;
        }
    }

    size_t offset = 0;
    for (const SubSampleLength& s : params.subSamples) {
        std::memmove(dst + offset, src + offset, s.clearBytes);
        offset += s.clearBytes;
        if (s.encryptedBytes > 0) {
            bool ok;
            if (ctr) {
                ok = ctrRegion(ctrCtx.get(), params, src + offset, dst + offset, s.encryptedBytes);
            } else {
                Ctx cbcCtx = makeDecryptor(EVP_aes_128_cbc(), params.key, params.iv);
                ok = cbcCtx &&
                        cbcRegion(cbcCtx.get(), params, src + offset, dst + offset, s.encryptedBytes);
            }
            if (!ok) {
                return Err::Decrypt;
            }
        }
        offset += s.encryptedBytes;
    }
    *written = offset;
    return Err::Ok;
}

namespace {

std::string lowerCase(const std::string& text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

std::string canonicalCipherAlgorithm(const std::string& algorithm) {
    const std::string name = lowerCase(algorithm);
    if (name == "aes/ctr/nopadding") {
        return "AES/CTR/NoPadding";
    }
    if (name == "aes/cbc/nopadding") {
        return "AES/CBC/NoPadding";
    }
    return "";
}

std::string canonicalMacAlgorithm(const std::string& algorithm) {
    return lowerCase(algorithm) == "hmacsha256" ? "HmacSHA256" : "";
}

Err aesCrypt(bool encrypt, const std::string& algorithm, const Bytes& key, const Bytes& iv,
        const Bytes& input, Bytes* output) {
    output->clear();
    if (key.size() != kBlockSize || iv.size() != kBlockSize) {
        return Err::BadValue;
    }
    const EVP_CIPHER* cipher = nullptr;
    if (algorithm == "AES/CTR/NoPadding") {
        cipher = EVP_aes_128_ctr();
    } else if (algorithm == "AES/CBC/NoPadding") {
        cipher = EVP_aes_128_cbc();
        if (input.size() % kBlockSize != 0) {
            return Err::BadValue;
        }
    } else {
        return Err::CannotHandle;
    }
    Ctx ctx(EVP_CIPHER_CTX_new());
    if (!ctx ||
            EVP_CipherInit_ex(ctx.get(), cipher, nullptr, key.data(), iv.data(), encrypt ? 1 : 0) != 1) {
        return Err::InvalidState;
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    output->resize(input.size());
    int produced = 0;
    if (!input.empty() &&
            (EVP_CipherUpdate(ctx.get(), output->data(), &produced, input.data(),
                    static_cast<int>(input.size())) != 1 ||
             static_cast<size_t>(produced) != input.size())) {
        output->clear();
        return Err::InvalidState;
    }
    return Err::Ok;
}

Err hmacSha256(const Bytes& key, const Bytes& message, Bytes* mac) {
    mac->assign(EVP_MAX_MD_SIZE, 0);
    unsigned int length = 0;
    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), message.data(),
                message.size(), mac->data(), &length) == nullptr) {
        mac->clear();
        return Err::InvalidState;
    }
    mac->resize(length);
    return Err::Ok;
}

bool constantTimeEquals(const Bytes& a, const Bytes& b) {
    return a.size() == b.size() &&
            (a.empty() || CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0);
}

}  // namespace rdk::clearkey
