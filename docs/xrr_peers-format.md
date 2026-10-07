# .xrr_peers format (v15)

xrRazom writes `<save>.xrr_peers` next to each `.scop`. It holds every
*client's* character; the host's character lives in the `.scop` as usual.

`tools/xrr_peers.py` reads and writes this format. All 5 local v15 samples
re-encode **byte-identical** (`python tools/xrr_peers.py check <files>`).
v12 (older xrRazom) differs and is not supported.

Little-endian. `cstr` = NUL-terminated, `lstr` = u16 length + bytes.

## File

| type    | field |
|---------|-------|
| char[4] | `XPRR` |
| u32     | version (15) |
| u32     | peer count |
| peer[]  | |

## Peer

| type | field | notes |
|------|-------|-------|
| cstr | name | xrRazom identity = player name from its settings |
| u64  | SteamID64 | |
| cstr | faction | `freedom`, `stalker`, ... |
| cstr | visual | `actors\stalker_freedom\stalker_freedom_0.ogf` |
| cstr ×6 | outfit, helmet, PDA, torch, active weapon | sections, empty if none |
| u32  | active slot | |
| cstr | level | `k02_trucks_cemetery` |
| f32 ×3 | position | |
| f32 ×2 | ? angles | |
| f32  | health | 0..1 |
| u32  | ? | always 0 |
| u32  | money | |
| cstr ×4 | quick slots | |
| u8   | ? | always 1 |
| u16 + item[] | inventory | |
| i32  | reputation | |
| i32  | rank | |
| u16 + (u8, i32)[] | faction goodwill | (community index, value) |
| u16 + (u16, i32)[] | NPC relations | (NPC object id, value) |
| cstr | stats | `arena_battles:0;...|visited,levels` |
| cstr | needs | `sat:0.909;drink:540;sleep:540;rad:0.000` |
| u8   | ? | always 0 |
| u16 + (cstr, cstr)[] | flags | `xrr_imsleep=500` |
| u16 + sized[] | limb health | `health.head`, `timedhp.torso`, ... |
| u16 + sized[] | actor `m_data` | skills, psy, disguise, mod data |
| u64 ×2 | game times (ms) | |

`sized` = cstr key, u16 byte length, typed value.

## Item

| type | field |
|------|-------|
| u32  | object id (on the host) |
| cstr | section |
| u16  | count (ammo boxes) |
| f32  | condition |
| u32  | rounds in magazine |
| u8   | slot index (when place = 1) |
| u8   | place: 1 slot, 2 belt, 3 backpack |
| u8   | uses left |
| u8 + cstr[] | installed upgrades |
| u8   | has data; if 1: u16 byte length, u16 count, (lstr key, typed value)[] |
| u16  | ? always 0 |

Item data keys seen: `m_data`, `parts` (weapon/outfit part conditions),
`healing_charge`.

## Typed values (Lua data)

`01 u8` bool · `02 f64` number · `03 lstr` string ·
`04 u16 n` table of n (typed key, typed value) pairs.

## Takeaways

- It's a high-level character snapshot (sections, conditions, Lua data), not
  engine objects, so it can be built from a live actor in Lua and applied back.
- Item object ids refer to the host's world; a converted record would need
  fresh ids (or xrRazom may reassign them; untested).
- Still unknown: the two angle floats, a few always-0/1 bytes, and whether the
  two game times are "saved at" / "last synced".
