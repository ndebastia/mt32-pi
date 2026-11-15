# MiSTer ↔ mt32-pi control protocol

This note records where the 3-byte control structure lives in pim-32, MiSTer
Main and the FPGA cores, and what a versioned/CRC-capable extension would have
to touch. It is a change map, not a change: the current wire format is unchanged
so existing MiSTer cores keep working.

## Current wire format

The Raspberry Pi is the I²C master; the MiSTer FPGA core is the slave at 7-bit
address `0x45`.

| Direction | Bytes | Meaning |
| --- | --- | --- |
| Pi → FPGA | 1 | `mode`: `0xA0` mute, `0xA1` MT-32/Munt, `0xA2` SoundFont |
| Pi → FPGA | 1 | MT-32 ROM-set index |
| Pi → FPGA | 1 | SoundFont list index, `0xFF` = none |
| FPGA → Pi | 1 | requested mode: `0xA0` reset/mute, `0xA1` MT-32, `0xA2` SoundFont |
| FPGA → Pi | 1 | requested MT-32 ROM set |
| FPGA → Pi | 1 | requested SoundFont index |

There is no length, version, sequence number or checksum. The Pi polls the
three read bytes at 20 Hz and writes the three configuration bytes back after a
change.

## pim-32 side

| File | Lines | Role |
| --- | --- | --- |
| `include/control/misterstatus.h` | 30-44 | `TMisterSynth` values and the packed 3-byte `TMisterStatus` |
| `src/control/mister.cpp` | 29 | `MisterI2CAddress = 0x45` |
| `src/control/mister.cpp` | 62-150 | `Update()`: poll, validate, debounce and apply MiSTer requests |
| `src/control/mister.cpp` | 192-203 | `WriteConfigToMister()`: write the 3-byte reply |
| `include/control/mister.h` | 45-50 | last/pending status and error counters |
| `include/mt32pi.h` | 156-162 | `CMisterControl` and poll interval member |

`IsValidStatus()` (`src/control/mister.cpp:45-60`) is the only validation today:
it checks the mode byte and clamps the ROM-set/SoundFont indices. A corrupted
I²C transfer can still produce a plausible mode value, which is what a CRC would
catch.

## MiSTer Main side

The main binary does not speak the 3-byte protocol directly. It reads the
core's OSD mask and writes the core's extended configuration, and the FPGA core
translates those to/from the I²C slave.

| File | Lines | Role |
| --- | --- | --- |
| `main/menu.cpp` | 5913-6018 | `MENU_MT32PI_MAIN1`: displays `Use MT32-pi`, `Show Info`, default/current config |
| `main/menu.cpp` | 5980 | reads `UIO_GET_OSDMASK`; bits 1-4 are the current synth/ROM/SoundFont |
| `main/menu.cpp` | 6023-6100 | `MENU_MT32PI_MAIN2`: edits `tos_get_extctrl()` / `minimig_get_extcfg()` |
| `main/user_io.h` | 128 | `UIO_GET_OSDMASK` definition (`0x2E`) |
| `main/support/minimig/minimig_config.cpp` | 1280-1300 | `minimig_set_extcfg()` / `minimig_get_extcfg()` |
| `main/support/st/st_tos.cpp` | 80-90 | `tos_get_extctrl()` / `tos_set_extctrl()` |

If a version/CRC is added, these are the places to surface it in the OSD, for
example by using currently unused `UIO_GET_OSDMASK` bits for `PROTO_VERSION` and
`CRC_ERROR`. That keeps the menu usable with old cores, which read those bits as
zero.

## FPGA core side

Every core that supports mt32-pi carries its own copy of the same module:

- `sys/mt32pi.sv` in `MiSTer-devel/ao486_MiSTer`
- `sys/mt32pi.sv` in `MiSTer-devel/Minimig-AGA_MiSTer`
- `sys/mt32pi.sv` in `MiSTer-devel/AtariST_MiSTer`
- the same module in the X68000 core

The relevant block is `i2c_slave` (around lines 156-255 in the current ao486
copy):

| Lines | Role |
| --- | --- |
| 148-154 | `sda_out`, LCD buffer, `mode_req`, `rom_req` |
| 183-190 | I²C start: reset `bcnt`, `ack`, direction |
| 213-227 | address match on `0x45` (and `0x3c` for the LCD) |
| 229-240 | write path: `bcnt == 1/2/3` stores mode/ROM/SoundFont; `bcnt == 3` toggles `mt32_newmode` |
| 241 | `bcnt` increment |
| 246-250 | read path: drives `mode_req`, `rom_req`, `mt32_sf_req` bits |
| 214-217 | address match also sets `mt32_available` |

The core also derives `mt32_available`, `mt32_use`/`mt32_mute` and the OSD mask
bits in its top level (`ao486.sv`, `Minimig.sv`, `AtariST.sv`); those are what
Main reads through `UIO_GET_OSDMASK`.

## Backward-compatible extension proposal

Keep bytes 1-3 exactly as they are. Add optional bytes only after them, so an
old core and an old Pi still interoperate:

1. **Write path (Pi → FPGA)**, after the existing third byte:
   - byte 4: protocol version (`1`)
   - byte 5: CRC-8/ATM over bytes 1-4 (poly `0x07`, init `0x00`)
   - New cores validate the CRC and ignore the write on mismatch, keeping the
     previous configuration. Old cores already ignore `bcnt > 3`.
2. **Read path (FPGA → Pi)**:
   - byte 4: protocol version
   - byte 5: capabilities (`bit 0` = CRC supported)
   - byte 6: CRC-8 over bytes 1-5
   - Old cores leave `sda_out` high for `bcnt > 3`, so the Pi reads `0xFF` and
     falls back to the current 3-byte behaviour. No probing or timing change is
     needed on the note path.
3. **Main/OSD**: use two spare `UIO_GET_OSDMASK` bits for `PROTO_VERSION` and
   `CRC_ERROR`; display them only when non-zero. No change to the existing
   `Use MT32-pi`/`Current Config` fields is required.

The Pi-side addition is confined to `TMisterStatus`, `CMisterControl::Update()`
and `WriteConfigToMister()`. The FPGA addition is confined to the `i2c_slave`
block and the OSD-mask wiring in each core. Because the I²C read/write happens
on the UI core at 20 Hz (not in the audio render path), extending it does not
affect note timing.
