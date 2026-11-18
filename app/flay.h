#pragma once

#include <imgui.h>
#include <Il2Cpp.h>
#include <Tools.h>
#include <Vector3.hpp>
#include "obfuscate.h"
#include "Unity/Unity.h"
#include "Class.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <map>
#include <string>
#include <ostream>
#include <sstream>

namespace FlyHit {
//fixs Siding Hit Fly Enmiysss

//trmuxx
inline bool FlyHitEnabled = false;
inline bool QueuedAction = false;
inline float Duration = 1.0f;
inline float SpeedXZ = 0.0f;
inline float HeightY = 0.0f;

inline bool ReadBoolValue(const std::map<std::string, std::string>& cfg,
                          const char* key, bool& out) {
    const auto it = cfg.find(key);
    if (it == cfg.end()) return false;
    const std::string& value = it->second;
    out = !(value.empty() || value == "0" || value == "false" || value == "FALSE");
    return true;
}

inline bool ReadFloatValue(const std::map<std::string, std::string>& cfg,
                           const char* key, float& out) {
    const auto it = cfg.find(key);
    if (it == cfg.end()) return false;
    char* end = nullptr;
    const float value = std::strtof(it->second.c_str(), &end);
    if (end == it->second.c_str() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

inline void ClampSettings() {
    if (!std::isfinite(Duration)) Duration = 1.0f;
    if (!std::isfinite(SpeedXZ)) SpeedXZ = 0.0f;
    if (!std::isfinite(HeightY)) HeightY = 0.0f;
    Duration = (Duration < 0.0f) ? 0.0f : ((Duration > 50.0f) ? 50.0f : Duration);
    SpeedXZ = (SpeedXZ < 0.0f) ? 0.0f : ((SpeedXZ > 50.0f) ? 50.0f : SpeedXZ);
    HeightY = (HeightY < 0.0f) ? 0.0f : ((HeightY > 80.0f) ? 80.0f : HeightY);
}

// Draw these controls inside the destination project's  tab.
inline void DrawMenu() {
    ImGui::Checkbox("Fly hit", &FlyHitEnabled);
    ImGui::SliderFloat("HitFly Speed", &SpeedXZ, 0.0f, 50.0f, "%.0f");
    ImGui::SliderFloat("HitFly Height", &HeightY, 0.0f, 80.0f, "%.0f");
}

inline bool Execute() {
    void* currentMatch = Curent_Match();
    if (!currentMatch) return false;

    void* localPlayer = GetLocalPlayer(currentMatch);
    void* mainCam = Camera_main();
    if (!localPlayer || !mainCam) return false;

    static size_t off_IHAAM = 0;
    static size_t off_CJK = 0;
    static size_t off_HIF = 0;
    static size_t off_DGDI = 0;
    static size_t off_MHC = 0;
    static size_t off_PlayerID = 0;

    if (off_IHAAM == 0) {
        off_IHAAM = Il2CppGetFieldOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
            OBFUSCATE("NNDOIBJBAJI"), OBFUSCATE("BEADLMGGGGL"));
        off_CJK = Il2CppGetFieldOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
            OBFUSCATE("NNDOIBJBAJI"), OBFUSCATE("DCEMJOGHMEG"));
        off_HIF = Il2CppGetFieldOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
            OBFUSCATE("NNDOIBJBAJI"), OBFUSCATE("KJGOMDJCKEB"));
        off_DGDI = Il2CppGetFieldOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
            OBFUSCATE("NNDOIBJBAJI"), OBFUSCATE("DPJAIECLHGL"));
        off_MHC = Il2CppGetFieldOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
            OBFUSCATE("NNDOIBJBAJI"), OBFUSCATE("MGJNBIJNLIJ"));
        off_PlayerID = Il2CppGetFieldOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"),
            OBFUSCATE("Player"), OBFUSCATE("MHGFKKALMDK"));
    }

    if (!off_IHAAM || !off_CJK || !off_HIF || !off_DGDI ||
        !off_MHC || !off_PlayerID) return false;

    void* msg = Il2CppCreateClassInstance(
        OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
        OBFUSCATE("NNDOIBJBAJI"));
    if (!msg) return false;

    const uintptr_t msgAddr = reinterpret_cast<uintptr_t>(msg);
    const uint32_t playerID = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(localPlayer) + off_PlayerID);

    *reinterpret_cast<uint32_t*>(msgAddr + off_IHAAM) = playerID;
    *reinterpret_cast<float*>(msgAddr + off_CJK) = Duration;
    *reinterpret_cast<float*>(msgAddr + off_HIF) = SpeedXZ;
    *reinterpret_cast<float*>(msgAddr + off_DGDI) = HeightY;

    static uintptr_t off_Pack = 0;
    if (!off_Pack) {
        off_Pack = reinterpret_cast<uintptr_t>(Il2CppGetMethodOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("message"),
            OBFUSCATE("LMCBHLCPAJD"), OBFUSCATE("MGOGJCMCIPG"), 1));
    }
    if (!off_Pack) return false;

    void* camTransform = Component_GetTransform(mainCam);
    if (!camTransform) return false;
    const Vector3 forward = GetForward(camTransform);
    void* packedObj = reinterpret_cast<void* (*)(Vector3)>(off_Pack)(forward);
    if (!packedObj) return false;
    *reinterpret_cast<void**>(msgAddr + off_MHC) = packedObj;

    static uintptr_t off_Send = 0;
    if (!off_Send) {
        off_Send = reinterpret_cast<uintptr_t>(Il2CppGetMethodOffset(
            OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW"),
            OBFUSCATE("GameFacade"), OBFUSCATE("Send"), 4));
    }
    if (!off_Send) return false;

    const auto sendMessage = reinterpret_cast<bool (*)(uint32_t, void*, uint8_t, bool)>(off_Send);
    return sendMessage(657, msg, 2, false);
}

// Call 
inline void RunQueuedAction() {
    if (!QueuedAction) return;
    QueuedAction = false; // consume before execution to prevent repeated sends
    ClampSettings();
    (void)Execute();
}

// Optional: call
inline void LoadConfig(const std::map<std::string, std::string>& cfg) {
    ReadBoolValue(cfg, "HitFlyShow", FlyHitEnabled);
    ReadFloatValue(cfg, "GUI_FlyTime", Duration);
    ReadFloatValue(cfg, "GUI_FlySpeedXZ", SpeedXZ);
    ReadFloatValue(cfg, "GUI_FlySpeedY", HeightY);
    QueuedAction = false;
    ClampSettings();
}