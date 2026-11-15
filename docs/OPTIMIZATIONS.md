# pim-32 on Raspberry Pi 3A+ with a MiSTer HAT

This document records the changes made in this fork, why they were made, and how
to verify them. It is deliberately separate from `README.md`, which describes the
upstream feature set.

## Target configuration

- Raspberry Pi 3 Model A+ (BCM2837B0, 4x Cortex-A53 at 1.4 GHz, 512 MiB, Wi-Fi/BT,
  one USB 2.0 port, no Ethernet).
- MiSTer HAT: I2S audio back to the MiSTer, I2C control at address 0x45, GPIO UART
  MIDI at 31250 baud.
- I2C OLED (SSD1306/SH1106), USB MIDI device, Wi-Fi with FTP and RTP/UDP MIDI.
- Powered from the MiSTer user port unless a dedicated supply is used.

The base is **pim-32 v0.18.0** (`macroexp/mt32-pi`), the community continuation of
`dwhinham/mt32-pi`: Circle 51.1.1, newlib 4.5.0, Munt 2.8.3, FluidSynth 2.6.1,
inih r62 and Arm GNU Toolchain 15.2.rel1. The four applicable robustness fixes
from `metaneutrons/mt32-pi` were cherry-picked with attribution.

## Real-time model

| Stage | Budget |
| --- | --- |
| One MIDI byte at 31250 baud | 320 us |
| Three-byte MIDI message | 960 us |
| Audio chunk (256 frames at 48 kHz) | 5.33 ms |
| One-chunk queue + I2S DMA + MiSTer mixing | about 6-8 ms end to end |
| Target | zero underruns, p99 <= 10 ms, bounded jitter |

Rules kept from pim-32:

- MIDI input is interrupt-driven (UART FIQ, USB IRQ) or drained from a ring buffer.
- The audio core renders the synth; MIDI and SysEx are handed to it under a lock
  that is never held across SD/USB I/O.
- SoundFont loading builds a detached synth and swaps it at a render boundary.
- Power, UI and logging work stays off the note path or is constant-time.

## Resource and power optimizations

| Change | Where | Effect |
| --- | --- | --- |
| Sleep UI and audio cores with the ARM generic timer event stream | `src/mt32pi.cpp` (`TimerEventStreamBitForPeriod`, `WaitForEvent`) | Cores 1 and 2 no longer spin at 100 %. The audio core wakes below one sample period; the UI core wakes sub-millisecond and still refreshes at 31.25 Hz. Build option `WFE_SLEEP=1` (default) allows A/B testing. |
| Event-driven audio refill | `CMT32Pi::SoundNeedDataHandler`, `m_bAudioDataNeeded` | The DMA callback wakes the audio core with `SendEvent()` when the queue needs data; the core waits in WFE instead of polling. |
| Firmware throttle polling rate-limited | `src/power.cpp` (`ThrottledStatusInterval`) | The `PROPTAG_GET_THROTTLED` mailbox call runs every 4 s on Pi 3/4 (1 s on Pi 5 for the fan) instead of every main-loop iteration. |
| USB plug-and-play rate-limited | `src/mt32pi.cpp` (`m_nLastUSBUpdateTime`) | The USB tree walk runs at most four times per second, not at spin rate. Hotplug detection stays below 250 ms. |
| OLED page-differential updates | `src/lcd/drivers/ssd1306.cpp` (`WriteFrameBuffer`) | A static screen generates no I2C traffic; a one-line change sends one 8-pixel page (129 bytes) instead of the whole 1 KiB frame. |
| LCD refresh lowered to 31.25 Hz | `src/mt32pi.cpp` (`LCDUpdatePeriodMillis`) | Halves UI redraw work and I2C bus contention with the 20 Hz MiSTer poll. |
| Power-save sequencing | `src/power.cpp`, `src/mt32pi.cpp` | Audio is stopped and the display blanked before the CPU clock is lowered. The clock returns to maximum on the first MIDI/USB/control/MiSTer activity. |
| Larger kernel stacks | `Makefile` (`KERNEL_STACK_SIZE=0x30000`) | 192 KiB per core instead of 128 KiB, leaving margin around Munt's ~98 KiB render frame. |
| FTP write amplification removed | `src/net/ftpworker.cpp` (`Store`) | `f_sync()` no longer runs after every received chunk; `f_close()` flushes at the end of the transfer. |
| Network MIDI parsed on the main task | `src/mt32pi.cpp` (`OnAppleMIDIDataReceived`, `OnUDPMIDIDataReceived`, `UpdateMIDI`) | The synth and UI are no longer touched from network tasks; ingress is drained by the main loop. |
| Config parser bounded | `src/config.cpp` | 16 KiB maximum file, no VLA, every integer option has a documented range, IP parsing is strict. |

