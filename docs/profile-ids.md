# Custom Profile IDs — midi2_cpp

This file registers custom Profile IDs used in sketches that consume midi2_cpp, following the convention `byte 0 = 0x7D` for educational/non-commercial use (MIDI Association reserved range).

## Registered Profiles

None yet. As users add custom profiles, document them here:

| Profile ID (5 bytes hex) | Name | Source | Notes |
|---|---|---|---|
| `7D 00 00 01 00` | (reserved example) | midi2_cpp examples | placeholder for sketches that demonstrate Profile Configuration |

## Registering a new Profile ID

Open a PR adding a row to the table above. Include:

- 5-byte ID in hex with spaces between bytes
- Profile name (snake_case)
- Source repo or sketch where it is defined
- Short description of behavior

For commercial Profile IDs (byte 0 != 0x7D), the MIDI Association registry takes precedence. See M2-102 (Common Rules for MIDI 2.0 Profiles).
