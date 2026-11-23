// SPDX-License-Identifier: Apache-2.0
#include "ClearKeyCryptoPlugin.h"

#include <IAVBufferHelper.h>

#include <com/rdk/hal/drm/DecryptArgs.h>
#include <com/rdk/hal/drm/Mode.h>

#include "binder_status.h"
#include "cipher.h"
#include "session.h"

using com::rdk::hal::drm::DecryptArgs;
using com::rdk::hal::drm::Mode;

namespace rdk::clearkey {

namespace {

// Maps an AVBuffer handle for the lifetime of the object.
class MappedBuffer {
public:
    MappedBuffer(IAVBufferHelper* helper, uint64_t handle) : mHelper(helper), mHandle(handle) {
        if (mHelper != nullptr) {
            mData = static_cast<uint8_t*>(mHelper->mapHandle(handle, &mSize));
        }
    }
    ~MappedBuffer() {
        if (mData != nullptr) {
            mHelper->unmapHandle(mHandle);
        }
    }
    MappedBuffer(const MappedBuffer&) = delete;
    MappedBuffer& operator=(const MappedBuffer&) = delete;

    uint8_t* data() const { return mData; }
    uint32_t size() const { return mSize; }

private:
    IAVBufferHelper* mHelper;
    uint64_t mHandle;
    uint8_t* mData = nullptr;
    uint32_t mSize = 0;
};

bool toCipherMode(Mode mode, CipherMode* out) {
    switch (mode) {
        case Mode::UNENCRYPTED: *out = CipherMode::Unencrypted; return true;
        case Mode::AES_CTR: *out = CipherMode::AesCtr; return true;
        case Mode::AES_CBC_CTS: *out = CipherMode::AesCbcCts; return true;
        case Mode::AES_CBC: *out = CipherMode::AesCbc; return true;
    }
    return false;
}

}  // namespace

ClearKeyCryptoPlugin::ClearKeyCryptoPlugin(const Bytes& sessionId) : mSessionId(sessionId) {}

::android::binder::Status ClearKeyCryptoPlugin::setMediaDrmSession(const Bytes& sessionId) {
    if (sessionId.empty() || !SessionLibrary::instance().contains(sessionId)) {
        return toStatus(Err::SessionNotOpened);
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mSessionId = sessionId;
    return Status::ok();
}

::android::binder::Status ClearKeyCryptoPlugin::notifyResolution(int32_t, int32_t) {
    return Status::ok();
}

::android::binder::Status ClearKeyCryptoPlugin::requiresSecureDecoderComponent(
        const std::string&, bool* _aidl_return) {
    *_aidl_return = false;
    return Status::ok();
}

::android::binder::Status ClearKeyCryptoPlugin::decrypt(
        const DecryptArgs& args, int32_t* _aidl_return) {
    Bytes sessionId;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        sessionId = mSessionId;
    }
    if (sessionId.empty() || !SessionLibrary::instance().contains(sessionId)) {
        return toStatus(Err::SessionNotOpened);
    }
    if (args.secure) {
        // Software ClearKey cannot write into secure output buffers.
        return toStatus(Err::CannotHandle);
    }

    SampleParams params;
    if (!toCipherMode(args.mode, &params.mode)) {
        return toStatus(Err::CannotHandle);
    }
    if (args.subSamples.empty()) {
        return toStatus(Err::ZeroSubSamples);
    }
    for (const auto& subSample : args.subSamples) {
        if (subSample.numBytesOfClearData < 0 || subSample.numBytesOfEncryptedData < 0) {
            return toStatus(Err::Decrypt);
        }
        params.subSamples.push_back({static_cast<uint32_t>(subSample.numBytesOfClearData),
                static_cast<uint32_t>(subSample.numBytesOfEncryptedData)});
    }
    if (args.pattern.encryptBlocks < 0 || args.pattern.skipBlocks < 0) {
        return toStatus(Err::Decrypt);
    }
    params.encryptBlocks = static_cast<uint32_t>(args.pattern.encryptBlocks);
    params.skipBlocks = static_cast<uint32_t>(args.pattern.skipBlocks);

    if (params.mode != CipherMode::Unencrypted) {
        const Err err = SessionLibrary::instance().findKey(sessionId, args.keyId, &params.key);
        if (err != Err::Ok) {
            return toStatus(err);
        }
        params.iv = args.iv;
    }

    IAVBufferHelper* helper = getAVBufferHelperInstance();
    if (helper == nullptr) {
        return toStatus(Err::InvalidState);
    }
    const uint64_t sourceHandle = static_cast<uint64_t>(args.sourceBufferHandle);
    const uint64_t destinationHandle = static_cast<uint64_t>(args.destinationBufferHandle);
    const bool inPlace = sourceHandle == destinationHandle;

    MappedBuffer source(helper, sourceHandle);
    if (source.data() == nullptr) {
        return toStatus(Err::Decrypt);
    }
    // Mapping the same handle twice is avoided; decrypt in place instead.
    MappedBuffer destination(inPlace ? nullptr : helper, destinationHandle);
    uint8_t* destinationData = inPlace ? source.data() : destination.data();
    const uint32_t destinationSize = inPlace ? source.size() : destination.size();
    if (destinationData == nullptr) {
        return toStatus(Err::Decrypt);
    }

    size_t written = 0;
    const Err err = decryptSample(params, source.data(), source.size(), destinationData,
            destinationSize, &written);
    if (err != Err::Ok) {
        return toStatus(err);
    }
    *_aidl_return = static_cast<int32_t>(written);
    return Status::ok();
}

}  // namespace rdk::clearkey
