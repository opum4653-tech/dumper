#ifndef ANTI_REPORT_H
#define ANTI_REPORT_H

#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cstring>
#include "../Unity/MonoString.h"

inline int32_t (*get_SystemPlatform)() = nullptr;
inline int32_t _get_SystemPlatform() { return (int32_t)2; }

inline int32_t (*get_ChannelType)() = nullptr;
inline int32_t _get_ChannelType() { return (int32_t)0; }

inline int32_t (*get_Architecture)() = nullptr;
inline int32_t _get_Architecture() { return (int32_t)3; }

inline bool enableAntiReport = true;
inline std::mutex logMutex;
inline std::deque<std::string> cmdLogs;

inline const std::vector<uint32_t> BLOCKED_MSG_IDS = {431, 453, 468, 939, 1026, 1079, 1364, 642, 455, 3031, 3025, 3023, 3009, 3008, 3032, 3047, 3500, 1602, 1088, 1087, 1086, 1041, 1080};
inline std::set<uint32_t> blockedMsgIDs = {431, 453, 1026, 939, 1079, 1364, 642, 455, 1080};
inline std::set<uint8_t> blockedCmds = {29, 34, 40, 253};

inline bool (*orig_MatchGameClient_Send)(void* self, uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) = nullptr;
inline bool (*orig_NetworkDetectionClient_Send)(void* self, uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) = nullptr;
inline bool (*orig_ServiceClient2_Send)(void* self, uint8_t cmd, void* data, int32_t byteCount, uint8_t region) = nullptr;

typedef void* (*OnSplittedDatagram_t)(void *instance, void *splittedMsg, void **bigMsg);
inline OnSplittedDatagram_t orig_MatchGameClient_OnSplittedDatagram = nullptr;
inline OnSplittedDatagram_t orig_MobileMatchGameClient_OnSplittedDatagram = nullptr;

enum Proto : int {
    INIT = 1,
    HEARTBEAT = 2,
    MATCHMAKING = 3,
    ROOM = 14,
    RECONNECTION = 17,
    HEARTBEATRANDOMMIN = 151,
    HEARTBEATRANDOMMAX = 255
};

typedef bool (*SendMessageToLobby_t)(void* thiz, int cmdType, uint32_t subCmdType, void* message, uint8_t regionID, bool ignoreDisconnectWnd);
inline SendMessageToLobby_t orig_SendMessageToLobby = nullptr;
inline void (*old_UDPSession_Send)(void* instance, uint32_t cmd, void* data, int32_t byteCount, uint8_t sendOption) = nullptr;
inline int (*orig_GGP_Init)(bool is_low_mem, int client_type, int64_t update_flag) = nullptr;

inline void AddLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    cmdLogs.push_back(message);
    if (cmdLogs.size() > 200) cmdLogs.pop_front();
}

inline bool hk_MatchGameClient_Send(void* instance, uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) {
    if (enableAntiReport && std::find(BLOCKED_MSG_IDS.begin(), BLOCKED_MSG_IDS.end(), messageID) != BLOCKED_MSG_IDS.end()) {
        return false;
    }
    if (orig_MatchGameClient_Send) {
        return orig_MatchGameClient_Send(instance, messageID, msg, sendOption, cacheMsgAnyWay);
    }
    return false;
}

inline bool hook_NetworkDetectionClient_Send(void* self, uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) {
    if (enableAntiReport && std::find(BLOCKED_MSG_IDS.begin(), BLOCKED_MSG_IDS.end(), messageID) != BLOCKED_MSG_IDS.end()) {
        return false;
    }
    if (orig_NetworkDetectionClient_Send) {
        return orig_NetworkDetectionClient_Send(self, messageID, msg, sendOption, cacheMsgAnyWay);
    }
    return false;
}

inline bool hook_ServiceClient2_Send(void* self, uint8_t cmd, void* data, int32_t byteCount, uint8_t region) {
    if (enableAntiReport && blockedCmds.find(cmd) != blockedCmds.end()) {
        return true;
    }
    if (orig_ServiceClient2_Send) {
        return orig_ServiceClient2_Send(self, cmd, data, byteCount, region);
    }
    return false;
}

inline void* hook_OnSplittedDatagram(void *instance, void *splittedMsg, void **bigMsg) {
    if (!splittedMsg) return nullptr;
    uint32_t cmd = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(splittedMsg) + 0x18);
    if (std::find(BLOCKED_MSG_IDS.begin(), BLOCKED_MSG_IDS.end(), cmd) != BLOCKED_MSG_IDS.end()) {
        if (bigMsg) *bigMsg = nullptr;
        return nullptr;
    }
    if (orig_MatchGameClient_OnSplittedDatagram) {
        return orig_MatchGameClient_OnSplittedDatagram(instance, splittedMsg, bigMsg);
    } else if (orig_MobileMatchGameClient_OnSplittedDatagram) {
        return orig_MobileMatchGameClient_OnSplittedDatagram(instance, splittedMsg, bigMsg);
    }
    return nullptr;
}

