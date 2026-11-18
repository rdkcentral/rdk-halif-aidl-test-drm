/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VTS_DRM_AVBUFFER_H
#define VTS_DRM_AVBUFFER_H

#include <binder/ProcessState.h>
#include <com/rdk/hal/audiodecoder/IAudioDecoder.h>
#include <com/rdk/hal/avbuffer/BnAVBufferSpaceListener.h>
#include <com/rdk/hal/avbuffer/IAVBuffer.h>
#include <com/rdk/hal/avbuffer/Pool.h>
#include <com/rdk/hal/drm/DecryptArgs.h>
#include <com/rdk/hal/drm/Mode.h>
#include <com/rdk/hal/drm/Pattern.h>
#include <com/rdk/hal/drm/SubSample.h>
#include <avbufferhelper.h>
#include <ut_log.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace vts_drm {

using android::defaultServiceManager;
using android::interface_cast;
using android::sp;
using android::String16;
using android::binder::Status;
using com::rdk::hal::audiodecoder::IAudioDecoder;
using com::rdk::hal::avbuffer::BnAVBufferSpaceListener;
using com::rdk::hal::avbuffer::IAVBuffer;
using com::rdk::hal::avbuffer::IAVBufferSpaceListener;
using com::rdk::hal::avbuffer::Pool;
using com::rdk::hal::drm::DecryptArgs;
using com::rdk::hal::drm::Mode;
using com::rdk::hal::drm::Pattern;
using com::rdk::hal::drm::SubSample;

class BufferSpaceListener final : public BnAVBufferSpaceListener {
public:
    Status onSpaceAvailable() override {
        return Status::ok();
    }
};

class SampleBuffers {
public:
    bool open(size_t sourceSize, size_t destinationSize = 0) {
        if (sourceSize == 0 ||
                sourceSize > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            UT_LOG_ERROR("Invalid AVBuffer source size");
            return false;
        }
        if (destinationSize == 0) {
            destinationSize = sourceSize;
        }
        if (destinationSize > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            UT_LOG_ERROR("Invalid AVBuffer destination size");
            return false;
        }

        mManager = interface_cast<IAVBuffer>(
                defaultServiceManager()->getService(
                        String16(IAVBuffer::serviceName().c_str())));
        if (mManager == nullptr) {
            UT_LOG_ERROR("AVBuffer Service not available");
            return false;
        }

        mHelper = getAVBufferHelperInstance();
        if (mHelper == nullptr) {
            UT_LOG_ERROR("AVBuffer helper is unavailable");
            return false;
        }

        android::ProcessState::self()->startThreadPool();
        mListener = new BufferSpaceListener();
        IAudioDecoder::Id decoderId;
        decoderId.value = IAudioDecoder::Id::UNDEFINED;
        Status status = mManager->createAudioPool(false, decoderId, mListener, &mPool);
        if (!status.isOk()) {
            UT_LOG_ERROR("Unable to create a non-secure audio AVBuffer pool");
            return false;
        }
        mPoolCreated = true;

        status = mManager->alloc(mPool, static_cast<int32_t>(sourceSize), &mSourceHandle);
        if (!status.isOk()) {
            UT_LOG_ERROR("Unable to allocate the encrypted AVBuffer");
            return false;
        }
        mSourceAllocated = true;
        status = mManager->alloc(
                mPool, static_cast<int32_t>(destinationSize), &mDestinationHandle);
        if (!status.isOk()) {
            UT_LOG_ERROR("Unable to allocate the decrypted AVBuffer");
            return false;
        }
        mDestinationAllocated = true;
        return true;
    }

    bool writeSource(const std::vector<uint8_t>& data) {
        uint32_t mappedSize = 0;
        void* mapped = mHelper->mapHandle(
                static_cast<uint64_t>(mSourceHandle), &mappedSize);
        if (mapped == nullptr) {
            UT_LOG_ERROR("Unable to map the encrypted AVBuffer");
            return false;
        }
        if (mappedSize < data.size()) {
            const bool unmapped =
                    mHelper->unmapHandle(static_cast<uint64_t>(mSourceHandle));
            UT_LOG_ERROR("Encrypted AVBuffer mapping is too small");
            if (!unmapped) {
                UT_LOG_ERROR("Unable to unmap the encrypted AVBuffer");
            }
            return false;
        }
        std::memcpy(mapped, data.data(), data.size());
        if (!mHelper->unmapHandle(static_cast<uint64_t>(mSourceHandle))) {
            UT_LOG_ERROR("Unable to unmap the encrypted AVBuffer");
            return false;
        }
        return true;
    }

