#ifndef PROTECTION_SECURITY_BYPASS_H
#define PROTECTION_SECURITY_BYPASS_H

#include <cstdint>
#include <cstddef>
#include "../Struct/Bools.h"

void* Current_Local_Player();

inline int32_t (*orig_KCMDEGKOPGO_IOFCMMGIHBB)() = nullptr;
inline int32_t hook_KCMDEGKOPGO_IOFCMMGIHBB() { return (int32_t)3; }

inline bool (*orig_Is32BitsProcess)() = nullptr;
inline bool hook_Is32BitsProcess() { return true; }

inline void (*orig_ProcessAH)(void* _this, void* loginRes) = nullptr;
inline void hook_ProcessAH(void* _this, void* loginRes) { return; }

inline bool (*orig_get_IsHPE)() = nullptr;
inline bool hook_get_IsHPE() { return false; }

inline bool (*orig_Is64BitsProcess)() = nullptr;
inline bool hook_Is64BitsProcess() { return false; }

inline void (*orig_ParseCert)(void* instance, void* data) = nullptr;
inline void hook_ParseCert(void* instance, void* data) { return; }

inline void* (*orig_PublicKeyToken)(void* instance) = nullptr;
inline void* hook_PublicKeyToken(void* instance) { return nullptr; }

inline void* (*orig_EncryptValue)(void* instance, void* data) = nullptr;
inline void* hook_EncryptValue(void* instance, void* data) { return data; }

inline void* (*orig_DecryptValue)(void* instance, void* data) = nullptr;
inline void* hook_DecryptValue(void* instance, void* data) { return data; }

inline bool (*orig_VerifySignature)(void* instance, void* hash, void* sig) = nullptr;
inline bool hook_VerifySignature(void* instance, void* hash, void* sig) { return true; }

inline bool (*orig_Verify_v15)(void* rsa, void* hash, void* hashValue, void* signature) = nullptr;
inline bool hook_Verify_v15(void* rsa, void* hash, void* hashValue, void* signature) { return true; }

inline void (*X509_Parse)(void* instance, void* data) = nullptr;
inline void _X509_Parse(void* instance, void* data) { return; }

inline void* (*PKCS12_Decrypt)(void* instance, void* ed) = nullptr;
inline void* _PKCS12_Decrypt(void* instance, void* ed) { return ed; }

inline void (*ASN1_Decode)(void* instance, void* data) = nullptr;
inline void _ASN1_Decode(void* instance, void* data) { return; }

inline bool (*BigInteger_IsPrime)(void* instance) = nullptr;
inline bool _BigInteger_IsPrime(void* instance) { return true; }

inline void* (*RSA_EncryptValue)(void* instance, void* rgb) = nullptr;
inline void* _RSA_EncryptValue(void* instance, void* rgb) { return rgb; }

inline void* (*RSA_DecryptValue)(void* instance, void* rgb) = nullptr;
inline void* _RSA_DecryptValue(void* instance, void* rgb) { return rgb; }

inline bool (*DSA_VerifySignature)(void* instance, void* hash, void* signature) = nullptr;
inline bool _DSA_VerifySignature(void* instance, void* hash, void* signature) { return true; }

inline bool (*PKCS1_Verify_v15)(void* rsa, void* hash, void* hashValue, void* signature) = nullptr;
inline bool _PKCS1_Verify_v15(void* rsa, void* hash, void* hashValue, void* signature) { return true; }

inline bool (*orig_VerifyGooglePlayServices)(void* packageManager) = nullptr;
inline bool hook_VerifyGooglePlayServices(void* packageManager) { return true; }

inline bool (*orig_VerifyGooglePlayPackage)(void* packageManager, void* packageName) = nullptr;
inline bool hook_VerifyGooglePlayPackage(void* packageManager, void* packageName) { return true; }

inline bool Guest = false;
inline bool (*ResetGuest)(void* _this) = nullptr;
inline bool _ResetGuest(void* _this) {
    if (Guest) return true;
    return ResetGuest ? ResetGuest(_this) : true;
}

inline bool camxa = false;
inline float (*GetCameraHeightRateValue)(void *player) = nullptr;
inline float _GetCameraHeightRateValue(void *player) {
    if (player != nullptr && camxa) return 4.0f;
    return GetCameraHeightRateValue ? GetCameraHeightRateValue(player) : 1.0f;
}

inline bool (*Bypass)(void* bypass) = nullptr;
inline bool _Bypass(void* bypass) { return false; }

inline bool (*get_ResetGuest)(bool* instance) = nullptr;
inline bool _get_ResetGuest(bool* instance) {
    if (Convidado) return true;
    return get_ResetGuest ? get_ResetGuest(instance) : true;
}

inline float (*old_GetCurrentDashSpeed)(void *instance) = nullptr;
inline float hook_GetCurrentDashSpeed(void *instance) {
    if (old_GetCurrentDashSpeed) return old_GetCurrentDashSpeed(instance);
    return 1.0f;
}

#endif // PROTECTION_SECURITY_BYPASS_H
