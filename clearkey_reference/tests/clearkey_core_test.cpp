// SPDX-License-Identifier: Apache-2.0
// Host-side checks of the transport independent ClearKey core. Vectors were
// generated with the OpenSSL command line tool, not with the code under test.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "base64.h"
#include "cipher.h"
#include "json.h"
#include "license.h"
#include "session.h"

using namespace rdk::clearkey;

namespace {

int gFailures = 0;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++gFailures;                                                      \
        }                                                                     \
    } while (0)

Bytes hex(const std::string& text) {
    Bytes out;
    for (size_t i = 0; i + 1 < text.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(text.substr(i, 2), nullptr, 16)));
    }
    return out;
}

const Bytes kKey = hex("000102030405060708090a0b0c0d0e0f");
const Bytes kIv = hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
const Bytes kPlain = hex(
        "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8"
        "ff060d141b222930373e454c535a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6");

SampleParams params(CipherMode mode, std::vector<SubSampleLength> subSamples,
        uint32_t encryptBlocks = 0, uint32_t skipBlocks = 0) {
    SampleParams p;
    p.mode = mode;
    p.key = kKey;
    p.iv = kIv;
    p.subSamples = std::move(subSamples);
    p.encryptBlocks = encryptBlocks;
    p.skipBlocks = skipBlocks;
    return p;
}

Bytes decrypt(const SampleParams& p, const Bytes& sample, Err* err) {
    Bytes out(sample.size());
    size_t written = 0;
    *err = decryptSample(p, sample.data(), sample.size(), out.data(), out.size(), &written);
    out.resize(written);
    return out;
}

void testBase64() {
    Bytes decoded;
    CHECK(base64Decode("AAECAwQFBgcICQoLDA0ODw", &decoded) && decoded == kKey);
    CHECK(base64Decode("AAECAwQFBgcICQoLDA0ODw==", &decoded) && decoded == kKey);
    CHECK(base64UrlEncode(kKey.data(), kKey.size()) == "AAECAwQFBgcICQoLDA0ODw");
    const Bytes high = {0xfb, 0xff, 0xfe};
    CHECK(base64UrlEncode(high.data(), high.size()) == "-__-");
    CHECK(base64Decode("-__-", &decoded) && decoded == high);
    CHECK(!base64Decode("AA*A", &decoded));
    CHECK(!base64Decode("A", &decoded));
}

void testJson() {
    JsonValue v;
    CHECK(parseJson("{\"a\":[1,true,null,\"x\\u0041\"],\"b\":{}}", &v));
    CHECK(v.find("a") != nullptr && v.find("a")->array.size() == 4);
    CHECK(v.find("a")->array[3].string == "xA");
    CHECK(!parseJson("{\"a\":", &v));
    CHECK(!parseJson("{\"a\":1} trailing", &v));
    CHECK(!parseJson(std::string(40, '['), &v));
}

Bytes psshBox(const std::array<uint8_t, 16>& systemId, const std::vector<Bytes>& kids) {
    Bytes box;
    const uint32_t size = static_cast<uint32_t>(32 + (kids.empty() ? 0 : 4 + 16 * kids.size()));
    for (int shift = 24; shift >= 0; shift -= 8) box.push_back(static_cast<uint8_t>(size >> shift));
    box.insert(box.end(), {'p', 's', 's', 'h', kids.empty() ? uint8_t{0} : uint8_t{1}, 0, 0, 0});
    box.insert(box.end(), systemId.begin(), systemId.end());
    if (!kids.empty()) {
        box.insert(box.end(), {0, 0, 0, static_cast<uint8_t>(kids.size())});
        for (const Bytes& kid : kids) box.insert(box.end(), kid.begin(), kid.end());
    }
    box.insert(box.end(), {0, 0, 0, 0});
    return box;
}

