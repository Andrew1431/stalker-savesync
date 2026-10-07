"""Read/write xrRazom .xrr_peers files (format v15).

    python tools/xrr_peers.py dump <file.xrr_peers>     -> JSON to stdout
    python tools/xrr_peers.py check <files...>          -> parse + re-encode, must be byte-identical
    python tools/xrr_peers.py swap <save.xrr_peers> <savesync_host.bin> <new host name>
        [--old-host-name NAME] [--old-host-steamid ID]
        -> what savesync.dll will do on receive: the old host's exported character
           becomes a client record, and the new host's own record moves to
           savesync_apply_<id>.bin (next to the export) for the Lua side to apply.

Strings are raw bytes (X-Ray uses cp1251), held as latin-1 so they round-trip.
Lua-style tables are kept as lists of [key, value] pairs so order and types
survive a round trip. Floats are written back from the exact parsed values.
"""
import json
import struct
import sys

SUPPORTED_VERSIONS = {15}

T_BOOL, T_NUM, T_STR, T_TABLE = 1, 2, 3, 4


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def take(self, fmt):
        v = struct.unpack_from(fmt, self.d, self.p)
        self.p += struct.calcsize(fmt)
        return v[0] if len(v) == 1 else list(v)

    def cstr(self):
        e = self.d.index(b"\0", self.p)
        s = self.d[self.p:e].decode("latin-1")
        self.p = e + 1
        return s

    def lstr(self):
        n = self.take("<H")
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s

    def value(self):
        t = self.take("<B")
        if t == T_BOOL:
            return bool(self.take("<B"))
        if t == T_NUM:
            return self.take("<d")
        if t == T_STR:
            return self.lstr()
        if t == T_TABLE:
            return {"table": [[self.value(), self.value()] for _ in range(self.take("<H"))]}
        raise ValueError(f"unknown value type {t} at {self.p - 1:#x}")


class Writer:
    def __init__(self):
        self.b = bytearray()

    def put(self, fmt, *v):
        self.b += struct.pack(fmt, *v)

    def cstr(self, s):
        self.b += s.encode("latin-1") + b"\0"

    def lstr(self, s):
        e = s.encode("latin-1")
        self.put("<H", len(e))
        self.b += e

    def value(self, v):
        if isinstance(v, bool):
            self.put("<BB", T_BOOL, int(v))
        elif isinstance(v, float):
            self.put("<Bd", T_NUM, v)
        elif isinstance(v, str):
            self.put("<B", T_STR)
            self.lstr(v)
        elif isinstance(v, dict):
            self.put("<BH", T_TABLE, len(v["table"]))
            for k, x in v["table"]:
                self.value(k)
                self.value(x)
        else:
            raise TypeError(type(v))


# Sized entry: cstr key, u16 byte length, typed value (used for limb health and m_data).
def read_sized(r):
    key = r.cstr()
    n = r.take("<H")
    end = r.p + n
    val = r.value()
    assert r.p == end, f"sized value {key!r} length mismatch"
    return [key, val]


def write_sized(w, entry):
    key, val = entry
    w.cstr(key)
    inner = Writer()
    inner.value(val)
    w.put("<H", len(inner.b))
    w.b += inner.b


# ---------------------------------------------------------------- items

def read_item(r):
    it = {
        "id": r.take("<I"),
        "section": r.cstr(),
        "count": r.take("<H"),
        "condition": r.take("<f"),
        "ammo_in_mag": r.take("<I"),
        "slot": r.take("<B"),    # slot index when place == 1
        "place": r.take("<B"),   # X-Ray eItemPlace: 1 slot, 2 belt, 3 backpack
        "uses": r.take("<B"),
    }
    it["upgrades"] = [r.cstr() for _ in range(r.take("<B"))]
    if r.take("<B"):
        n = r.take("<H")
        end = r.p + n
        it["data"] = [[r.lstr(), r.value()] for _ in range(r.take("<H"))]
        assert r.p == end, f"item {it['section']} data length mismatch"
    it["unk_tail"] = r.take("<H")
    return it


def write_item(w, it):
    w.put("<I", it["id"])
    w.cstr(it["section"])
    w.put("<HfIBBBB", it["count"], it["condition"], it["ammo_in_mag"], it["slot"], it["place"],
          it["uses"], len(it["upgrades"]))
    for u in it["upgrades"]:
        w.cstr(u)
    if "data" in it:
        inner = Writer()
        inner.put("<H", len(it["data"]))
        for k, v in it["data"]:
            inner.lstr(k)
            inner.value(v)
        w.put("<BH", 1, len(inner.b))
        w.b += inner.b
    else:
        w.put("<B", 0)
    w.put("<H", it["unk_tail"])


# ---------------------------------------------------------------- peer

def read_peer(r):
    p = {
        "name": r.cstr(),
        "steamid": r.take("<Q"),
        "faction": r.cstr(),
        "visual": r.cstr(),
        "outfit": r.cstr(),
        "helmet": r.cstr(),
        "pda": r.cstr(),
        "torch": r.cstr(),
        "active_weapon": r.cstr(),
        "active_slot": r.take("<I"),
        "level": r.cstr(),
        "position": r.take("<3f"),
        "unk_angles": r.take("<2f"),  # probably view yaw/pitch or body/head heading
        "health": r.take("<f"),
        "unk_u32": r.take("<I"),
        "money": r.take("<I"),
        "quick_slots": [r.cstr() for _ in range(4)],
        "unk_u8": r.take("<B"),
    }
    p["items"] = [read_item(r) for _ in range(r.take("<H"))]
    p["reputation"] = r.take("<i")
    p["rank"] = r.take("<i")
    p["faction_goodwill"] = [r.take("<Bi") for _ in range(r.take("<H"))]  # (community index, value)
    p["npc_relations"] = [r.take("<Hi") for _ in range(r.take("<H"))]     # (NPC object id, value)
    p["stats"] = r.cstr()
    p["needs"] = r.cstr()
    p["unk_u8_flags"] = r.take("<B")
    p["flags"] = [[r.cstr(), r.cstr()] for _ in range(r.take("<H"))]
    p["limbs"] = [read_sized(r) for _ in range(r.take("<H"))]
    p["m_data"] = [read_sized(r) for _ in range(r.take("<H"))]
    p["game_times"] = r.take("<2Q")  # X-Ray game time (ms); saved / last update?
    return p


