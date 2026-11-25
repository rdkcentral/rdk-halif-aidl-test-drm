// SPDX-License-Identifier: Apache-2.0
#include "session.h"

#include <openssl/rand.h>

namespace rdk::clearkey {

SessionLibrary& SessionLibrary::instance() {
    static SessionLibrary library;
    return library;
}

SessionLibrary::Session* SessionLibrary::find(const Bytes& sessionId) {
    const auto it = mSessions.find(sessionId);
    return it == mSessions.end() ? nullptr : &it->second;
}

Err SessionLibrary::open(Bytes* sessionId) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mSessions.size() >= kMaxSessions) {
        return Err::ResourceBusy;
    }
    Bytes id(16);
    do {
        if (RAND_bytes(id.data(), static_cast<int>(id.size())) != 1) {
            return Err::InvalidState;
        }
    } while (mSessions.count(id) != 0);
    mSessions.emplace(id, Session());
    *sessionId = id;
    return Err::Ok;
}

Err SessionLibrary::close(const Bytes& sessionId) {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSessions.erase(sessionId) == 0 ? Err::SessionNotOpened : Err::Ok;
}

bool SessionLibrary::contains(const Bytes& sessionId) {
    std::lock_guard<std::mutex> lock(mMutex);
    return find(sessionId) != nullptr;
}

size_t SessionLibrary::count() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSessions.size();
}

Err SessionLibrary::addKeys(const Bytes& sessionId, const std::vector<KeyEntry>& keys) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    for (const KeyEntry& entry : keys) {
        session->keys[entry.kid] = entry.key;
    }
    return Err::Ok;
}

Err SessionLibrary::storeOfflineKeys(const std::vector<KeyEntry>& keys, Bytes* keySetId) {
    std::lock_guard<std::mutex> lock(mMutex);
    Bytes id(16);
    do {
        if (RAND_bytes(id.data(), static_cast<int>(id.size())) != 1) {
            return Err::InvalidState;
        }
    } while (mOfflineLicenses.count(id) != 0);
    mOfflineLicenses.emplace(id, keys);
    *keySetId = id;
    return Err::Ok;
}

Err SessionLibrary::offlineKeyIds(const Bytes& keySetId, std::vector<Bytes>* keyIds) {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mOfflineLicenses.find(keySetId);
    if (it == mOfflineLicenses.end()) {
        return Err::LicenseRelease;
    }
    keyIds->clear();
    for (const KeyEntry& entry : it->second) {
        keyIds->push_back(entry.kid);
    }
    return Err::Ok;
}

Err SessionLibrary::releaseOfflineKeys(const Bytes& keySetId) {
    std::lock_guard<std::mutex> lock(mMutex);
    return mOfflineLicenses.erase(keySetId) == 0 ? Err::LicenseRelease : Err::Ok;
}

Err SessionLibrary::removeKeys(const Bytes& sessionId) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    session->keys.clear();
    return Err::Ok;
}

Err SessionLibrary::keyCount(const Bytes& sessionId, size_t* count) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    *count = session->keys.size();
    return Err::Ok;
}

Err SessionLibrary::findKey(const Bytes& sessionId, const Bytes& keyId, Bytes* key) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    const auto it = session->keys.find(keyId);
    if (it == session->keys.end()) {
        return Err::NoLicense;
    }
    *key = it->second;
    return Err::Ok;
}

Err SessionLibrary::setCipherAlgorithm(const Bytes& sessionId, const std::string& algorithm) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    session->cipherAlgorithm = algorithm;
    return Err::Ok;
}

Err SessionLibrary::setMacAlgorithm(const Bytes& sessionId, const std::string& algorithm) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    session->macAlgorithm = algorithm;
    return Err::Ok;
}

Err SessionLibrary::setPlaybackId(const Bytes& sessionId, const std::string& playbackId) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    session->playbackId = playbackId;
    return Err::Ok;
}

Err SessionLibrary::getCipherAlgorithm(const Bytes& sessionId, std::string* algorithm) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    *algorithm = session->cipherAlgorithm;
    return Err::Ok;
}

Err SessionLibrary::getMacAlgorithm(const Bytes& sessionId, std::string* algorithm) {
    std::lock_guard<std::mutex> lock(mMutex);
    Session* session = find(sessionId);
    if (session == nullptr) {
        return Err::SessionNotOpened;
    }
    *algorithm = session->macAlgorithm;
    return Err::Ok;
}

}  // namespace rdk::clearkey
