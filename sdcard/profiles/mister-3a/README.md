# MiSTer / Raspberry Pi 3A+ profile

Copy these two files over `config.txt` and `mt32-pi.cfg` at the root of the SD
card. They reproduce the card used for the Pi 3A+ + MiSTer HAT validation:

- I2S audio back to the MiSTer.
- `mister = on`, I2C OLED 128x64 (SSD1306 at 0x3C).
- USB MIDI enabled, GPIO UART MIDI at 31250 baud.
- Wi-Fi, DHCP, RTP-MIDI, UDP MIDI and FTP enabled. **Change the FTP credentials**
  before using the card on a shared network.
- FluidSynth polyphony 128; GeneralUser GS v2.0.3 is index 0, the existing SC-55
  SoundFont is index 1, hence `soundfont = 1`.
- `avoid_warnings=2` is preserved from the original card because the Pi is powered
  from the MiSTer user port. Keep the Pi heatsinked; remove the line if you see
  stutters or random reboots.

Do not copy the boot firmware (`bootcode.bin`, `start*.elf`, `fixup*.dat`, DTBs)
from this profile; keep the files already on the card. Do not overwrite
`wpa_supplicant.conf` unless you intend to change the Wi-Fi network.

See `docs/SD-CARD-3A-MISTER.md` for the update and rollback procedure.
