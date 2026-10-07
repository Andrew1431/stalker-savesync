// stalker-savesync: steam_api64.dll proxy that copies the co-op host's saves
// to every connected client over Steam P2P.
//
// All Steam exports are forwarded to steam_api64_real.dll (see proxy.def).
// A worker thread waits for the game to initialise Steam, then:
//   - hooks ISteamNetworkingSockets::ConnectP2P to learn who the host is
//     when this player joins a game,
//   - listens on its own P2P virtual port (kSyncPort), separate from the
//     game's connection, so xrRazom traffic is never touched,
//   - as host: watches savedgames/ and sends every finished save to all
//     connected clients,
//   - as client: connects to the host's sync port and writes received saves
//     as "sync-<host> - <name>".

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "forwards.h"
#include "steam_min.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

#define SAVESYNC_VERSION "1.4.2"

static constexpr int kSyncPort = 21331;
static constexpr uint8_t kProtoVersion = 2;
static constexpr size_t kChunkSize = 256 * 1024;  // Steam's max message is 512 KB
static constexpr uint64_t kMaxFileSize = 128ull * 1024 * 1024;
static constexpr const char* kSyncPrefix = "sync-";
static constexpr int kMaxReconnects = 5;
// .xrr_peers is xrRazom's per-client character data for that save.
static const char* const kExts[] = {".scop", ".scoc", ".dds", ".xrr_peers"};
static constexpr uint8_t kNumExts = 4;

// ---------------------------------------------------------------- logging

static std::mutex g_logMutex;
static fs::path g_logPath;

static void Log(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* f = _wfopen(g_logPath.c_str(), L"a");
    if (!f) return;
    fprintf(f, "[%02d:%02d:%02d] %s\n", t.wHour, t.wMinute, t.wSecond, msg);
    fclose(f);
}

// ---------------------------------------------------------------- steam flat API

static struct {
    HSteamUser (*GetHSteamUser)();
    void (*Shutdown)();
    void* (*SteamNetworkingSockets)();
    void* (*SteamFriends)();
    HSteamListenSocket (*CreateListenSocketP2P)(void*, int, int, const SteamNetworkingConfigValue_t*);
    HSteamNetConnection (*ConnectP2P)(void*, const SteamNetworkingIdentity*, int, int, const SteamNetworkingConfigValue_t*);
    int (*AcceptConnection)(void*, HSteamNetConnection);
    bool (*CloseConnection)(void*, HSteamNetConnection, int, const char*, bool);
    bool (*CloseListenSocket)(void*, HSteamListenSocket);
    int (*SendMessageToConnection)(void*, HSteamNetConnection, const void*, uint32_t, int, int64_t*);
    int (*ReceiveMessagesOnConnection)(void*, HSteamNetConnection, SteamNetworkingMessage_t**, int);
    bool (*GetConnectionInfo)(void*, HSteamNetConnection, SteamNetConnectionInfo_t*);
    void (*ReleaseMessage)(SteamNetworkingMessage_t*);
    const char* (*GetPersonaName)(void*);
    const char* (*GetFriendPersonaName)(void*, uint64_t);
} S;

static HMODULE g_real;

static bool LoadSteamApi() {
    g_real = LoadLibraryW(L"steam_api64_real.dll");
    if (!g_real) return false;
    bool ok = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(g_real, name));
        if (!fn) {
            Log("missing export %s", name);
            ok = false;
        }
    };
    get(S.GetHSteamUser, "SteamAPI_GetHSteamUser");
    get(S.Shutdown, "SteamAPI_Shutdown");
    get(S.SteamNetworkingSockets, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012");
    get(S.SteamFriends, "SteamAPI_SteamFriends_v018");
    get(S.CreateListenSocketP2P, "SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P");
    get(S.ConnectP2P, "SteamAPI_ISteamNetworkingSockets_ConnectP2P");
    get(S.AcceptConnection, "SteamAPI_ISteamNetworkingSockets_AcceptConnection");
    get(S.CloseConnection, "SteamAPI_ISteamNetworkingSockets_CloseConnection");
    get(S.CloseListenSocket, "SteamAPI_ISteamNetworkingSockets_CloseListenSocket");
    get(S.SendMessageToConnection, "SteamAPI_ISteamNetworkingSockets_SendMessageToConnection");
    get(S.ReceiveMessagesOnConnection, "SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection");
    get(S.GetConnectionInfo, "SteamAPI_ISteamNetworkingSockets_GetConnectionInfo");
    get(S.ReleaseMessage, "SteamAPI_SteamNetworkingMessage_t_Release");
    get(S.GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName");
    get(S.GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    return ok;
}

// ---------------------------------------------------------------- shared state

static std::atomic<bool> g_stop{false};
static std::thread g_worker;
static void* g_sockets;
static void* g_friends;
static HSteamListenSocket g_listen = k_HSteamListenSocket_Invalid;

struct StatusEvent {
    HSteamNetConnection conn;
    HSteamListenSocket listen;
    int state;
    uint64_t steamId;
};

static std::mutex g_mutex;  // guards everything below
static std::vector<StatusEvent> g_events;
static bool g_hostChanged = false;
static SteamNetworkingIdentity g_hostId{};

// Runs on the game thread inside SteamAPI_RunCallbacks.
static void __cdecl OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* cb) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back({cb->m_hConn, cb->m_info.m_hListenSocket, cb->m_info.m_eState,
                        cb->m_info.m_identityRemote.m_steamID64});
}