inline bool Hook_SendMessageToLobby(void* thiz, int cmdType, uint32_t subCmdType, void* message, uint8_t regionID, bool ignoreDisconnectWnd) {
    if (enableAntiReport) {
        if (cmdType == 29 || cmdType == 34 || cmdType == 40 || cmdType == 253) return true;
        if (cmdType == INIT || cmdType == HEARTBEAT || cmdType == RECONNECTION || (cmdType >= HEARTBEATRANDOMMIN && cmdType <= HEARTBEATRANDOMMAX)) {
            return orig_SendMessageToLobby ? orig_SendMessageToLobby(thiz, cmdType, subCmdType, message, regionID, ignoreDisconnectWnd) : true;
        }
        if (cmdType == ROOM || cmdType == MATCHMAKING) {
            return orig_SendMessageToLobby ? orig_SendMessageToLobby(thiz, cmdType, subCmdType, message, regionID, ignoreDisconnectWnd) : true;
        }
        return true;
    }
    return orig_SendMessageToLobby ? orig_SendMessageToLobby(thiz, cmdType, subCmdType, message, regionID, ignoreDisconnectWnd) : true;
}

inline void UDPSession_Send_Hook(void* instance, uint32_t cmd, void* data, int32_t byteCount, uint8_t sendOption) {
    if (cmd == 1079 || cmd == 1364 || cmd == 431 || cmd == 453 || cmd == 1026) return;
    if (old_UDPSession_Send) old_UDPSession_Send(instance, cmd, data, byteCount, sendOption);
}

inline int my_GGP_Init(bool is_low_mem, int client_type, int64_t update_flag) {
    if (update_flag == 2147483645 || client_type == 2) return 0;
    return orig_GGP_Init ? orig_GGP_Init(is_low_mem, client_type, update_flag) : 0;
}

enum Type : int32_t {
    Type_ANTIHACK_MATCH_BANNED1 = 53,
    Type_ANTIHACK_MATCH_BANNED2 = 54,
    Type_ANTIHACK_MATCH_BANNED3 = 55,
    Type_CREATE_PUNISH_MATCH_BEHAVIOR = 92,
    Type_CREATE_PUNISH_CHAT_REPORT = 93,
    Type_ESPORTS_ANTIHACK = 113
};

inline bool (*orig_GameFacade_Send)(uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) = nullptr;
inline void (*orig_MatchGameClient_OnSplittedDatagramOld)(void* _this, void* msg) = nullptr;

inline bool my_GameFacade_Send(uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) {
    if (messageID == Type_ANTIHACK_MATCH_BANNED1 || messageID == Type_ANTIHACK_MATCH_BANNED2 || messageID == Type_ANTIHACK_MATCH_BANNED3 || messageID == Type_CREATE_PUNISH_MATCH_BEHAVIOR || messageID == Type_CREATE_PUNISH_CHAT_REPORT || messageID == Type_ESPORTS_ANTIHACK) {
        return false;
    }
    return orig_GameFacade_Send ? orig_GameFacade_Send(messageID, msg, sendOption, cacheMsgAnyWay) : false;
}

inline bool my_MatchGameClient_Send(void* _this, uint32_t messageID, void* msg, uint8_t sendOption, bool cacheMsgAnyWay) {
    if (messageID == Type_ANTIHACK_MATCH_BANNED1 || messageID == Type_ANTIHACK_MATCH_BANNED2 || messageID == Type_ANTIHACK_MATCH_BANNED3 || messageID == Type_CREATE_PUNISH_MATCH_BEHAVIOR || messageID == Type_CREATE_PUNISH_CHAT_REPORT || messageID == Type_ESPORTS_ANTIHACK) {
        return false;
    }
    return orig_MatchGameClient_Send ? orig_MatchGameClient_Send(_this, messageID, msg, sendOption, cacheMsgAnyWay) : false;
}

inline void* my_MatchGameClient_OnSplittedDatagram(void* _this, void* msg, void** bigMsg) {
    if (!msg) return nullptr;
    uint32_t cmd = *(uint32_t*)((uintptr_t)msg + 0x18);
    if (cmd == Type_ANTIHACK_MATCH_BANNED1 || cmd == Type_ANTIHACK_MATCH_BANNED2 || cmd == Type_ANTIHACK_MATCH_BANNED3 || cmd == Type_CREATE_PUNISH_MATCH_BEHAVIOR || cmd == Type_CREATE_PUNISH_CHAT_REPORT || cmd == Type_ESPORTS_ANTIHACK) {
        if (bigMsg) *bigMsg = nullptr;
        return nullptr;
    }
    return orig_MatchGameClient_OnSplittedDatagram ? orig_MatchGameClient_OnSplittedDatagram(_this, msg, bigMsg) : nullptr;
}

inline void my_MatchGameClient_OnSplittedDatagramOld(void* _this, void* msg) {
    if (!msg) return;
    uint32_t cmd = *(uint32_t*)((uintptr_t)msg + 0x18);
    if (cmd == Type_ANTIHACK_MATCH_BANNED1 || cmd == Type_ANTIHACK_MATCH_BANNED2 || cmd == Type_ANTIHACK_MATCH_BANNED3 || cmd == Type_CREATE_PUNISH_MATCH_BEHAVIOR || cmd == Type_CREATE_PUNISH_CHAT_REPORT || cmd == Type_ESPORTS_ANTIHACK) {
        return;
    }
    if (orig_MatchGameClient_OnSplittedDatagramOld) {
        orig_MatchGameClient_OnSplittedDatagramOld(_this, msg);
    }
}

inline void (*orig_ProcessBugReport)(void *instance, void *loginRes) = nullptr;
inline void hook_ProcessBugReport(void *instance, void *loginRes) { return; }

inline bool (*orig_GetffantihackEnabled)(void *instance) = nullptr;
inline bool hook_GetffantihackEnabled(void *instance) { return false; }

inline void (*orig_ProcessffantihackGGP)(void *instance, void *loginRes) = nullptr;
inline void hook_ProcessffantihackGGP(void *instance, void *loginRes) { return; }

#endif // ANTI_REPORT_H