The firmware still locks `core_freq=250` for I2C stability; that trade-off is kept
because the MiSTer control channel and OLED share the bus. `dtoverlay=disable-bt`
is set in the Pi 3 section of `config.txt`. `hdmi_blanking` is deliberately not
set: on some firmware revisions it also parks the display PLL that feeds the I2S
bit clock, and the power it saves is not worth a silent audio output.

## Power telemetry

The Pi has no on-board power sensor, so `mt32pi-status.txt` (served read-only by
the FTP server) exposes both software proxies and, optionally, an INA219:

- build version, `wfe=on/off`, PLLD rate and whether the audio device started;
- CPU clock, temperature and the raw `PROPTAG_GET_THROTTLED` word;
- power-save/audio-active flags, audio queue fill, render/sleep counts, dropped
  chunks and the MIDI message counter;
- bus voltage, current and power from an INA219 at `ina219_address` with
  `ina219_shunt_milliohms` (default 0x40 / 100 mOhm).

Set `power_monitor = on` in `[system]` to enable the firmware clock/temperature
samples and probe the INA219; without the chip the first four groups still work.
The probing and sampling run on the UI core (and the firmware reads on the main
loop only when the option is on), so the default note path is unchanged. Poll it
from a PC with `scripts/dev/power-log.py <ip>`, which writes a CSV time series.

## Eco profile

`sdcard/profiles/mister-3a/config.txt` caps the Pi 3A+ at 1.2 GHz with
`arm_freq=1200` (a down-clock; no `over_voltage` needed). Validate it with a dense
FluidSynth passage while polling `mt32pi-status.txt`: `audio_dropped` must stay 0
and `audio_queue_percent` must remain stable. If chunks are dropped, remove the
line or lower `[fluidsynth] polyphony`. Hardware validation of this profile is
still pending on the maintainer's Pi 3A+.

## Security hardening

- FTP is **off by default** in `config.def`; the MiSTer 3A+ profile in
  `sdcard/profiles/mister-3a/` enables it explicitly. Change
  `ftp_username`/`ftp_password` from the defaults.
- FTP command termination no longer indexes `nReceiveBytes - 2`; CR/LF is stripped
  safely and a one-byte command cannot write before the buffer.
- FTP paths are checked component by component; `..` is rejected before any
  fatfs call. `CWD` uses the same check.
- FTP `PORT` (active mode) only accepts the control connection's peer address and
  ports >= 1024, closing the SSRF/FTP-bounce path.
- Custom SysEx commands (`F0 7D ...`: reboot, synth/ROM/SoundFont switches) are
  rejected when they arrive through UDP or RTP-MIDI. Local GPIO/UART/USB MIDI can
  still use them.
- The release updater verifies `SHA256SUMS` when the release provides it and
  refuses to install a mismatched archive. CI now attaches `SHA256SUMS`.
- The panic handler has bounded waits, checked string searches, a 512-byte stack
  buffer and a hardware-watchdog reboot instead of a two-minute busy loop.
- The hardware watchdog is armed after initialization and re-armed every 5 s from
  the main loop (15 s timeout).