void testLicense() {
    std::vector<Bytes> kids;
    CHECK(parseInitData(psshBox(kClearKeyUuid, {kKey}), "video/mp4", &kids) == Err::Ok);
    CHECK(kids.size() == 1 && kids[0] == kKey);
    CHECK(parseInitData(psshBox(kCommonPsshUuid, {kKey, kIv}), "cenc", &kids) == Err::Ok);
    CHECK(kids.size() == 2);

    // A PSSH for another DRM system carries no ClearKey key IDs.
    std::array<uint8_t, 16> other = kClearKeyUuid;
    other[0] ^= 0xff;
    CHECK(parseInitData(psshBox(other, {kKey}), "video/mp4", &kids) == Err::InitData);
    CHECK(parseInitData({}, "video/mp4", &kids) == Err::BadValue);
    CHECK(parseInitData(hex("0000002070737368"), "video/mp4", &kids) == Err::InitData);
    CHECK(parseInitData(kKey, "video/webm", &kids) == Err::Ok && kids[0] == kKey);
    CHECK(parseInitData(hex("0102"), "video/webm", &kids) == Err::InitData);
    CHECK(parseInitData(kKey, "text/plain", &kids) == Err::CannotHandle);

    CHECK(buildLicenseRequest({kKey}) == "{\"kids\":[\"AAECAwQFBgcICQoLDA0ODw\"],\"type\":\"temporary\"}");

    std::vector<KeyEntry> keys;
    const std::string good =
            "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"AAECAwQFBgcICQoLDA0ODw\","
            "\"k\":\"8PHy8/T19vf4+fr7/P3+/w\"}],\"type\":\"temporary\"}";
    CHECK(parseLicenseResponse(Bytes(good.begin(), good.end()), &keys) == Err::Ok);
    CHECK(keys.size() == 1 && keys[0].kid == kKey && keys[0].key == kIv);

    const std::string shortKey = "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"AAEC\",\"k\":\"AAEC\"}]}";
    CHECK(parseLicenseResponse(Bytes(shortKey.begin(), shortKey.end()), &keys) == Err::LicenseParse);
    const std::string notJson = "not json";
    CHECK(parseLicenseResponse(Bytes(notJson.begin(), notJson.end()), &keys) == Err::LicenseParse);
    const std::string persistent =
            "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"AAECAwQFBgcICQoLDA0ODw\","
            "\"k\":\"AAECAwQFBgcICQoLDA0ODw\"}],\"type\":\"persistent-license\"}";
    CHECK(parseLicenseResponse(Bytes(persistent.begin(), persistent.end()), &keys) == Err::CannotHandle);
    bool isPersistent = false;
    CHECK(parseLicenseResponse(Bytes(persistent.begin(), persistent.end()), &keys, &isPersistent) == Err::Ok);
    CHECK(isPersistent && keys.size() == 1);
}

void testDecryptSample() {
    Err err;
    CHECK(decrypt(params(CipherMode::AesCtr, {{10, 20}, {5, 35}}),
                  hex("030a11181f262d343b422ff790b6513e423216d95191aeb2061f0b4110ced5dce3eaf14f"
                      "613aa0b0b651925eab21af9e22c83714349de07f0a19c0ef35fbc4cc1bac8be460be"),
                  &err) == kPlain && err == Err::Ok);

    CHECK(decrypt(params(CipherMode::AesCbc, {{8, 40}}),
                  hex("030a11181f262d341984d457466ae10050522be74b82e3e079066d42b6a1e1fdce63fdbd"
                      "c795203f1b222930373e454c"),
                  &err) == Bytes(kPlain.begin(), kPlain.begin() + 48) && err == Err::Ok);

    CHECK(decrypt(params(CipherMode::AesCbc, {{0, 70}}, 1, 1),
                  hex("29c99ea986fa95edf1f01a6a9ba4b1f9737a81888f969da4abb2b9c0c7ced5dc982f7990"
                      "9e55dc6e9e155590fd2d13c5535a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6"),
                  &err) == kPlain && err == Err::Ok);

    CHECK(decrypt(params(CipherMode::AesCtr, {{0, 70}}, 1, 1),
                  hex("65add6f02b741c7cac1397576448c8c1737a81888f969da4abb2b9c0c7ced5dc516b26f8"
                      "489831b9bf8f5a8b59a25aa6535a61686f767d848b9299a0a7aeb5bc11bb438ea3bd"),
                  &err) == kPlain && err == Err::Ok);

    CHECK(decrypt(params(CipherMode::Unencrypted, {{3, 4}}), hex("01020304050607"), &err) ==
            hex("01020304050607") && err == Err::Ok);

    // In-place decryption, as used when source and destination share a buffer.
    Bytes inPlace = hex("030a11181f262d343b422ff790b6513e423216d95191aeb2061f0b4110ced5dce3eaf14f"
                        "613aa0b0b651925eab21af9e22c83714349de07f0a19c0ef35fbc4cc1bac8be460be");
    size_t written = 0;
    CHECK(decryptSample(params(CipherMode::AesCtr, {{10, 20}, {5, 35}}), inPlace.data(),
                  inPlace.size(), inPlace.data(), inPlace.size(), &written) == Err::Ok);
    CHECK(inPlace == kPlain);

    Bytes out(10);
    CHECK(decryptSample(params(CipherMode::AesCtr, {{0, 20}}), kPlain.data(), 20, out.data(),
                  out.size(), &written) == Err::FrameTooLarge);
    CHECK(decryptSample(params(CipherMode::AesCtr, {{0, 80}}), kPlain.data(), 70, out.data(),
                  out.size(), &written) == Err::Decrypt);
    out.resize(16);
    CHECK(decryptSample(params(CipherMode::AesCbcCts, {{0, 16}}), kPlain.data(), 16, out.data(),
                  out.size(), &written) == Err::CannotHandle);
}