    bool readSource(std::vector<uint8_t>* data, size_t size) {
        return readHandle(mSourceHandle, "encrypted", data, size);
    }

    bool readDestination(std::vector<uint8_t>* data, size_t size) {
        return readHandle(mDestinationHandle, "decrypted", data, size);
    }

    int64_t sourceHandle() const {
        return mSourceHandle;
    }

    DecryptArgs decryptArgs(
            const std::vector<uint8_t>& keyId,
            const std::vector<uint8_t>& iv,
            Mode mode,
            const std::vector<SubSample>& subSamples,
            int32_t encryptBlocks = 0,
            int32_t skipBlocks = 0) const {
        DecryptArgs args;
        args.secure = false;
        args.keyId = keyId;
        args.iv = iv;
        args.mode = mode;
        args.pattern.encryptBlocks = encryptBlocks;
        args.pattern.skipBlocks = skipBlocks;
        args.subSamples = subSamples;
        args.sourceBufferHandle = mSourceHandle;
        args.destinationBufferHandle = mDestinationHandle;
        return args;
    }

    bool cleanup() {
        bool success = true;
        if (mSourceAllocated) {
            bool freed = false;
            const Status status = mManager->free(mSourceHandle, &freed);
            if (!status.isOk() || !freed) {
                UT_LOG_ERROR("Unable to free the encrypted AVBuffer");
                success = false;
            } else {
                mSourceAllocated = false;
            }
        }
        if (mDestinationAllocated) {
            bool freed = false;
            const Status status = mManager->free(mDestinationHandle, &freed);
            if (!status.isOk() || !freed) {
                UT_LOG_ERROR("Unable to free the decrypted AVBuffer");
                success = false;
            } else {
                mDestinationAllocated = false;
            }
        }
        if (mPoolCreated && !mSourceAllocated && !mDestinationAllocated) {
            bool destroyed = false;
            const Status status = mManager->destroyPool(mPool, &destroyed);
            if (!status.isOk() || !destroyed) {
                UT_LOG_ERROR("Unable to destroy the AVBuffer pool");
                success = false;
            } else {
                mPoolCreated = false;
            }
        }
        return success;
    }

    ~SampleBuffers() {
        if (!cleanup()) {
            UT_LOG_ERROR("AVBuffer cleanup failed");
        }
    }

private:
    bool readHandle(int64_t handle, const char* name, std::vector<uint8_t>* data, size_t size) {
        uint32_t mappedSize = 0;
        void* mapped = mHelper->mapHandle(static_cast<uint64_t>(handle), &mappedSize);
        if (mapped == nullptr) {
            UT_LOG_ERROR("Unable to map the %s AVBuffer", name);
            return false;
        }
        if (mappedSize < size) {
            const bool unmapped = mHelper->unmapHandle(static_cast<uint64_t>(handle));
            UT_LOG_ERROR("%s AVBuffer mapping is too small", name);
            if (!unmapped) {
                UT_LOG_ERROR("Unable to unmap the %s AVBuffer", name);
            }
            return false;
        }
        data->assign(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + size);
        if (!mHelper->unmapHandle(static_cast<uint64_t>(handle))) {
            UT_LOG_ERROR("Unable to unmap the %s AVBuffer", name);
            return false;
        }
        return true;
    }

    sp<IAVBuffer> mManager;
    sp<IAVBufferSpaceListener> mListener;
    IAVBufferHelper* mHelper = nullptr;
    Pool mPool;
    int64_t mSourceHandle = -1;
    int64_t mDestinationHandle = -1;
    bool mPoolCreated = false;
    bool mSourceAllocated = false;
    bool mDestinationAllocated = false;
};

}  // namespace vts_drm

#endif
