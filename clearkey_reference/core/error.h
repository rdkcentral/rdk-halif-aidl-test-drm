// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_ERROR_H
#define RDK_CLEARKEY_ERROR_H

namespace rdk::clearkey {

// Transport-independent result codes; the Binder layer maps them to the
// DrmErrors or BAD_VALUE service-specific codes.
enum class Err {
    Ok,
    BadValue,
    SessionNotOpened,
    NoLicense,
    CannotHandle,
    Decrypt,
    FrameTooLarge,
    ZeroSubSamples,
    LicenseParse,
    InitData,
    ResourceBusy,
    ResourceContention,
    InvalidState,
    NotProvisioned,
};

}  // namespace rdk::clearkey

#endif
