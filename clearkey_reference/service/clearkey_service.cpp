// SPDX-License-Identifier: Apache-2.0
// Registers the reference ClearKey IDrmFactory with the service manager.
// The instance name defaults to "clearkey" and can be set with argv[1] or the
// DRM_FACTORY_INSTANCE environment variable (the name the VTS looks up).
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>

#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "ClearKeyDrmFactory.h"

int main(int argc, char* argv[]) {
    std::string instance = "clearkey";
    if (const char* fromEnv = std::getenv("DRM_FACTORY_INSTANCE")) {
        if (fromEnv[0] != '\0') {
            instance = fromEnv;
        }
    }
    if (argc > 1 && argv[1][0] != '\0') {
        instance = argv[1];
    }

    signal(SIGPIPE, SIG_IGN);

    const std::string name = rdk::clearkey::ClearKeyDrmFactory::serviceName(instance);
    ::android::sp<::android::IServiceManager> sm(::android::defaultServiceManager());
    ::android::sp<rdk::clearkey::ClearKeyDrmFactory> factory =
            ::android::sp<rdk::clearkey::ClearKeyDrmFactory>::make();
    const ::android::status_t status = sm->addService(::android::String16(name.c_str()), factory);
    if (status != ::android::OK) {
        std::fprintf(stderr, "Failed to register %s (status %d)\n", name.c_str(), status);
        return EXIT_FAILURE;
    }
    std::printf("Registered %s\n", name.c_str());

    ::android::sp<::android::ProcessState> ps(::android::ProcessState::self());
    ps->startThreadPool();
    ::android::IPCThreadState::self()->joinThreadPool();
    return EXIT_FAILURE;
}
