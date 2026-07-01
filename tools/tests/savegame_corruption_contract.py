#!/usr/bin/env python3
"""Savegame corruption and fuzz guardrails for engine-side load code."""

from __future__ import annotations

import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> None:
    if re.search(pattern, haystack, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(f"Missing pattern {pattern!r} in {context}")


def parse_int_constant(relative_path: str, pattern: str, context: str) -> int:
    source = read(relative_path)
    match = re.search(pattern, source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"Missing integer constant for {context}")
    return int(match.group(1))


def parse_ssd_entity_types() -> dict[str, int]:
    source = read("src/ui/GameSSDWindow.h")
    for match in re.finditer(r"enum\s*\{(?P<body>.*?)\};", source, re.MULTILINE | re.DOTALL):
        body = match.group("body")
        if "SSD_ENTITY_BASE" not in body:
            continue

        values: dict[str, int] = {}
        current = -1
        for raw_entry in body.split(","):
            entry = raw_entry.strip()
            if not entry:
                continue
            if "=" in entry:
                name, value = entry.split("=", 1)
                current = int(value.strip())
            else:
                name = entry
                current += 1
            values[name.strip()] = current
        return values

    raise AssertionError("Missing SSD entity type enum")


MAX_STRING_CHARS = parse_int_constant(
    "src/idlib/Lib.h",
    r"#define\s+MAX_STRING_CHARS\s+(\d+)",
    "MAX_STRING_CHARS",
)
MAX_ASYNC_CLIENTS = parse_int_constant(
    "src/framework/async/AsyncNetwork.h",
    r"MAX_ASYNC_CLIENTS\s*=\s*(\d+)",
    "MAX_ASYNC_CLIENTS",
)
SESSION_MAX_SAVEGAME_DICT_KV = 16384
SSD_MAX_ASTEROIDS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_ASTEROIDS\s+(\d+)", "MAX_ASTEROIDS")
SSD_MAX_ASTRONAUT = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_ASTRONAUT\s+(\d+)", "MAX_ASTRONAUT")
SSD_MAX_EXPLOSIONS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_EXPLOSIONS\s+(\d+)", "MAX_EXPLOSIONS")
SSD_MAX_POINTS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_POINTS\s+(\d+)", "MAX_POINTS")
SSD_MAX_PROJECTILES = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_PROJECTILES\s+(\d+)", "MAX_PROJECTILES")
SSD_MAX_POWERUPS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_POWERUPS\s+(\d+)", "MAX_POWERUPS")
SSD_MAX_SAVE_LEVELS = parse_int_constant(
    "src/ui/GameSSDWindow.cpp",
    r"GAME_SSD_MAX_SAVE_LEVELS\s*=\s*(\d+)",
    "GAME_SSD_MAX_SAVE_LEVELS",
)
SSD_MAX_SAVE_WEAPONS = parse_int_constant(
    "src/ui/GameSSDWindow.cpp",
    r"GAME_SSD_MAX_SAVE_WEAPONS\s*=\s*(\d+)",
    "GAME_SSD_MAX_SAVE_WEAPONS",
)
SSD_ENTITY_TYPES = parse_ssd_entity_types()
SSD_POOL_ORDER = (
    ("asteroid", SSD_ENTITY_TYPES["SSD_ENTITY_ASTEROID"], SSD_MAX_ASTEROIDS),
    ("astronaut", SSD_ENTITY_TYPES["SSD_ENTITY_ASTRONAUT"], SSD_MAX_ASTRONAUT),
    ("explosion", SSD_ENTITY_TYPES["SSD_ENTITY_EXPLOSION"], SSD_MAX_EXPLOSIONS),
    ("points", SSD_ENTITY_TYPES["SSD_ENTITY_POINTS"], SSD_MAX_POINTS),
    ("projectile", SSD_ENTITY_TYPES["SSD_ENTITY_PROJECTILE"], SSD_MAX_PROJECTILES),
    ("powerup", SSD_ENTITY_TYPES["SSD_ENTITY_POWERUP"], SSD_MAX_POWERUPS),
)
SSD_ENTITY_POOL_LIMITS = {entity_type: max_count for _, entity_type, max_count in SSD_POOL_ORDER}
SSD_MAX_SAVE_ENTITY_REFS = sum(SSD_ENTITY_POOL_LIMITS.values())


class CorruptSave(ValueError):
    pass


class SaveHeaderReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def read_int(self) -> int:
        if self.offset + 4 > len(self.data):
            raise CorruptSave("truncated int")
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_save_string(self, max_length: int) -> bytes:
        length = self.read_int()
        remaining = len(self.data) - self.offset
        if length < 0 or length > max_length or length > remaining:
            raise CorruptSave(f"invalid save string length {length}")
        value = self.data[self.offset : self.offset + length]
        self.offset += length
        return value

    def read_cstring(self, max_length: int) -> bytes:
        start = self.offset
        for _ in range(max_length):
            if self.offset >= len(self.data):
                raise CorruptSave("truncated cstring")
            value = self.data[self.offset]
            self.offset += 1
            if value == 0:
                return self.data[start : self.offset - 1]
        raise CorruptSave("unterminated cstring")

    def read_dict(self) -> dict[bytes, bytes]:
        count = self.read_int()
        if count < 0 or count > SESSION_MAX_SAVEGAME_DICT_KV:
            raise CorruptSave(f"invalid dict count {count}")
        values: dict[bytes, bytes] = {}
        for _ in range(count):
            key = self.read_cstring(MAX_STRING_CHARS)
            value = self.read_cstring(MAX_STRING_CHARS)
            values[key] = value
        return values


class SSDRestoreGuardReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0
        self.restored_ids: dict[int, set[int]] = {entity_type: set() for entity_type in SSD_ENTITY_POOL_LIMITS}

    def read_int(self) -> int:
        if self.offset + 4 > len(self.data):
            raise CorruptSave("truncated SSD int")
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_count(self, label: str, max_count: int) -> int:
        count = self.read_int()
        if count < 0 or count > max_count:
            raise CorruptSave(f"invalid SSD {label} {count}")
        return count

    @staticmethod
    def validate_index(label: str, value: int, count: int) -> None:
        if value < 0 or value >= count:
            raise CorruptSave(f"invalid SSD {label} {value} for count {count}")

    @staticmethod
    def validate_index_or_end(label: str, value: int, count: int) -> None:
        if value < 0 or value > count:
            raise CorruptSave(f"invalid SSD {label} {value} for count {count}")

    @staticmethod
    def validate_entity_reference(label: str, entity_type: int, entity_id: int) -> None:
        max_count = SSD_ENTITY_POOL_LIMITS.get(entity_type)
        if max_count is None:
            raise CorruptSave(f"invalid SSD {label} type {entity_type}")
        if entity_id < 0 or entity_id >= max_count:
            raise CorruptSave(f"invalid SSD {label} id {entity_id}")

    def parse(self) -> None:
        level_count = self.read_count("level count", SSD_MAX_SAVE_LEVELS)
        weapon_count = self.read_count("weapon count", SSD_MAX_SAVE_WEAPONS)

        current_weapon = self.read_int()
        current_level = self.read_int()
        next_level = self.read_int()
        self.validate_index("current weapon", current_weapon, weapon_count)
        self.validate_index("current level", current_level, level_count)
        self.validate_index_or_end("next level", next_level, level_count)

        for label, entity_type, max_count in SSD_POOL_ORDER:
            count = self.read_count(f"{label} count", max_count)
            for _ in range(count):
                entity_id = self.read_int()
                self.validate_entity_reference(f"{label} id", entity_type, entity_id)
                self.restored_ids[entity_type].add(entity_id)

        entity_ref_count = self.read_count("entity reference count", SSD_MAX_SAVE_ENTITY_REFS)
        for _ in range(entity_ref_count):
            entity_type = self.read_int()
            entity_id = self.read_int()
            self.validate_entity_reference("entity reference", entity_type, entity_id)
            if entity_id not in self.restored_ids[entity_type]:
                raise CorruptSave(f"SSD entity reference points at inactive pool id {entity_id}")

        if self.offset != len(self.data):
            raise CorruptSave("SSD guard payload has trailing bytes")


def int32(value: int) -> bytes:
    return struct.pack("<i", value)


def save_string(value: bytes) -> bytes:
    return int32(len(value)) + value


def save_dict(pairs: tuple[tuple[bytes, bytes], ...] = ()) -> bytes:
    data = int32(len(pairs))
    for key, value in pairs:
        data += key + b"\0" + value + b"\0"
    return data


def valid_retail_header() -> bytes:
    return (
        save_string(b"Quake4")
        + int32(12)
        + save_string(b"maps/game/airdefense1.map")
        + save_string(b"")
        + (save_dict() * MAX_ASYNC_CLIENTS)
    )


def parse_retail_header(data: bytes) -> int:
    reader = SaveHeaderReader(data)
    reader.read_save_string(MAX_STRING_CHARS)
    reader.read_int()
    reader.read_save_string(MAX_STRING_CHARS)
    reader.read_save_string(MAX_STRING_CHARS)
    for _ in range(MAX_ASYNC_CLIENTS):
        reader.read_dict()
    return reader.offset


def expect_corrupt(data: bytes, label: str) -> None:
    try:
        parse_retail_header(data)
    except CorruptSave:
        return
    raise AssertionError(f"{label} unexpectedly parsed as a valid save header")


def ssd_guard_payload(
    *,
    level_count: int = 1,
    weapon_count: int = 1,
    current_weapon: int = 0,
    current_level: int = 0,
    next_level: int = 0,
    pool_counts: dict[str, int] | None = None,
    pool_ids: dict[str, tuple[int, ...]] | None = None,
    entity_ref_count: int | None = None,
    entity_refs: tuple[tuple[int, int], ...] = (),
) -> bytes:
    pool_counts = pool_counts or {}
    pool_ids = pool_ids or {}

    data = (
        int32(level_count)
        + int32(weapon_count)
        + int32(current_weapon)
        + int32(current_level)
        + int32(next_level)
    )

    for label, _, _ in SSD_POOL_ORDER:
        ids = pool_ids.get(label, ())
        data += int32(pool_counts.get(label, len(ids)))
        for entity_id in ids:
            data += int32(entity_id)

    data += int32(len(entity_refs) if entity_ref_count is None else entity_ref_count)
    for entity_type, entity_id in entity_refs:
        data += int32(entity_type) + int32(entity_id)
    return data


def parse_ssd_guard_payload(data: bytes) -> None:
    SSDRestoreGuardReader(data).parse()


def expect_ssd_corrupt(data: bytes, label: str) -> None:
    try:
        parse_ssd_guard_payload(data)
    except CorruptSave:
        return
    raise AssertionError(f"{label} unexpectedly parsed as a valid SSD restore guard payload")


def validate_header_fuzz_model() -> None:
    valid = valid_retail_header()
    if parse_retail_header(valid) != len(valid):
        raise AssertionError("valid save header fixture was not fully consumed")

    expect_corrupt(b"\x00\x01", "truncated game-name length")
    expect_corrupt(int32(-1), "negative game-name length")
    expect_corrupt(int32(MAX_STRING_CHARS + 1) + (b"x" * (MAX_STRING_CHARS + 1)), "oversized game-name length")
    expect_corrupt(save_string(b"Quake4") + int32(12) + int32(100), "map-name length beyond remaining bytes")

    header_before_dicts = (
        save_string(b"Quake4")
        + int32(12)
        + save_string(b"maps/game/airdefense1.map")
        + save_string(b"")
    )
    expect_corrupt(header_before_dicts + int32(-1), "negative persistent-player dictionary count")
    expect_corrupt(
        header_before_dicts + int32(SESSION_MAX_SAVEGAME_DICT_KV + 1),
        "oversized persistent-player dictionary count",
    )
    expect_corrupt(header_before_dicts + int32(1) + b"missing-nul", "unterminated persistent-player key")
    expect_corrupt(
        header_before_dicts + int32(1) + b"key\0" + (b"value" * MAX_STRING_CHARS),
        "unterminated persistent-player value",
    )


def validate_ssd_restore_fuzz_model() -> None:
    parse_ssd_guard_payload(ssd_guard_payload())

    asteroid_type = SSD_ENTITY_TYPES["SSD_ENTITY_ASTEROID"]
    parse_ssd_guard_payload(
        ssd_guard_payload(
            pool_ids={"asteroid": (0,)},
            entity_refs=((asteroid_type, 0),),
        )
    )

    for bad_count in (-1, SSD_MAX_SAVE_LEVELS + 1):
        expect_ssd_corrupt(ssd_guard_payload(level_count=bad_count), f"bad SSD level count {bad_count}")

    for bad_count in (-1, SSD_MAX_SAVE_WEAPONS + 1):
        expect_ssd_corrupt(ssd_guard_payload(weapon_count=bad_count), f"bad SSD weapon count {bad_count}")

    expect_ssd_corrupt(ssd_guard_payload(current_level=-1), "negative SSD current level")
    expect_ssd_corrupt(ssd_guard_payload(current_level=1), "SSD current level beyond restored level count")
    expect_ssd_corrupt(ssd_guard_payload(next_level=-1), "negative SSD next level")
    expect_ssd_corrupt(ssd_guard_payload(next_level=2), "SSD next level beyond restored level count")
    expect_ssd_corrupt(ssd_guard_payload(current_weapon=-1), "negative SSD current weapon")
    expect_ssd_corrupt(ssd_guard_payload(current_weapon=1), "SSD current weapon beyond restored weapon count")

    for label, _, max_count in SSD_POOL_ORDER:
        expect_ssd_corrupt(
            ssd_guard_payload(pool_counts={label: -1}),
            f"negative SSD {label} pool count",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_counts={label: max_count + 1}),
            f"oversized SSD {label} pool count",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_ids={label: (-1,)}),
            f"negative SSD {label} pool id",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_ids={label: (max_count,)}),
            f"oversized SSD {label} pool id",
        )

    unknown_type = max(SSD_ENTITY_POOL_LIMITS) + 1
    expect_ssd_corrupt(
        ssd_guard_payload(entity_refs=((unknown_type, 0),)),
        "unknown SSD entity reference type",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(pool_ids={"asteroid": (0,)}, entity_refs=((asteroid_type, -1),)),
        "negative SSD entity reference id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(pool_ids={"asteroid": (0,)}, entity_refs=((asteroid_type, SSD_MAX_ASTEROIDS),)),
        "oversized SSD entity reference id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_refs=((asteroid_type, 0),)),
        "SSD entity reference to inactive pool id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_ref_count=-1),
        "negative SSD entity reference count",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_ref_count=SSD_MAX_SAVE_ENTITY_REFS + 1),
        "oversized SSD entity reference count",
    )


