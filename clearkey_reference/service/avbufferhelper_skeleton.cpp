// SPDX-License-Identifier: Apache-2.0
#include <IAVBufferHelper.h>

#include <cstdio>

// Link-time placeholder, as in the unified VTS. Real buffer mapping is platform-specific.
IAVBufferHelper* getAVBufferHelperInstance() {
    std::fprintf(stderr, "ClearKey: AVBuffer helper placeholder cannot map buffers; "
                         "load the platform libavbufferhelper.so for L3 decryption\n");
    return nullptr;
}
