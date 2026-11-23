// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_SESSION_H
#define RDK_CLEARKEY_SESSION_H

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "error.h"
#include "license.h"

namespace rdk::clearkey {

// Process-wide session table shared by every DRM plugin and crypto plugin
// instance, so that a crypto plugin can resolve a MediaDrm session ID.
class SessionLibrary {
public:
    static constexpr size_t kMaxSessions = 16;

    static SessionLibrary& instance();

    Err open(Bytes* sessionId);
    Err close(const Bytes& sessionId);
    bool contains(const Bytes& sessionId);
    size_t count();

    Err addKeys(const Bytes& sessionId, const std::vector<KeyEntry>& keys);
    Err removeKeys(const Bytes& sessionId);
    Err keyCount(const Bytes& sessionId, size_t* count);
    Err findKey(const Bytes& sessionId, const Bytes& keyId, Bytes* key);

    Err setCipherAlgorithm(const Bytes& sessionId, const std::string& algorithm);
    Err setMacAlgorithm(const Bytes& sessionId, const std::string& algorithm);
    Err setPlaybackId(const Bytes& sessionId, const std::string& playbackId);
    Err getCipherAlgorithm(const Bytes& sessionId, std::string* algorithm);
    Err getMacAlgorithm(const Bytes& sessionId, std::string* algorithm);

private:
    struct Session {
        std::map<Bytes, Bytes> keys;
        std::string cipherAlgorithm = "AES/CTR/NoPadding";
        std::string macAlgorithm = "HmacSHA256";
        std::string playbackId;
    };

    Session* find(const Bytes& sessionId);

    std::mutex mMutex;
    std::map<Bytes, Session> mSessions;
};

}  // namespace rdk::clearkey

#endif