void testGenericCrypto() {
    Bytes out;
    CHECK(aesCrypt(false, "AES/CTR/NoPadding", kKey, kIv, hex(
            "65add6f02b741c7cac1397576448c8c1c1fb56883808a1090f1fca7ba952ca36319b63ae"
            "835de689e0a3a26978ac103d2382073253c925c3dcff9ab8e8ce8226a8cb73f0f270"), &out) == Err::Ok);
    CHECK(out == kPlain);

    Bytes cbc;
    CHECK(aesCrypt(true, "AES/CBC/NoPadding", kKey, kIv, Bytes(kPlain.begin(), kPlain.begin() + 48),
                  &cbc) == Err::Ok);
    CHECK(cbc == hex("29c99ea986fa95edf1f01a6a9ba4b1f9990e43bb4c151b5afb7146d2756cde07"
                     "f0a1d6943fe3ae7c0c1e6b5e9aed7cd0"));
    CHECK(aesCrypt(false, "AES/CBC/NoPadding", kKey, kIv, Bytes(3), &out) == Err::BadValue);
    CHECK(aesCrypt(false, "AES/ECB/NoPadding", kKey, kIv, Bytes(16), &out) == Err::CannotHandle);

    Bytes mac;
    CHECK(hmacSha256(kKey, Bytes{'m', 'e', 's', 's', 'a', 'g', 'e'}, &mac) == Err::Ok);
    CHECK(mac == hex("a3e24a124c1900e52c406c42351252b0d07b751bbaa9a44ce6690158ff6f032b"));
    CHECK(constantTimeEquals(mac, mac));
    CHECK(!constantTimeEquals(mac, Bytes(32)));
    CHECK(canonicalCipherAlgorithm("aes/cbc/NOPADDING") == "AES/CBC/NoPadding");
    CHECK(canonicalCipherAlgorithm("AES/CBC/PKCS5Padding").empty());
    CHECK(canonicalMacAlgorithm("hmacsha256") == "HmacSHA256");
    CHECK(canonicalMacAlgorithm("HmacSHA1").empty());
}

void testSessions() {
    SessionLibrary& library = SessionLibrary::instance();
    const size_t before = library.count();
    Bytes id;
    CHECK(library.open(&id) == Err::Ok && id.size() == 16);
    CHECK(library.count() == before + 1);

    Bytes key;
    CHECK(library.findKey(id, kKey, &key) == Err::NoLicense);
    CHECK(library.addKeys(id, {{kKey, kIv}}) == Err::Ok);
    CHECK(library.findKey(id, kKey, &key) == Err::Ok && key == kIv);
    CHECK(library.removeKeys(id) == Err::Ok);
    CHECK(library.findKey(id, kKey, &key) == Err::NoLicense);

    CHECK(library.close(id) == Err::Ok);
    CHECK(library.close(id) == Err::SessionNotOpened);
    CHECK(library.findKey(id, kKey, &key) == Err::SessionNotOpened);
    CHECK(library.count() == before);

    std::vector<Bytes> open(SessionLibrary::kMaxSessions - before);
    for (Bytes& session : open) CHECK(library.open(&session) == Err::Ok);
    Bytes extra;
    CHECK(library.open(&extra) == Err::ResourceBusy);
    for (const Bytes& session : open) CHECK(library.close(session) == Err::Ok);
}

}  // namespace

int main() {
    testBase64();
    testJson();
    testLicense();
    testDecryptSample();
    testGenericCrypto();
    testSessions();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return EXIT_FAILURE;
    }
    std::puts("clearkey core: all checks passed");
    return EXIT_SUCCESS;
}