def validate_session_source_contract() -> None:
    source = read("src/framework/Session.cpp")

    for token in (
        "static bool Session_ReadSaveGameInt( idFile *file, int &value, const char *fieldName, const char *savePath )",
        "if ( bytesRead != sizeof( value ) )",
        "static bool Session_ReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "if ( len < 0 || len > maxLength || len > remainingBytes )",
        "if ( bytesRead != len )",
        "static bool Session_ReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "for ( int len = 0; len < maxLength; len++ )",
        "has unterminated %s",
        "static bool Session_ReadSaveGameDict( idFile *file, idDict &dict, const char *fieldName, const char *savePath )",
        "SESSION_MAX_SAVEGAME_DICT_KV = 16384",
        "if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV )",
        "Session_ReadSaveGameCString( file, key, MAX_STRING_CHARS",
        "Session_ReadSaveGameCString( file, value, MAX_STRING_CHARS",
    ):
        require(source, token, "Session savegame corruption reader contract")

    for field_name in ("game name", "map name", "entity filter"):
        require_regex(
            source,
            rf"Session_ReadSaveGameString\s*\(\s*savegameFile,\s*[^,]+,\s*MAX_STRING_CHARS,\s*\"{field_name}\"",
            f"load savegame bounded {field_name} header read",
        )

    require(source, "if ( !Session_IsSupportedSaveGameName( gamename ) )", "savegame header name allowlist")
    require(source, "for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ )", "fixed persistent-player dictionary slots")
    require_regex(
        source,
        r"Session_ReadSaveGameDict\s*\(\s*savegameFile,\s*mapSpawnData\.persistentPlayerInfo\s*\[\s*i\s*\],"
        r"\s*va\s*\(\s*\"persistent player info %d\"",
        "bounded persistent-player dictionary reads",
    )
    require_regex(
        source,
        r"Session_ReadSaveGameString\s*\(\s*file,\s*gamename,\s*MAX_STRING_CHARS,\s*\"game name\".*?"
        r"Session_ReadSaveGameString\s*\(\s*file,\s*saveMap,\s*MAX_STRING_CHARS,\s*\"map name\"",
        "staged save header validation uses bounded strings",
    )


