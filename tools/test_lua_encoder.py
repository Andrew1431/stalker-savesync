"""Check lua/.../savesync.script's encoder against real .xrr_peers records.

Each peer is decoded with tools/xrr_peers.py, handed to the Lua encode_peer()
(run in LuaJIT via lupa), and the bytes must match the original record exactly.

Usage: python tools/test_lua_encoder.py <files.xrr_peers...>
"""
import pathlib
import sys

import lupa.luajit21 as lupa

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import xrr_peers  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
lua = lupa.LuaRuntime(unpack_returned_tuples=True, encoding=None)
lua.execute(open(ROOT / "lua/gamedata/scripts/savesync.script", "rb").read())
encode_peer = lua.globals()[b"encode_peer"]


def to_lua(v):
    if isinstance(v, str):
        return v.encode("latin-1")
    if isinstance(v, dict):
        return lua.table_from({k.encode(): to_lua(x) for k, x in v.items()})
    if isinstance(v, (list, tuple)):
        return lua.table_from([to_lua(x) for x in v])
    return v


bad = 0
for f in sys.argv[1:]:
    data = open(f, "rb").read()
    try:
        doc = xrr_peers.load(data)
    except ValueError as e:
        print(f"SKIP {e}: {f}")
        continue
    for p in doc["peers"]:
        p["steamid"] = 0  # > 2^53, can't be a Lua number; savesync.dll fills it in
        w = xrr_peers.Writer()
        xrr_peers.write_peer(w, p)
        expected = bytes(w.b)
        got = encode_peer(to_lua(p))
        ok = got == expected
        bad += not ok
        where = "" if ok else f" (first diff at {next(i for i in range(min(len(got), len(expected))) if got[i] != expected[i]) if got[:len(expected)] != expected[:len(got)] else min(len(got), len(expected))})"
        print(f"{'OK  ' if ok else 'DIFF'} {p['name']:12} {len(expected):6} bytes{where}  {f}")
sys.exit(1 if bad else 0)