// ---------------------------------------------------------------- ConnectP2P hook

using ConnectP2PFn = HSteamNetConnection(__fastcall*)(void*, const SteamNetworkingIdentity*, int, int,
                                                      const SteamNetworkingConfigValue_t*);
static ConnectP2PFn g_origConnectP2P;

static HSteamNetConnection __fastcall HookConnectP2P(void* self, const SteamNetworkingIdentity* id, int port,
                                                     int nOptions, const SteamNetworkingConfigValue_t* options) {
    if (port != kSyncPort && id && id->m_eType == k_ESteamNetworkingIdentityType_SteamID) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_hostId = *id;
        g_hostChanged = true;
    }
    return g_origConnectP2P(self, id, port, nOptions, options);
}

// The flat export is a thunk like "mov rax,[rcx]; jmp [rax+18h]". Reading the
// offset from it keeps the hook correct if Valve reorders the vtable.
static int VtableOffsetFromThunk(const uint8_t* p) {
    if (p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01) return -1;
    p += 3;
    if (p[0] == 0x4C && p[1] == 0x8B && p[2] == 0x50) return p[3];             // mov r10,[rax+disp8]
    if (p[0] == 0x48 && p[1] == 0xFF && p[2] == 0x60) return p[3];             // jmp [rax+disp8]
    if (p[0] == 0x4C && p[1] == 0x8B && p[2] == 0x90) return *(int32_t*)(p + 3);  // mov r10,[rax+disp32]
    if (p[0] == 0x48 && p[1] == 0xFF && p[2] == 0xA0) return *(int32_t*)(p + 3);  // jmp [rax+disp32]
    return -1;
}

static bool InstallHook() {
    int offset = VtableOffsetFromThunk(reinterpret_cast<const uint8_t*>(S.ConnectP2P));
    if (offset < 0) {
        Log("could not decode ConnectP2P thunk; joining won't trigger sync");
        return false;
    }
    void** slot = reinterpret_cast<void**>(*reinterpret_cast<uint8_t**>(g_sockets) + offset);
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    g_origConnectP2P = reinterpret_cast<ConnectP2PFn>(*slot);
    *slot = reinterpret_cast<void*>(&HookConnectP2P);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log("hooked ConnectP2P (vtable +0x%x)", offset);
    return true;
}

// ---------------------------------------------------------------- helpers

static std::string SanitizeName(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        if (c < 32 || strchr("\\/:*?\"<>|", c)) continue;
        out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(0, 1);
    if (out.size() > 60) out.resize(60);
    return out.empty() ? "unknown" : out;
}

static bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

static bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    _fseeki64(f, 0, SEEK_END);
    int64_t size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    bool ok = size == 0 || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

struct Writer {
    std::vector<uint8_t> b;
    template <class T> void put(T v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof v);
    }
    void bytes(const void* p, size_t n) {
        b.insert(b.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
    }
};

struct Reader {
    const uint8_t* p;
    size_t left;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (left < sizeof v) { ok = false; return v; }
        memcpy(&v, p, sizeof v);
        p += sizeof v;
        left -= sizeof v;
        return v;
    }
    std::string str(size_t n) {
        if (left < n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        left -= n;
        return s;
    }
};