def validate_minigame_restore_contract() -> None:
    minigame_contracts = {
        "src/ui/GameBustOutWindow.cpp": (
            "GAME_BUSTOUT_MAX_SAVE_ENTITIES = 512",
            "GAME_BUSTOUT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'O' << 24 )",
            "GAME_BUSTOUT_SAVE_VERSION = 1",
            "GameBustOut_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameBustOut_ReadLegacySaveInt",
            "GameBustOut_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameBustOut_ReadSaveInt",
            "GameBustOut_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_MAGIC );",
            "GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_VERSION );",
            "if ( bytesRead != sizeof( value ) )",
            "GameBustOut_ValidateSaveCount",
            "count < 0 || count > GAME_BUSTOUT_MAX_SAVE_ENTITIES",
            "GameBustOut_ValidateEntityIndex",
            "index < 0 || index >= entityCount",
            "ball entity index",
            "powerup entity index",
            "brick entity index",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "GAME_BEARSHOOT_MAX_SAVE_ENTITIES = 64",
            "GAME_BEARSHOOT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'S' << 24 )",
            "GAME_BEARSHOOT_SAVE_VERSION = 1",
            "GameBearShoot_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameBearShoot_ReadLegacySaveInt",
            "GameBearShoot_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameBearShoot_ReadSaveInt",
            "GameBearShoot_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameBearShoot_WriteSaveInt( savefile, GAME_BEARSHOOT_SAVE_MAGIC );",
            "GameBearShoot_WriteSaveInt( savefile, GAME_BEARSHOOT_SAVE_VERSION );",
            "if ( bytesRead != sizeof( value ) )",
            "GameBearShoot_ValidateSaveCount",
            "count < 0 || count > GAME_BEARSHOOT_MAX_SAVE_ENTITIES",
            "GameBearShoot_ValidateEntityIndex",
            "index < 0 || index >= entityCount",
            "turret entity index",
            "bear entity index",
            "gunblast entity index",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "GAME_SSD_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'S' << 16 ) | ( 'D' << 24 )",
            "GAME_SSD_SAVE_VERSION = 1",
            "GAME_SSD_MAX_SAVE_LEVELS = 64",
            "GAME_SSD_MAX_SAVE_WEAPONS = 16",
            "GAME_SSD_MAX_SAVE_ENTITY_REFS = MAX_ASTEROIDS + MAX_ASTRONAUT + MAX_EXPLOSIONS + MAX_POINTS + MAX_PROJECTILES + MAX_POWERUPS",
            "GameSSD_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameSSD_ReadLegacySaveInt",
            "GameSSD_ReadLegacySaveBlock",
            "truncated legacy %s",
            "GameSSD_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameSSD_ReadSaveInt",
            "GameSSD_ReadSaveCount",
            "GameSSD_ReadSavePoolId",
            "GameSSD_ValidateSaveCount",
            "GameSSD_ValidatePoolId",
            "GameSSD_ValidateEntityType",
            "GameSSD_ValidateExpectedEntityType",
            "GameSSD_ValidateEntityReference",
            "GameSSD_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameSSD_WriteSaveInt( savefile, GAME_SSD_SAVE_MAGIC );",
            "GameSSD_WriteSaveInt( savefile, GAME_SSD_SAVE_VERSION );",
            "GameSSD_WriteLevelData",
            "GameSSD_ReadLevelData",
            "GameSSD_WriteAsteroidData",
            "GameSSD_ReadAsteroidData",
            "GameSSD_WriteAstronautData",
            "GameSSD_ReadAstronautData",
            "GameSSD_WritePowerupData",
            "GameSSD_ReadPowerupData",
            "GameSSD_WriteWeaponData",
            "GameSSD_ReadWeaponData",
            "GameSSD_WriteGameStats",
            "GameSSD_ReadGameStats",
            "GameSSD_WriteSaveInt( savefile, entCount );",
            "explosionPool[i].id = i;",
            "pointsPool[i].id = i;",
            "projectilePool[i].id = i;",
            "powerupPool[i].id = i;",
            "GameSSD_ReadSaveCount( savefile, \"asteroid count\", MAX_ASTEROIDS );",
            "GameSSD_ReadSavePoolId( savefile, \"asteroid id\", MAX_ASTEROIDS );",
            "GameSSD_ReadSaveCount( savefile, \"astronaut count\", MAX_ASTRONAUT );",
            "GameSSD_ReadSavePoolId( savefile, \"astronaut id\", MAX_ASTRONAUT );",
            "GameSSD_ReadSaveCount( savefile, \"explosion count\", MAX_EXPLOSIONS );",
            "GameSSD_ReadSavePoolId( savefile, \"explosion id\", MAX_EXPLOSIONS );",
            "GameSSD_ReadSaveCount( savefile, \"points count\", MAX_POINTS );",
            "GameSSD_ReadSavePoolId( savefile, \"points id\", MAX_POINTS );",
            "GameSSD_ReadSaveCount( savefile, \"projectile count\", MAX_PROJECTILES );",
            "GameSSD_ReadSavePoolId( savefile, \"projectile id\", MAX_PROJECTILES );",
            "GameSSD_ReadSaveCount( savefile, \"powerup count\", MAX_POWERUPS );",
            "GameSSD_ReadSavePoolId( savefile, \"powerup id\", MAX_POWERUPS );",
            "GameSSD_ReadSaveCount( savefile, \"level count\", GAME_SSD_MAX_SAVE_LEVELS );",
            "GameSSD_ReadSaveCount( savefile, \"weapon count\", GAME_SSD_MAX_SAVE_WEAPONS );",
            "GameSSD_ReadSaveCount( savefile, \"entity reference count\", GAME_SSD_MAX_SAVE_ENTITY_REFS );",
            "GameSSD_ValidateSaveIndex( \"crosshair index\", currentCrosshair, SSDCrossHair::CROSSHAIR_COUNT );",
            "GameSSD_ValidateExpectedEntityType( \"asteroid entity\", type, SSD_ENTITY_ASTEROID );",
            "GameSSD_ValidateExpectedEntityType( \"astronaut entity\", type, SSD_ENTITY_ASTRONAUT );",
            "GameSSD_ValidateExpectedEntityType( \"explosion entity\", type, SSD_ENTITY_EXPLOSION );",
            "GameSSD_ValidateExpectedEntityType( \"points entity\", type, SSD_ENTITY_POINTS );",
            "GameSSD_ValidateExpectedEntityType( \"projectile entity\", type, SSD_ENTITY_PROJECTILE );",
            "GameSSD_ValidateExpectedEntityType( \"powerup entity\", type, SSD_ENTITY_POWERUP );",
            "GameSSD_ValidateSaveIndex( \"explosion type\", explosionType, EXPLOSION_MATERIAL_COUNT );",
            "GameSSD_ValidateSaveIndex( \"powerup state\", powerupState, POWERUP_STATE_OPEN + 1 );",
            "GameSSD_ValidateSaveIndex( \"powerup type\", powerupType, POWERUP_TYPE_MAX );",
            "GameSSD_ValidateSaveIndex( \"current level\", gameStats.currentLevel, levelCount );",
            "GameSSD_ValidateSaveIndexOrEnd( \"next level\", gameStats.nextLevel, levelCount );",
            "GameSSD_ValidateSaveIndex( \"current weapon\", gameStats.currentWeapon, weaponCount );",
            "GameSSD_ValidateEntityReference( \"explosion buddy\", type, id );",
            "type = GameSSD_ReadSaveInt( savefile, \"entity reference type\" );",
            "id = GameSSD_ReadSaveInt( savefile, \"entity reference id\" );",
            "GameSSD_ValidateEntityReference( \"entity reference\", type, id );",
            "if( ent == NULL || !ent->inUse )",
        ),
    }
    for relative_path, tokens in minigame_contracts.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, f"minigame save restore corruption guard in {relative_path}")

    raw_integer_regressions = {
        "src/ui/GameBustOutWindow.cpp": (
            "savefile->Write( &numLevels",
            "savefile->Write( &numBricks",
            "savefile->Write( &currentLevel",
            "savefile->Write( &gameScore",
            "savefile->Write( &nextBallScore",
            "savefile->Write( &bigPaddleTime",
            "savefile->Write( &ballsRemaining",
            "savefile->Write( &ballsInPlay",
            "savefile->Write( &numberOfEnts",
            "savefile->Write( &ballIndex",
            "savefile->Write( &powerIndex",
            "savefile->Write( &index",
            "savefile->Write( &powerup",
            "savefile->Read( &numLevels",
            "savefile->Read( &numBricks",
            "savefile->Read( &currentLevel",
            "savefile->Read( &gameScore",
            "savefile->Read( &nextBallScore",
            "savefile->Read( &bigPaddleTime",
            "savefile->Read( &ballsRemaining",
            "savefile->Read( &ballsInPlay",
            "savefile->Read( &powerup",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "savefile->Write( &currentLevel",
            "savefile->Write( &goalsHit",
            "savefile->Write( &bearShrinkStartTime",
            "savefile->Write( &windUpdateTime",
            "savefile->Write( &numberOfEnts",
            "savefile->Write( &index",
            "savefile->Read( &currentLevel",
            "savefile->Read( &goalsHit",
            "savefile->Read( &bearShrinkStartTime",
            "savefile->Read( &windUpdateTime",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "savefile->Write(&currentCrosshair",
            "savefile->Read(&currentCrosshair",
            "savefile->Write(&type",
            "savefile->Read(&type",
            "savefile->Write(&currentTime",
            "savefile->Read(&currentTime",
            "savefile->Write(&lastUpdate",
            "savefile->Read(&lastUpdate",
            "savefile->Write(&elapsed",
            "savefile->Read(&elapsed",
            "savefile->Write(&health",
            "savefile->Read(&health",
            "savefile->Write(&count",
            "savefile->Read(&count",
            "savefile->Write(&id",
            "savefile->Read(&id",
            "savefile->Write(&length",
            "savefile->Read(&length",
            "savefile->Write(&beginTime",
            "savefile->Read(&beginTime",
            "savefile->Write(&endTime",
            "savefile->Read(&endTime",
            "savefile->Write(&explosionType",
            "savefile->Read(&explosionType",
            "savefile->Write(&distance",
            "savefile->Read(&distance",
            "savefile->Write(&powerupState",
            "savefile->Read(&powerupState",
            "savefile->Write(&powerupType",
            "savefile->Read(&powerupType",
            "savefile->Write(&ssdTime",
            "savefile->Read(&ssdTime",
            "savefile->Write(&levelCount",
            "savefile->Read(&levelCount",
            "savefile->Write(&weaponCount",
            "savefile->Read(&weaponCount",
            "savefile->Write(&superBlasterTimeout",
            "savefile->Read(&superBlasterTimeout",
            "savefile->Write(&gameStats",
            "savefile->Read(&gameStats",
            "savefile->Write(&entCount",
            "savefile->Read(&entCount",
            "savefile->Write(&(levelData[i])",
            "savefile->Read(&newLevel",
            "savefile->Write(&(asteroidData[i])",
            "savefile->Read(&newAsteroid",
            "savefile->Write(&(astronautData[i])",
            "savefile->Read(&newAstronaut",
            "savefile->Write(&(powerupData[i])",
            "savefile->Read(&newPowerup",
            "savefile->Write(&(weaponData[i])",
            "savefile->Read(&newWeapon",
        ),
    }
    for relative_path, needles in raw_integer_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"Minigame integer field still uses raw host-order IO: {needle!r} in {relative_path}")

    ssd_source = read("src/ui/GameSSDWindow.cpp")
    unchecked_ssd_restore_fields = (
        'count = GameSSD_ReadSaveInt( savefile, "asteroid count" );',
        'id = GameSSD_ReadSaveInt( savefile, "asteroid id" );',
        'count = GameSSD_ReadSaveInt( savefile, "astronaut count" );',
        'id = GameSSD_ReadSaveInt( savefile, "astronaut id" );',
        'count = GameSSD_ReadSaveInt( savefile, "explosion count" );',
        'id = GameSSD_ReadSaveInt( savefile, "explosion id" );',
        'count = GameSSD_ReadSaveInt( savefile, "points count" );',
        'id = GameSSD_ReadSaveInt( savefile, "points id" );',
        'count = GameSSD_ReadSaveInt( savefile, "projectile count" );',
        'id = GameSSD_ReadSaveInt( savefile, "projectile id" );',
        'count = GameSSD_ReadSaveInt( savefile, "powerup count" );',
        'id = GameSSD_ReadSaveInt( savefile, "powerup id" );',
        'levelCount = GameSSD_ReadSaveInt( savefile, "level count" );',
        'weaponCount = GameSSD_ReadSaveInt( savefile, "weapon count" );',
        'entCount = GameSSD_ReadSaveInt( savefile, "entity reference count" );',
    )
    for needle in unchecked_ssd_restore_fields:
        if needle in ssd_source:
            raise AssertionError(f"SSD restore field bypasses bounds helper: {needle!r}")
    if ssd_source.count("ent->id = id;") < 6:
        raise AssertionError("SSD pool restores must stamp each restored pool object's saved id")


def validate_validation_wiring() -> None:
    for relative_path in (
        "tools/validation/openq4_validate.py",
        ".github/workflows/push-verification.yml",
        ".github/workflows/commit-validation.yml",
    ):
        require(read(relative_path), "savegame_corruption_contract.py", f"{relative_path} wiring")


def main() -> None:
    validate_header_fuzz_model()
    validate_ssd_restore_fuzz_model()
    validate_session_source_contract()
    validate_minigame_restore_contract()
    validate_validation_wiring()
    print("savegame_corruption_contract: ok")


if __name__ == "__main__":
    main()
