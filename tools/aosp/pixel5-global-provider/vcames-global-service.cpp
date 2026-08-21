/*
 * Copyright 2026 VCamES contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "VCamESGlobalProvider"

#include "CameraProvider_2_4.h"
#include "ExternalCameraProviderImpl_2_4.h"

#include <android/hardware/camera/provider/2.4/ICameraProvider.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

using android::ProcessState;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::camera::provider::V2_4::ICameraProvider;
using android::hardware::camera::provider::V2_4::implementation::CameraProvider;
using android::hardware::camera::provider::V2_4::implementation::
        ExternalCameraProviderImpl_2_4;
using android::sp;

int main() {
    ProcessState::initWithDriver("/dev/vndbinder");
    configureRpcThreadpool(6, true);
    using Provider = CameraProvider<ExternalCameraProviderImpl_2_4>;
    sp<Provider> implementation = new Provider();
    if (implementation == nullptr || implementation->isInitFailed()) {
        ALOGE("global provider initialization failed");
        return 1;
    }
    sp<ICameraProvider> provider = implementation;
    const auto status = provider->registerAsService("legacy/0");
    if (status != android::OK) {
        ALOGE("legacy/0 registration failed: %d", status);
        return 2;
    }
    ALOGI("legacy/0 registered with global camera IDs 0 and 1");
    joinRpcThreadpool();
    return 3;
}