enum : uint8_t { kMsgBegin = 1, kMsgChunk = 2, kMsgEnd = 3 };

static void PutHeader(Writer& w, uint8_t type) {
    w.bytes("SSYN", 4);
    w.put<uint8_t>(kProtoVersion);
    w.put<uint8_t>(type);
}

// ---------------------------------------------------------------- host side

static fs::path g_saveDir;
static std::set<HSteamNetConnection> g_peers;
static std::set<HSteamNetConnection> g_peersNeedingLatest;
static uint32_t g_nextSetId = 1;

struct FileStamp {
    uintmax_t size = 0;
    fs::file_time_type mtime{};
    bool operator==(const FileStamp& o) const { return size == o.size && mtime == o.mtime; }
};
struct SaveStamp {
    FileStamp scop, scoc, peers;
    bool operator==(const SaveStamp& o) const { return scop == o.scop && scoc == o.scoc && peers == o.peers; }
};
static std::map<std::string, SaveStamp> g_known;    // stem -> last stamp we've seen and handled
static std::map<std::string, SaveStamp> g_pending;  // stem -> changed, waiting until it stops changing

static FileStamp Stamp(const fs::path& p) {
    FileStamp s;
    std::error_code ec;
    s.size = fs::file_size(p, ec);
    if (ec) return {};
    s.mtime = fs::last_write_time(p, ec);
    return s;
}

static std::map<std::string, SaveStamp> ScanSaves() {
    std::map<std::string, SaveStamp> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(g_saveDir, ec)) {
        if (e.path().extension() != ".scop") continue;
        std::string stem = e.path().stem().u8string();
        if (StartsWith(stem, kSyncPrefix)) continue;  // never re-share saves we received
        fs::path base = g_saveDir / e.path().stem();
        out[stem] = {Stamp(fs::path(base).concat(".scop")), Stamp(fs::path(base).concat(".scoc")),
                     Stamp(fs::path(base).concat(".xrr_peers"))};
    }
    return out;
}