def write_peer(w, p):
    w.cstr(p["name"])
    w.put("<Q", p["steamid"])
    for k in ("faction", "visual", "outfit", "helmet", "pda", "torch", "active_weapon"):
        w.cstr(p[k])
    w.put("<I", p["active_slot"])
    w.cstr(p["level"])
    w.put("<3f", *p["position"])
    w.put("<2f", *p["unk_angles"])
    w.put("<fII", p["health"], p["unk_u32"], p["money"])
    for s in p["quick_slots"]:
        w.cstr(s)
    w.put("<BH", p["unk_u8"], len(p["items"]))
    for it in p["items"]:
        write_item(w, it)
    w.put("<ii", p["reputation"], p["rank"])
    for key, fmt in (("faction_goodwill", "<Bi"), ("npc_relations", "<Hi")):
        w.put("<H", len(p[key]))
        for a, b in p[key]:
            w.put(fmt, a, b)
    w.cstr(p["stats"])
    w.cstr(p["needs"])
    w.put("<BH", p["unk_u8_flags"], len(p["flags"]))
    for k, v in p["flags"]:
        w.cstr(k)
        w.cstr(v)
    w.put("<H", len(p["limbs"]))
    for e in p["limbs"]:
        write_sized(w, e)
    w.put("<H", len(p["m_data"]))
    for e in p["m_data"]:
        write_sized(w, e)
    w.put("<2Q", *p["game_times"])


# ---------------------------------------------------------------- file

def load(data):
    if data[:4] != b"XPRR":
        raise ValueError("not an .xrr_peers file")
    r = Reader(data, 4)
    version, count = r.take("<2I")
    if version not in SUPPORTED_VERSIONS:
        raise ValueError(f"unsupported .xrr_peers version {version}")
    peers = [read_peer(r) for _ in range(count)]
    if r.p != len(data):
        raise ValueError(f"{len(data) - r.p} trailing bytes not understood")
    return {"version": version, "peers": peers}


def save(doc):
    w = Writer()
    w.b += b"XPRR"
    w.put("<2I", doc["version"], len(doc["peers"]))
    for p in doc["peers"]:
        write_peer(w, p)
    return bytes(w.b)


def read_export(data, magic):
    if data[:4] != magic:
        raise ValueError(f"not a {magic.decode()} file")
    r = Reader(data, 8)
    save_id = r.lstr()
    peer = read_peer(r)
    return save_id, peer


def write_wrapped(magic, save_id, peer):
    w = Writer()
    w.b += magic
    w.put("<I", 1)
    w.lstr(save_id)
    write_peer(w, peer)
    return bytes(w.b)


def swap(peers_path, export_path, new_host, old_name=None, old_steamid=None):
    import os
    import shutil
    doc = load(open(peers_path, "rb").read())
    save_id, old_host = read_export(open(export_path, "rb").read(), b"SSHX")
    if old_name:
        old_host["name"] = old_name
    if old_steamid:
        old_host["steamid"] = int(old_steamid)
    times = [p["game_times"] for p in doc["peers"]]
    if times:
        old_host["game_times"] = max(times)

    mine = [p for p in doc["peers"] if p["name"] == new_host]
    if not mine:
        raise SystemExit(f"no record for {new_host!r} in {peers_path}")
    doc["peers"] = [p for p in doc["peers"] if p["name"] not in (new_host, old_host["name"])]
    doc["peers"].append(old_host)

    shutil.copyfile(peers_path, peers_path + ".orig")
    open(peers_path, "wb").write(save(doc))
    apply_path = os.path.join(os.path.dirname(os.path.abspath(export_path)), f"savesync_apply_{save_id}.bin")
    open(apply_path, "wb").write(write_wrapped(b"SSAP", save_id, mine[0]))
    print(f"{peers_path}: removed {new_host}, added {old_host['name']} as a client "
          f"({len(old_host['items'])} items); peers now: {', '.join(p['name'] for p in doc['peers'])}")
    print(f"wrote {apply_path} ({len(mine[0]['items'])} items, money {mine[0]['money']}, rank {mine[0]['rank']})")


def main():
    cmd, files = sys.argv[1], sys.argv[2:]
    if cmd == "swap":
        opts = dict(zip(files[3::2], files[4::2]))
        swap(files[0], files[1], files[2], opts.get("--old-host-name"), opts.get("--old-host-steamid"))
        return
    if cmd == "dump":
        print(json.dumps(load(open(files[0], "rb").read()), indent=1))
    elif cmd == "check":
        bad = 0
        for f in files:
            data = open(f, "rb").read()
            try:
                doc = load(data)
                same = save(doc) == data
                print(f"{'OK  ' if same else 'DIFF'} v{doc['version']} "
                      f"{', '.join(p['name'] for p in doc['peers'])}: {f}")
                bad += not same
            except Exception as e:
                print(f"FAIL {e}: {f}")
                bad += 1
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
