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

#ifndef DRM_STRING_COMPAT_H
#define DRM_STRING_COMPAT_H

#include <com/rdk/hal/drm/IDrmPlugin.h>
#include <utils/String8.h>

#include <string>
#include <type_traits>

namespace rdk::drm_compat {

template <typename>
struct FirstMethodArgument;

template <typename Result, typename Class, typename First, typename... Rest>
struct FirstMethodArgument<Result (Class::*)(First, Rest...)> {
    using Type = std::remove_cv_t<std::remove_reference_t<First>>;
};

using DrmString = typename FirstMethodArgument<
        decltype(&::com::rdk::hal::drm::IDrmPlugin::getPropertyString)>::Type;

template <typename Target>
Target fromUtf8As(const std::string& value) {
    if constexpr (std::is_same_v<Target, std::string>) {
        return Target(value);
    } else {
        return Target(::android::String16(::android::String8(value.c_str())));
    }
}

template <typename Source>
std::string toUtf8From(const Source& value) {
    if constexpr (std::is_same_v<Source, std::string>) {
        return value;
    } else {
        return ::android::String8(value).c_str();
    }
}

inline DrmString fromUtf8(const std::string& value) {
    return fromUtf8As<DrmString>(value);
}

inline DrmString fromUtf8(const char* value) {
    return fromUtf8(std::string(value));
}

inline std::string toUtf8(const DrmString& value) {
    return toUtf8From(value);
}

}  // namespace rdk::drm_compat

#endif
