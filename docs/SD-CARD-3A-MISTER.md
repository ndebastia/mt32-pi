# Updating the Raspberry Pi 3A+ SD card (MiSTer profile)

Target: the card currently mounted as `G:\` on the Windows host, originally
created from a stock mt32-pi/pim-32 image and already configured. The update
replaces the kernel and the two configuration files, adds the legally obtained
MT-32 ROMs and the bundled GeneralUser GS SoundFont, and leaves the boot firmware,
Wi-Fi credentials and any user files in place.

## 1. Back up the card

Copy the whole `G:\` tree to a folder on the PC, or clone the card with the
Raspberry Pi Imager/Win32DiskImager. Keep the backup until the Pi has booted and
played a test MIDI file on the MiSTer.

## 2. Files to copy

From the build/release tree to the root of `G:\`:

| File | Action |
| --- | --- |
| `kernel8.img` | Replace with the newly built `pi3-64` kernel. |
| `config.txt` | Replace with `sdcard/config.txt` from this branch (Pi 3A+ / MiSTer profile). |
| `mt32-pi.cfg` | Replace with `sdcard/mt32-pi.cfg` from this branch, then set your Wi-Fi/FTP credentials if they differ. |
| `roms/*.rom` | Copy the legally obtained ROM files from `D:\Jeux\mt32-pi-roms`. Keep the existing ROMs if they are the same files. |
| `soundfonts/GeneralUser GS v2.0.3.sf2` | Add the bundled GeneralUser GS v2.0.3 SoundFont (see the release/CI package). |
| `soundfonts/GeneralUser GS v2.0.3.cfg` | Add the matching effects profile. |
| `docs/` | Optional: `OPTIMIZATIONS.md`, this file, the licence and the GeneralUser GS notices. |

**Do not overwrite** `bootcode.bin`, `start*.elf`, `fixup*.dat`, the `.dtb` files
or `wpa_supplicant.conf` unless you intentionally want to change the boot firmware
or Wi-Fi credentials. The original card already has them and the new kernel uses
the same boot files.

`cmdline.txt` only contains `fast=true` in pim-32. If your card has additional
options, keep them.

## 3. Configuration profile

The shipped `mt32-pi.cfg` is set for:

- `[audio] output_device = i2s` (MiSTer HAT)
- `[control] mister = on`
- `[lcd] type = ssd1306_i2c`, `width = 128`, `height = 64` (the installed OLED)
- `[system] usb = on`, `i2c_baud_rate = 400000`, `power_save_timeout = 300`
- `[network] mode = wifi`, `dhcp = on`, `rtp_midi = on`, `udp_midi = on`,
  `ftp = on`
- `[fluidsynth] polyphony = 128` (lower than the 200 default to leave CPU and
  thermal headroom on the 3A+), `soundfont = 1` to keep the existing SC-55
  SoundFont as the default after GeneralUser GS is added at index 0
- `[mt32emu] rom_set = old` (MT-32 v1.x control ROM)

Set `ftp_username`/`ftp_password` to non-default values before enabling FTP on a
shared network. For a minimal card, set `usb = off`, `mode = off` and `ftp = off`;
the device still works with the MiSTer HAT.

`config.txt` for the Pi 3 section adds `hdmi_blanking=2` and
`dtoverlay=disable-bt`. Comment out `hdmi_blanking=2` if you use an
`HDMI_CONSOLE=1` build. The optional `arm_freq=1200` eco profile is commented out;
validate it with FluidSynth at your usual polyphony before enabling it.

The existing card already had `avoid_warnings=2`; it was preserved because the Pi
is powered from the MiSTer. Remember that it disables undervoltage detection and
CPU throttling: keep the Pi heatsinked, and remove the line if you see audio
stutters or random reboots.

## 4. ROMs

The ROM files in `D:\Jeux\mt32-pi-roms` are used only for local testing and
validation, under the user's statement that they were legally obtained for
non-commercial use. They are not part of the repository and must not be added to
the release archive. The ROM manager detects control and PCM ROMs by content, so
the file names do not matter.

## 5. First boot checklist

1. Insert the card in the Pi 3A+, connect the MiSTer user port and power on.
2. Watch the OLED: the version splash should appear, then the MiSTer logo when
   the I2C control channel is detected.
3. On the MiSTer OSD, open the MT32-pi submenu and confirm that synth, ROM set and
   SoundFont can be changed. `Reset Hanging Notes` should silence notes.
4. Start a core (AO486, Atari ST, Minimig or X68000) and play a known MT-32 track.
5. Leave the device idle for `power_save_timeout`; the display should switch off
   and the first MIDI note should wake it without a stuck note.
6. If the OLED works but the OSD does not, check the I2C wiring/address and keep
   `i2c_baud_rate = 400000` (do not raise it to 1 MHz on a marginal HAT).
7. If you see `Low voltage! Chk PSU`, prefer a short USB cable or a dedicated
   5 V supply for the Pi. Do not enable `avoid_warnings=2` unless the Pi is
   heatsinked and you understand that it also disables throttling.

## 6. Rollback

Copy the backed-up `kernel8.img`, `config.txt` and `mt32-pi.cfg` back to `G:\`.
The ROMs and SoundFonts can stay; they are data files. The original boot files were
never modified.

## 7. What changed in the kernel

See `docs/OPTIMIZATIONS.md` for the full list. The short version: idle UI/audio
cores sleep in WFE, audio refills are event-driven, firmware/USB polling is
rate-limited, OLED updates are page-differential, power-save sequences audio and
display before lowering the clock, FTP is hardened and off by default, network
MIDI is parsed on the main task, custom remote-control SysEx is rejected from the
network, and the watchdog reboots a hung device.