- Asserts that performed real work (`fluid_synth_write_*`, `m_pSynth->open`) were
  replaced by checked calls with explicit error handling and a safe fallback.

## MiSTer integration

- The 3-byte I2C status structure is validated: unknown synth values and ROM-set
  indices outside the enum are ignored.
- Changes must be seen twice in a row before being applied, so a single noisy read
  cannot switch a synth or a ROM set. The `Mute` command is acted on immediately
  (reset hanging notes must not wait).
- A transient I2C error no longer silences all notes: three consecutive failures
  trigger `AllSoundOff`, then the bus is left alone for 500 ms (10 polls).
- `Mute` no longer re-sends `AllSoundOff` on every poll; the last MiSTer status is
  recorded after handling it.
- The 20 Hz poll and the OSD response time (<= 100 ms) are unchanged; the control
  path is on the UI core and does not touch the MIDI/audio path.
- The OLED differential update reduces contention with the 20 Hz MiSTer read.

## Web GUI

**Not included in this iteration.** The existing forks with a web UI (`rtzor`,
`ckeisc`) add an HTTP/WebSocket/REST stack and a second control surface, which is
a large attack surface and extra CPU/network work on a 3A+. The security-first
profile keeps the network services to FTP, RTP-MIDI and UDP MIDI, all opt-in.

A read-only status page is a possible next step: a small HTTP server on the main
task serving synth/ROM/SoundFont, temperature, throttle status and counters, with
no write endpoints and no WebSocket. It should be a separate, opt-in build option.
Until then, `mt32pi-status.txt` provides the same read-only diagnostics over the
existing FTP service.

## Verification

```sh
make BOARD=pi3-64
cmake -S tests/host -B build-host-tsan -G Ninja -DPIM32_SANITIZE=thread
cmake --build build-host-tsan
ctest --test-dir build-host-tsan --output-on-failure
# same with -DPIM32_SANITIZE=address;undefined
PIM32_MT32_ROMS=/path/to/roms ctest --test-dir build-host-asan -L roms
```

Status on this branch:

- `make BOARD=pi3-64`: passes (kernel8.img built).
- Host ASan/UBSan suite: 16/16 pass.
- Host TSan suite: 16/16 pass.
- MT-32 ROM tests with the maintainer's ROM folder: 4/4 pass.
- Hardware tests on the Pi 3A+/MiSTer: pending; see
  `docs/SD-CARD-3A-MISTER.md` for the card update procedure.

## Build options and profiles

- `WFE_SLEEP=1` (default) enables the event-stream sleep on 64-bit boards.
  Build with `WFE_SLEEP=0` for a timing A/B against the old spin behaviour.
- `HDMI_CONSOLE=1` sends the log to HDMI; it conflicts with `hdmi_blanking=2`,
  so comment that line out in `config.txt` when using it.
- The `sdcard/profiles/mister-3a/mt32-pi.cfg` profile is the one applied to the
  maintainer's card: I2S audio, `mister = on`, SSD1306 128x64 OLED, USB MIDI
  enabled, Wi-Fi + FTP + RTP/UDP MIDI enabled, FluidSynth polyphony 128. The
  repository's default `sdcard/mt32-pi.cfg` stays generic. For a minimal card,
  set `usb = off`, `mode = off`, `ftp = off`; this saves radio/USB power and
  removes the network attack surface.

## Known limitations

- Hardware power measurement needs an INA219 (or a USB power meter); the
  software proxies alone cannot be converted to watts. The driver, status fields
  and CSV poller are in place for when the shunt is fitted.
- The 1.2 GHz eco profile is implemented in the MiSTer profile but still needs a
  dense FluidSynth soak test on the Pi 3A+.
- `core_freq=250` is required for stable I2C with the current firmware; do not
  remove it without checking the OLED and MiSTer control channel.
- The MiSTer protocol remains a 3-byte structure for compatibility. The change
  map for a versioned/CRC extension is in `docs/MISTER-PROTOCOL.md`; it needs
  matching RTL changes in every core that carries `sys/mt32pi.sv`.
