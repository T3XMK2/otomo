# Protocol v1 — the body and the home

The **body** (the plugin inside the game) knows only the game's addresses: it reads raw facts and applies answers, it
never interprets. The **home** (the ESP32-C3) is the cat: it turns the facts into senses, decides, learns. A new game
version changes only the body; a new idea about the cat changes only the home.

## Transport

UDP, little-endian, one datagram per message, at most 1024 bytes. The home listens on port **7777** and answers to the
sender's address and port. Every message starts with an 8-byte header:

| Offset | Type | Field |
|---|---|---|
| 0 | u16 | magic 0x544F ('OT') |
| 2 | u8 | version = 1 |
| 3 | u8 | type: 0 HELLO, 1 SENSE, 2 TABLE, 3 BYE |
| 4 | u32 | seq (the sender's counter) |

## Body → home

**HELLO (0)** at each quest's start: game id (char[10], `ULES01213`), body kind (u8: 1 = the plugin), body version
(u16); then, optional, the **equipped comrade's hall card** (112 raw bytes) and its training at the Comrade Board (u8,
0 = unknown). The home gives a waiting brain to that comrade, or checks that it is its own (an 8-byte id inside the card)
and keeps its growth; any other comrade is refused (no attach).

**SENSE (1)**, about 5 per second:

| Block | Fields |
|---|---|
| time | u32 body ms; u32 quest timer (30 Hz countdown, 0 = no quest); u8 lease valid; u8 pad; u16 proposals lost under an expired lease (total) |
| cat | behaviour u8, phase u8, animation u16, HP i16, HP max i16, zone u16, paralysis i16, position 3 × f32; nature: level u8, attack type u8, temperament weights 4 × u8, tier u32, skills u64, attack f32, defence f32 |
| hunter | zone u16 (0xFFFF = none), HP u16, action type u8, action id u8, crouch u8, pad u8, poison i16, attack-up u16, defence-up u16, position 3 × f32 |
| monsters | u8 count (≤ 20, the game's own list); each: key u16 (record address >> 4), zone u16, HP u16, action type u8, action id u8, species u8, large u8, position 3 × f32 |
| proposals | u8 count (≤ 16); each u8: the game's proposal, bit 7 = it happened under an expired lease |
| hits | u8 count (≤ 8); each: who u8 (0 cat, 1 hunter, 2 other), monster index u8 (255 = not listed), damage u16 |
| objects | u8 count (≤ 4), placed traps; each: key u16, owner u8 (0 cat, 1 hunter, 2 other), trap u8 (0 the cat's shock trap, 1 shock trap, 2 pitfall), x f32, z f32 |

**BYE (3)** when the quest is over: quest (char[16]), run (char[32]), why (u8: 0 end, 1 abandoned). The home lives the
decisions still pending, records the quest if the brain was attached, and answers BYE: lessons lived (u32), seconds
attached (f32), its step (u32).

## Home → body

**HELLO (0)**: home version (u16), the cat's name (char[16]); optional trailing bytes are ignored by this body.

**TABLE (2)**, the answer to every SENSE — the heartbeat:

| Field | Type |
|---|---|
| ack: the SENSE seq it answers | u32 |
| lease, ms (how long this table holds without news) | u16 |
| attached (1 = the brain decides, 0 = the game's cat) | u8 |
| flags (0) | u8 |
| the table: the answer to each proposal, 0xFF = the game's own | 64 × u8 |
| (unused) | 12 bytes |

## Safety

- The body applies a table only while the home's lease runs: no TABLE for `lease ms` (2000) → the game's cat decides,
  and those proposals are marked lost (the brain never learns from them). Between quests the lease is cleared.
- Every answer in a table is one the cat's own nature allows for that proposal.
- The home never writes game memory; the body never interprets.
