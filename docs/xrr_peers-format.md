# .xrr_peers format (reverse-engineered, partial)

xrRazom writes `<save>.xrr_peers` next to each `.scop`. It holds every
*client's* character; the host's character lives in the `.scop` as usual.
Decoded from 7 sample files (versions 12 and 15). Little-endian, strings are
NUL-terminated unless noted. **?** = guessed.

## Header

| type     | field                         |
|----------|-------------------------------|
| char[4]  | `XPRR`                        |
| u32      | version (12, 15 seen)         |
| u32      | peer count                    |

## Peer record

| type      | field | notes |
|-----------|-------|-------|
| cstr      | player name | xrRazom identity = name from its settings |
| u64       | id | v15: SteamID64. v12: 8-byte LAN id (cf. `appdata/xrrazom_lan_id.bin`) |
| cstr      | faction | `stalker`, `freedom`, ... |
| cstr      | visual | e.g. `actors\stalker_freedom\stalker_freedom_0.ogf` |
| cstr      | outfit section | empty if none |
| cstr      | helmet section | empty if none |
| cstr      | PDA section | |
| cstr      | torch section | |
| cstr      | active weapon section | |
| u32       | ? active slot | 3 in v12 sample, 0 in v15 |
| cstr      | level name | `k00_marsh`, `l02_garbage` |
| f32 x3    | position | |
| f32 ...   | ? direction / health | v12: two floats + `1.0` + u32 0 |
| u32       | money | 5000 / 1148 |
| cstr x4   | quick slots | `medkit`, `bandage`, `medkit_army`, ... |
| ...       | item count | v12: u8 + u16 (46); v15: u8 + u16 (121) |
| item[]    | inventory | see below |
| cstr      | stats | `arena_battles:0;...;wounded_helped:0\|<level>` |
| cstr      | needs | `sat:0.963;drink:240;sleep:240;rad:0.000` |
| kv list   | flags | e.g. `xrr_imsleep=200` |
| table     | limb health | `health.head`, `timedhp.torso`, ... |
| u32 + blob| `m_data` | serialized Lua table: skills, psy, disguise, mods' data |
| table     | `__xrr_meta` | timestamps / save reason (v12) |

## Item record (v12)

`u16 index, u16 0x007e?, cstr section, u16 count/ammo, f32 condition,
u32 ?, u8 slot/flags?, ...` then, for weapons/outfits with parts, an inline
Lua-style table (`parts` → `prt_w_barrel_2 = 96.0` ...).

## Value encoding inside tables (v12)

`03 <u16 len> <bytes>` = string key/value, `02 <f64>` = number,
`04 <u16 count>` = table, `01 <u8>` = bool. v15 changed this layout
(keys became plain cstrs with a trailing type byte) — needs more samples.

## Takeaways

- It's a high-level character snapshot (sections + conditions + Lua data),
  not engine objects, so it can be generated from a live actor in Lua.
- The format changes between xrRazom versions; any tool must check the
  version and refuse unknown ones.
