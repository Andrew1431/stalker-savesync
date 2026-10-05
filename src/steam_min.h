// Minimal Steamworks declarations, so the proxy builds without the SDK.
// Layouts match steamnetworkingtypes.h (pack 8 on Windows). Only the fields
// we touch are named; the sizes are asserted below.
#pragma once
#include <cstdint>

typedef int32_t HSteamUser;
typedef uint32_t HSteamNetConnection;
typedef uint32_t HSteamListenSocket;
constexpr HSteamNetConnection k_HSteamNetConnection_Invalid = 0;
constexpr HSteamListenSocket k_HSteamListenSocket_Invalid = 0;

enum {
    k_EResultOK = 1,
    k_EResultLimitExceeded = 25,
};

enum {
    k_ESteamNetworkingIdentityType_SteamID = 16,
};

enum {
    k_ESteamNetworkingConnectionState_None = 0,
    k_ESteamNetworkingConnectionState_Connecting = 1,
    k_ESteamNetworkingConnectionState_FindingRoute = 2,
    k_ESteamNetworkingConnectionState_Connected = 3,
    k_ESteamNetworkingConnectionState_ClosedByPeer = 4,
    k_ESteamNetworkingConnectionState_ProblemDetectedLocally = 5,
};

enum {
    k_ESteamNetworkingConfig_SendBufferSize = 9,
    k_ESteamNetworkingConfig_SendRateMin = 10,
    k_ESteamNetworkingConfig_SendRateMax = 11,
    k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged = 201,
};

enum {
    k_ESteamNetworkingConfig_Int32 = 1,
    k_ESteamNetworkingConfig_Ptr = 5,
};

constexpr int k_nSteamNetworkingSend_Reliable = 8;

#pragma pack(push, 8)

struct SteamNetworkingIdentity {
    int32_t m_eType;
    int32_t m_cbSize;
    union {
        uint64_t m_steamID64;
        char m_raw[128];
    };
};
static_assert(sizeof(SteamNetworkingIdentity) == 136, "identity layout");

#pragma pack(push, 1)
struct SteamNetworkingIPAddr {
    uint8_t m_ipv6[16];
    uint16_t m_port;
};
#pragma pack(pop)

struct SteamNetConnectionInfo_t {
    SteamNetworkingIdentity m_identityRemote;
    int64_t m_nUserData;
    HSteamListenSocket m_hListenSocket;
    SteamNetworkingIPAddr m_addrRemote;
    uint16_t m__pad1;
    uint32_t m_idPOPRemote;
    uint32_t m_idPOPRelay;
    int32_t m_eState;
    int32_t m_eEndReason;
    char m_szEndDebug[128];
    char m_szConnectionDescription[128];
    int32_t m_nFlags;
    uint32_t reserved[63];
};
static_assert(sizeof(SteamNetConnectionInfo_t) == 696, "connection info layout");

struct SteamNetConnectionStatusChangedCallback_t {
    HSteamNetConnection m_hConn;
    SteamNetConnectionInfo_t m_info;
    int32_t m_eOldState;
};
static_assert(sizeof(SteamNetConnectionStatusChangedCallback_t) == 712, "callback layout");

struct SteamNetworkingConfigValue_t {
    int32_t m_eValue;
    int32_t m_eDataType;
    union {
        int32_t m_int32;
        int64_t m_int64;
        void* m_ptr;
    } m_val;
};

// Only the leading fields; always released through the flat API.
struct SteamNetworkingMessage_t {
    void* m_pData;
    int32_t m_cbSize;
    HSteamNetConnection m_conn;
};

#pragma pack(pop)
