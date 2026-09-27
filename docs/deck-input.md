# Deck input, version 1

Every request supplies `music_score_id` and exactly one of `deck` or `player_data`.
The master snapshot must include every referenced table, including all five
Memory tables. Explicitly empty Memory tables are valid; absent tables fail.
`deck_id` defaults to 1. Power preview accepts empty member slots; a full-song
analysis may impose a complete formation separately.

## Structured cards

```json
{
  "music_score_id": 10006903,
  "deck": {
    "members": [
      {"member_card_id": 1, "level": 1, "awake_count": 1, "rank": 1,
       "live_skill_level": 1, "gekisou_skill_level": 1},
      null, null, null, null
    ],
    "supports": [{"support_card_id": 1, "level": 1, "rank": 1}],
    "performance_order": [0, 1, 2, 3, 4]
  },
  "account": {
    "character_exp": [{"character_id": 1, "exp": 0}],
    "band_items": [],
    "vip_point": 0,
    "memory_status": {},
    "owned_members": [],
    "owned_supports": []
  }
}
```

Member arrays have exactly five formation slots; supports have zero to five.
Missing tail supports and `null` entries are empty. A support requires a member
in that slot. Card IDs cannot repeat. The leader is formation slot 2.
`performance_order` maps activation index to formation slot and must permute
0..4; its default is `[0,1,2,3,4]`. Level, awake count, rank and both skill levels
default to 1. Explicit values must exist in master; levels beyond the derived
awake/rank limit fail. Leader skill level always comes from member rank master.

All account fields are optional. Character experience defaults to zero for
every master character, including nonplayable characters. `band_items` entries
contain `band_item_id` and `level`. Additional `owned_members`/`owned_supports`
contain IDs of cards outside the formation, used for unlocked Memory counts;
formation cards are automatically owned. `memory_status` uses the GetPlayerData
shape described below. Structured deck/account keys are checked so misspelled
bonuses cannot silently disappear.

## GetPlayerData

`player_data` accepts the full response wrapper containing `playerData` or the
inner player object. `account` cannot accompany it. Standard protobuf JSON
integer strings are accepted, but floats and booleans are rejected. Formation
`slotIndex` and `performanceOrderIndex` must each permute 0..4. Nonpositive IDs
(-1 or 0) denote empty slots. Owned card references must exist.

The adapter reads member `exp`, `awakeCount`, `cardRank`, `liveSkillLevel`, and
legacy `performanceSkillLevel` (the actual native Gekisou getter), support
`exp`/`cardRank`, `characterRank`, `bandItems`, `vip.point` and `memoryStatus`.
It does not return player identifiers, profile fields, authentication, or the
original player object. Numeric protobuf fields omitted from this input use
protobuf zero defaults, which must still resolve to valid master rows.

`memoryStatus` accepts only `memoryMembers` (`memberCardId`), `memorySupports`
(`supportCardId`) and `memoryMusics` (`memoryMusicBonusGroup`). The last field is
a **bonus row ID**, not a group ID. Counts include only owned and explicitly
unlocked cards. Selection preserves master row order and uses current bonuses.

## Explicit context

`deck_context` optionally supplies nonnegative values:

- `music_type_extra_bonus_bp`, `music_tag_extra_bonus_bp`: additional song rates.
- `member_event_rates`, `support_event_rates`: five `[P,T,V]` BP rate vectors.
- `music_memory_points`, `character_memory_points`: five integer point values.
- `as_of`: ISO time with an optional timezone, required for nontrivial Memory
  release dates. Dates without an offset use JST; no current-clock default.
- `memory`: explicitly resolved Memory state, overriding the automatic adapter.

The prepared request may supply `context.music_type` (1..5 or 99) for an
activity song type. It replaces the master song type for matching and leader
conditions. This belongs to preparation, so the public deck request does not
duplicate it.

All omitted rates/points are zero. Each present per-slot array has exactly five
entries. Explicit flat Memory points are an additional caller bonus; callers
must avoid double-counting the same memory in both paths.

Resolved `memory` contains optional `member_level` and `support_level` objects
with `points: [P,T,V]`, and `groups` containing `current` (null or an object with
`points`) and `targets`. Target fields are `band_id`, `member_type`,
`character_id`, `tag_id`, `live_categories`, `gekisou_categories`, and
`gekisou_mission`; they follow the native OR chain. Empty targets do not match.
Optional metadata `id`/`level`/`point` on levels and `id`/`group_id`/`score_rank`
on current bonuses may be retained by the caller, but do not alter arithmetic.
`source` defaults to `caller-resolved`. Stored metadata is not trusted as rules.

## Output and arithmetic

The resolved report contains `total_power`, component `[P,T,V]` BP/point totals,
per-slot growth/memory/bonuses, activation order, and member/support skill IDs
and levels. Account identity is excluded. `source.master` identifies the prepared
master; `source.rules` identifies the recovered unpatched client contract.

Each independent bonus floors separately. Native signed wrap and float32
conversion points are preserved, including the different level/awake/rank
formulas. All dimensions must remain within the documented int32 gameplay
conversion range. Unsupported references or effects fail explicitly.