static bool SendReliable(HSteamNetConnection conn, const Writer& w) {
    for (int tries = 0; tries < 200 && !g_stop; ++tries) {
        int r = S.SendMessageToConnection(g_sockets, conn, w.b.data(), static_cast<uint32_t>(w.b.size()),
                                          k_nSteamNetworkingSend_Reliable, nullptr);
        if (r == k_EResultOK) return true;
        if (r != k_EResultLimitExceeded) {
            Log("send failed on conn %u (result %d)", conn, r);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

static void SendSave(const std::string& stem, const std::vector<HSteamNetConnection>& to) {
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> files;
    for (uint8_t i = 0; i < kNumExts; ++i) {
        fs::path p = fs::path(g_saveDir / fs::u8path(stem)).concat(kExts[i]);
        std::vector<uint8_t> data;
        if (!fs::exists(p)) continue;
        if (!ReadFileBytes(p, data) || data.size() > kMaxFileSize) {
            Log("could not read %s", p.u8string().c_str());
            return;
        }
        files.emplace_back(i, std::move(data));
    }
    if (files.empty() || files[0].first != 0) return;  // .scop is required

    std::string host = S.GetPersonaName(g_friends);
    uint32_t setId = g_nextSetId++;

    Writer begin;
    PutHeader(begin, kMsgBegin);
    begin.put<uint32_t>(setId);
    begin.put<uint16_t>(static_cast<uint16_t>(host.size()));
    begin.put<uint16_t>(static_cast<uint16_t>(stem.size()));
    begin.put<uint8_t>(static_cast<uint8_t>(files.size()));
    for (auto& f : files) {
        begin.put<uint8_t>(f.first);
        begin.put<uint64_t>(f.second.size());
    }
    begin.bytes(host.data(), host.size());
    begin.bytes(stem.data(), stem.size());

    Writer end;
    PutHeader(end, kMsgEnd);
    end.put<uint32_t>(setId);

    size_t total = 0;
    for (auto& f : files) total += f.second.size();

    for (HSteamNetConnection conn : to) {
        bool ok = SendReliable(conn, begin);
        for (uint8_t fi = 0; ok && fi < files.size(); ++fi) {
            const auto& data = files[fi].second;
            for (size_t off = 0; ok && off < data.size(); off += kChunkSize) {
                Writer chunk;
                PutHeader(chunk, kMsgChunk);
                chunk.put<uint32_t>(setId);
                chunk.put<uint8_t>(fi);
                chunk.put<uint64_t>(off);
                chunk.bytes(data.data() + off, std::min(kChunkSize, data.size() - off));
                ok = SendReliable(conn, chunk);
            }
        }
        ok = ok && SendReliable(conn, end);
        Log("%s save '%s' (%zu KB) to conn %u", ok ? "sent" : "FAILED to send", stem.c_str(), total / 1024,
            conn);
    }
}

static std::string LatestSave(const std::map<std::string, SaveStamp>& saves) {
    std::string best;
    fs::file_time_type bestTime{};
    for (auto& [stem, st] : saves) {
        if (st.scop.size && (best.empty() || st.scop.mtime > bestTime)) {
            best = stem;
            bestTime = st.scop.mtime;
        }
    }
    return best;
}

static void HostTick() {
    auto now = ScanSaves();

    // A save is "finished" once it looked the same on two scans in a row.
    std::vector<std::string> ready;
    for (auto& [stem, st] : now) {
        auto known = g_known.find(stem);
        if (known != g_known.end() && known->second == st) {
            g_pending.erase(stem);
            continue;
        }
        auto pend = g_pending.find(stem);
        if (pend != g_pending.end() && pend->second == st) {
            ready.push_back(stem);
            g_known[stem] = st;
            g_pending.erase(pend);
        } else {
            g_pending[stem] = st;
        }
    }

    std::vector<HSteamNetConnection> all(g_peers.begin(), g_peers.end());
    if (!all.empty()) {
        for (auto& stem : ready) SendSave(stem, all);
    }

    if (!g_peersNeedingLatest.empty()) {
        std::vector<HSteamNetConnection> fresh(g_peersNeedingLatest.begin(), g_peersNeedingLatest.end());
        g_peersNeedingLatest.clear();
        std::string latest = LatestSave(g_known);
        if (!latest.empty()) SendSave(latest, fresh);
    }
}

// ---------------------------------------------------------------- client side

static HSteamNetConnection g_hostConn = k_HSteamNetConnection_Invalid;
static SteamNetworkingIdentity g_connectTo{};
static int g_reconnects = 0;
static Clock::time_point g_nextConnect{};

struct Incoming {
    uint32_t setId = 0;
    std::string finalStem;
    struct File {
        uint8_t ext;
        uint64_t size;
        uint64_t received = 0;
        fs::path tmp;
        FILE* f = nullptr;
    };
    std::vector<File> files;
    bool active = false;

    void Abort() {
        for (auto& f : files) {
            if (f.f) fclose(f.f);
            std::error_code ec;
            fs::remove(f.tmp, ec);
        }
        files.clear();
        active = false;
    }
};
static Incoming g_in;

static void HandleMessage(const uint8_t* data, size_t size) {
    Reader r{data, size};
    std::string magic = r.str(4);
    uint8_t ver = r.get<uint8_t>();
    uint8_t type = r.get<uint8_t>();
    if (!r.ok || magic != "SSYN") return;
    if (ver != kProtoVersion) {
        Log("host uses a different savesync version (%d vs %d); update both", ver, kProtoVersion);
        return;
    }
    uint32_t setId = r.get<uint32_t>();

    if (type == kMsgBegin) {
        g_in.Abort();
        uint16_t hostLen = r.get<uint16_t>();
        uint16_t stemLen = r.get<uint16_t>();
        uint8_t count = r.get<uint8_t>();
        if (count == 0 || count > kNumExts) return;
        std::vector<Incoming::File> files(count);
        for (auto& f : files) {
            f.ext = r.get<uint8_t>();
            f.size = r.get<uint64_t>();
            if (f.ext >= kNumExts || f.size > kMaxFileSize) return;
        }
        std::string host = r.str(hostLen);
        std::string stem = r.str(stemLen);
        if (!r.ok) return;
        g_in.setId = setId;
        g_in.finalStem = std::string(kSyncPrefix) + SanitizeName(host) + " - " + SanitizeName(stem);
        g_in.files = std::move(files);
        for (size_t i = 0; i < g_in.files.size(); ++i) {
            auto& f = g_in.files[i];
            f.tmp = g_saveDir / (".savesync_" + std::to_string(i) + ".tmp");
            f.f = _wfopen(f.tmp.c_str(), L"wb");
            if (!f.f) {
                Log("cannot write to %s", g_saveDir.u8string().c_str());
                g_in.Abort();
                return;
            }
        }
        g_in.active = true;
    } else if (type == kMsgChunk) {
        if (!g_in.active || setId != g_in.setId) return;
        uint8_t fi = r.get<uint8_t>();
        uint64_t off = r.get<uint64_t>();
        if (!r.ok || fi >= g_in.files.size()) return;
        auto& f = g_in.files[fi];
        if (off != f.received || f.received + r.left > f.size) {
            Log("bad chunk, dropping save");
            g_in.Abort();
            return;
        }
        fwrite(r.p, 1, r.left, f.f);
        f.received += r.left;
    } else if (type == kMsgEnd) {
        if (!g_in.active || setId != g_in.setId) return;
        for (auto& f : g_in.files) {
            fclose(f.f);
            f.f = nullptr;
            if (f.received != f.size) {
                Log("incomplete save, dropping");
                g_in.Abort();
                return;
            }
        }
        // Rename .scop last so the game never sees a save without its .scoc.
        std::vector<Incoming::File*> order;
        for (auto& f : g_in.files) order.push_back(&f);
        std::stable_sort(order.begin(), order.end(), [](auto* a, auto* b) { return (a->ext == 0) < (b->ext == 0); });
        fs::path base = g_saveDir / fs::u8path(g_in.finalStem);
        for (auto* f : order) {
            fs::path dest = fs::path(base).concat(kExts[f->ext]);
            if (!MoveFileExW(f->tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                Log("could not write %s (error %lu)", dest.u8string().c_str(), GetLastError());
            }
        }
        Log("received save '%s'", g_in.finalStem.c_str());
        g_in.files.clear();
        g_in.active = false;
    }
}

static SteamNetworkingConfigValue_t g_opts[4];

static void ClientTick() {
    if (g_hostConn == k_HSteamNetConnection_Invalid && g_connectTo.m_eType != 0 &&
        g_reconnects < kMaxReconnects && Clock::now() >= g_nextConnect) {
        g_hostConn = S.ConnectP2P(g_sockets, &g_connectTo, kSyncPort, 4, g_opts);
        ++g_reconnects;
        Log("connecting to host %llu (attempt %d)", g_connectTo.m_steamID64, g_reconnects);
    }
    if (g_hostConn == k_HSteamNetConnection_Invalid) return;

    SteamNetworkingMessage_t* msgs[32];
    int n;
    while ((n = S.ReceiveMessagesOnConnection(g_sockets, g_hostConn, msgs, 32)) > 0) {
        for (int i = 0; i < n; ++i) {
            HandleMessage(static_cast<const uint8_t*>(msgs[i]->m_pData), msgs[i]->m_cbSize);
            S.ReleaseMessage(msgs[i]);
        }
    }
}

// ---------------------------------------------------------------- event handling

static void HandleEvents() {
    std::vector<StatusEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        events.swap(g_events);
        if (g_hostChanged) {
            g_hostChanged = false;
            if (g_hostConn) S.CloseConnection(g_sockets, g_hostConn, 0, "host changed", false);
            g_hostConn = k_HSteamNetConnection_Invalid;
            g_in.Abort();
            g_connectTo = g_hostId;
            g_reconnects = 0;
            g_nextConnect = Clock::now() + std::chrono::seconds(3);  // let the game connect first
            Log("game joined host %llu", g_hostId.m_steamID64);
        }
    }

    for (auto& e : events) {
        Log("event conn=%u listen=%u state=%d", e.conn, e.listen, e.state);
        bool closed = e.state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                      e.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
        if (e.conn == g_hostConn && g_hostConn != k_HSteamNetConnection_Invalid) {
            if (e.state == k_ESteamNetworkingConnectionState_Connected) {
                g_reconnects = 0;
                Log("connected to host for save sync");
            } else if (closed) {
                S.CloseConnection(g_sockets, e.conn, 0, nullptr, false);
                g_hostConn = k_HSteamNetConnection_Invalid;
                g_in.Abort();
                g_nextConnect = Clock::now() + std::chrono::seconds(15);
                if (g_reconnects >= kMaxReconnects)
                    Log("host isn't answering (do they have savesync installed?)");
            }
            continue;
        }
        // Our callback is only attached to our own sockets, so anything else is a
        // player connecting to our sync port. Don't trust e.listen: under Proton the
        // callback struct may not carry it.
        if (e.state == k_ESteamNetworkingConnectionState_Connecting) {
            int r = S.AcceptConnection(g_sockets, e.conn);
            Log("accepting sync connection %u (result %d)", e.conn, r);
        } else if (e.state == k_ESteamNetworkingConnectionState_Connected) {
            std::string name = S.GetFriendPersonaName(g_friends, e.steamId);
            Log("player '%s' connected for save sync", name.c_str());
            g_peers.insert(e.conn);
            g_peersNeedingLatest.insert(e.conn);
        } else if (closed) {
            g_peers.erase(e.conn);
            g_peersNeedingLatest.erase(e.conn);
            S.CloseConnection(g_sockets, e.conn, 0, nullptr, false);
        }
    }
}

// ---------------------------------------------------------------- worker

static void Worker() {
    if (!LoadSteamApi()) {
        Log("steam_api64_real.dll not found or incomplete; run install.bat again");
        return;
    }
    while (!g_stop && S.GetHSteamUser() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (g_stop) return;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    g_sockets = S.SteamNetworkingSockets();
    g_friends = S.SteamFriends();
    if (!g_sockets || !g_friends) {
        Log("Steam interfaces unavailable; save sync disabled");
        return;
    }
    InstallHook();

    g_opts[0] = {k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, k_ESteamNetworkingConfig_Ptr, {}};
    g_opts[0].m_val.m_ptr = reinterpret_cast<void*>(&OnStatusChanged);
    g_opts[1] = {k_ESteamNetworkingConfig_SendBufferSize, k_ESteamNetworkingConfig_Int32, {}};
    g_opts[1].m_val.m_int32 = 16 * 1024 * 1024;
    g_opts[2] = {k_ESteamNetworkingConfig_SendRateMin, k_ESteamNetworkingConfig_Int32, {}};
    g_opts[2].m_val.m_int32 = 256 * 1024;
    g_opts[3] = {k_ESteamNetworkingConfig_SendRateMax, k_ESteamNetworkingConfig_Int32, {}};
    g_opts[3].m_val.m_int32 = 2 * 1024 * 1024;

    g_listen = S.CreateListenSocketP2P(g_sockets, kSyncPort, 4, g_opts);
    Log("savesync " SAVESYNC_VERSION " ready as '%s'; saves: %s; listen socket %u", S.GetPersonaName(g_friends),
        g_saveDir.u8string().c_str(), g_listen);

    g_known = ScanSaves();
    auto lastScan = Clock::now();
    while (!g_stop) {
        HandleEvents();
        ClientTick();
        if (Clock::now() - lastScan >= std::chrono::seconds(1)) {
            lastScan = Clock::now();
            HostTick();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    for (auto c : g_peers) S.CloseConnection(g_sockets, c, 0, "shutdown", false);
    if (g_hostConn) S.CloseConnection(g_sockets, g_hostConn, 0, "shutdown", false);
    if (g_listen) S.CloseListenSocket(g_sockets, g_listen);
    g_in.Abort();
}

// ---------------------------------------------------------------- exports / entry

static void StopWorker() {
    g_stop = true;
    if (g_worker.joinable()) g_worker.join();
}

// install.bat looks for this export name to tell the proxy apart from Valve's DLL.
extern "C" __declspec(dllexport) const char* StalkerSaveSync_Version() {
    return SAVESYNC_VERSION;
}

extern "C" __declspec(dllexport) void SteamAPI_Shutdown() {
    StopWorker();
    if (S.Shutdown) S.Shutdown();
}

static void Start(HMODULE self) {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(self, buf, MAX_PATH);
    fs::path binDir = fs::path(buf).parent_path();
    g_logPath = binDir / "savesync.log";
    if (FILE* f = _wfopen(g_logPath.c_str(), L"w")) fclose(f);
    g_saveDir = (binDir.parent_path() / "appdata" / "savedgames").lexically_normal();

    g_worker = std::thread(Worker);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Threads can't start running until DllMain returns, so this is safe.
        Start(inst);
    } else if (reason == DLL_PROCESS_DETACH) {
        // Joining here would deadlock on the loader lock; just ask it to stop.
        g_stop = true;
        if (g_worker.joinable()) g_worker.detach();
    }
    return TRUE;
}
