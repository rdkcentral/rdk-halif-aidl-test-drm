// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_ERROR_H
#define RDK_CLEARKEY_ERROR_H

namespace rdk::clearkey {

// Transport-independent result codes. Every DrmErrors value from
// com.rdk.hal.drm.DrmErrors (DRM 0.2.0.0, release 0.23.0) is mirrored here with
// the same numeric value; the Binder layer static_asserts that they match the
// generated header. BadValue is android::BAD_VALUE, which DRM 0.2.0.0 reports
// through EX_SERVICE_SPECIFIC alongside the DrmErrors codes.
enum class Err : int {
    Ok = 0,
    BadValue = -22,
    Unknown = -2000,
    NoLicense = -2001,
    LicenseExpired = -2002,
    SessionNotOpened = -2003,
    DecryptUnitNotInitialized = -2004,
    Decrypt = -2005,
    CannotHandle = -2006,
    TamperDetected = -2007,
    NotProvisioned = -2008,
    DeviceRevoked = -2009,
    ResourceBusy = -2010,
    InsufficientOutputProtection = -2011,
    InsufficientSecurity = -2012,
    FrameTooLarge = -2013,
    ResourceContention = -2014,
    SessionLostState = -2015,
    InvalidState = -2016,
    CertificateMalformed = -2017,
    CertificateMissing = -2018,
    CryptoLibrary = -2019,
    GenericOem = -2020,
    GenericPlugin = -2021,
    InitData = -2022,
    KeyNotLoaded = -2023,
    LicenseParse = -2024,
    LicensePolicy = -2025,
    LicenseRelease = -2026,
    LicenseRequestRejected = -2027,
    LicenseRestore = -2028,
    LicenseState = -2029,
    MediaFramework = -2030,
    ProvisioningCertificate = -2031,
    ProvisioningConfig = -2032,
    ProvisioningParse = -2033,
    ProvisioningRequestRejected = -2034,
    ProvisioningRetry = -2035,
    SecureStopRelease = -2036,
    StorageRead = -2037,
    StorageWrite = -2038,
    ZeroSubSamples = -2039,
};

}  // namespace rdk::clearkey

#endif
