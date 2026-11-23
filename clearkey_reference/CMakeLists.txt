cmake_minimum_required(VERSION 3.10)
project(rdk_clearkey_reference CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas -Wno-attributes)

find_package(OpenSSL QUIET COMPONENTS Crypto)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR
        "ClearKey requires target OpenSSL development headers and libcrypto. "
        "Selected CMake sysroot: '${CMAKE_SYSROOT}'. "
        "For an OE/Yocto SDK, include openssl-dev in TOOLCHAIN_TARGET_TASK and "
        "regenerate/reinstall the SDK. Alternatively provide OPENSSL_INCLUDE_DIR "
        "and OPENSSL_CRYPTO_LIBRARY from a matching target OpenSSL installation. "
        "Do not use x86 host libraries for an ARM build.")
endif()
find_package(Threads REQUIRED)

# Transport independent ClearKey logic: init data, license parsing, sessions, AES.
add_library(clearkey_core STATIC
    core/base64.cpp
    core/json.cpp
    core/license.cpp
    core/cipher.cpp
    core/session.cpp)
target_include_directories(clearkey_core PUBLIC core)
target_link_libraries(clearkey_core PUBLIC OpenSSL::Crypto Threads::Threads)
set_target_properties(clearkey_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_executable(clearkey_core_test tests/clearkey_core_test.cpp)
target_link_libraries(clearkey_core_test PRIVATE clearkey_core)
add_test(NAME clearkey_core COMMAND clearkey_core_test)

# The Binder service needs the platform Binder SDK, the generated DRM AIDL C++
# code and the AVBuffer helper (or a link-time placeholder), so it is opt-in:
#
#   cmake -DCLEARKEY_BUILD_SERVICE=ON \
#         -DBINDER_INCLUDE_DIR=<dir containing binder/, utils/, ...> \
#         -DBINDER_LIB_DIR=<dir with libbinder, libutils, libcutils, liblog, libbase> \
#         -DDRM_AIDL_INCLUDE_DIR=<.../drm/<version>/include> \
#         -DDRM_AIDL_SOURCE_DIR=<.../drm/<version>/src/com/rdk/hal/drm>  (or -DDRM_AIDL_LIBRARY=...) \
#         -DAVBUFFERHELPER_INCLUDE_DIR=<dir with avbufferhelper.h or IAVBufferHelper.h> \
#         -DAVBUFFERHELPER_LIBRARY=<libavbufferhelper.so> ..
option(CLEARKEY_BUILD_SERVICE "Build the Binder ClearKey service" OFF)
option(CLEARKEY_BUILD_HELPER_SKELETON "Build a link-time-only AVBuffer helper placeholder" OFF)
if(CLEARKEY_BUILD_SERVICE)
    set(CLEARKEY_REQUIRED_PATHS BINDER_INCLUDE_DIR BINDER_LIB_DIR DRM_AIDL_INCLUDE_DIR
        AVBUFFERHELPER_INCLUDE_DIR)
    if(NOT CLEARKEY_BUILD_HELPER_SKELETON)
        list(APPEND CLEARKEY_REQUIRED_PATHS AVBUFFERHELPER_LIBRARY)
    endif()
    foreach(required ${CLEARKEY_REQUIRED_PATHS})
        if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
            message(FATAL_ERROR "CLEARKEY_BUILD_SERVICE requires -D${required}=...")
        endif()
    endforeach()
    if(EXISTS "${AVBUFFERHELPER_INCLUDE_DIR}/avbufferhelper.h")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/helper_include")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/helper_include/IAVBufferHelper.h"
            "#include <avbufferhelper.h>\n")
        set(AVBUFFERHELPER_COMPAT_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/helper_include")
    elseif(NOT EXISTS "${AVBUFFERHELPER_INCLUDE_DIR}/IAVBufferHelper.h")
        message(FATAL_ERROR "AVBuffer helper header not found in ${AVBUFFERHELPER_INCLUDE_DIR}")
    endif()
    if("${DRM_AIDL_SOURCE_DIR}" STREQUAL "" AND "${DRM_AIDL_LIBRARY}" STREQUAL "")
        message(FATAL_ERROR "Set DRM_AIDL_SOURCE_DIR or DRM_AIDL_LIBRARY")
    endif()

    set(DRM_AIDL_SOURCES "")
    if(NOT "${DRM_AIDL_SOURCE_DIR}" STREQUAL "")
        file(GLOB DRM_AIDL_SOURCES "${DRM_AIDL_SOURCE_DIR}/*.cpp")
    endif()

    set(BINDER_LIBRARIES "")
    foreach(lib binder utils cutils log base)
        unset(BINDER_${lib}_LIB CACHE)
        find_library(BINDER_${lib}_LIB ${lib} PATHS ${BINDER_LIB_DIR}
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        if(NOT BINDER_${lib}_LIB)
            message(FATAL_ERROR "Missing lib${lib} in BINDER_LIB_DIR=${BINDER_LIB_DIR}")
        endif()
        list(APPEND BINDER_LIBRARIES ${BINDER_${lib}_LIB})
    endforeach()

    add_library(clearkey_binder STATIC
        service/ClearKeyDrmFactory.cpp
        service/ClearKeyDrmPlugin.cpp
        service/ClearKeyCryptoPlugin.cpp
        ${DRM_AIDL_SOURCES})
    target_include_directories(clearkey_binder PUBLIC service
        ${BINDER_INCLUDE_DIR} ${DRM_AIDL_INCLUDE_DIR}
        ${AVBUFFERHELPER_COMPAT_INCLUDE} ${AVBUFFERHELPER_INCLUDE_DIR})
    target_link_libraries(clearkey_binder PUBLIC clearkey_core ${BINDER_LIBRARIES}
        ${DRM_AIDL_LIBRARY})
    set_target_properties(clearkey_binder PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_executable(clearkey_service service/clearkey_service.cpp)
    if(CLEARKEY_BUILD_HELPER_SKELETON)
        add_library(avbufferhelper SHARED service/avbufferhelper_skeleton.cpp)
        target_include_directories(avbufferhelper PRIVATE
            ${AVBUFFERHELPER_COMPAT_INCLUDE} ${AVBUFFERHELPER_INCLUDE_DIR})
        target_link_libraries(clearkey_service PRIVATE clearkey_binder avbufferhelper)
        install(TARGETS avbufferhelper LIBRARY DESTINATION lib)
    else()
        target_link_libraries(clearkey_service PRIVATE clearkey_binder ${AVBUFFERHELPER_LIBRARY})
    endif()

    # Calls the plugins in-process with a fake AVBuffer helper; needs no binder driver.
    add_executable(clearkey_binder_test tests/clearkey_binder_test.cpp)
    target_link_libraries(clearkey_binder_test PRIVATE clearkey_binder)
    add_test(NAME clearkey_binder COMMAND clearkey_binder_test)

    install(TARGETS clearkey_service RUNTIME DESTINATION bin)
endif()
