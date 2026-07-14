# TODO

- **ROOT CAUSE FOUND (2026-07-14) for all volume weirdness on Linux:** the
  kernel never created a hardware volume control for the dongle. GET_CUR
  recomputed the volume from config on every read instead of returning the
  last SET value, so the kernel's write-then-readback probe
  (check_sticky_volume_control, sound/usb/mixer.c) flagged the mixer as
  "sticky mixer values ... disabling" (visible in dmesg), dropped the volume
  control, and deliberately pinned the device at max volume. The desktop
  slider was pure PipeWire *software* volume the whole time — the firmware's
  dB→byte mapping (incl. DS4_VOLUME_MAX=87) was never exercised, which is why
  no by-ear model ever fit. Fixed in firmware (GET_CUR now returns the last
  SET value; config only seeds the initial value). After reflash, verify
  `pactl list sinks` shows HW_VOLUME_CTRL and dmesg no longer logs the sticky
  warning. All previous by-ear observations below are void and need re-testing
  with hardware volume active.

- ~~The volume does scale within 0% and 5% up from 10% it just gets very
  loud.~~ **Resolved 2026-07-14** by the sticky-volume fix plus the measured
  mapping below. Re-test on macOS to confirm the log-feel is gone there too.

- Add Microphone support or check if working. I will wire the cable to my speaker out for that. Ask me to do it.

- Device is detected as "clone" by https://dualshock-tools.github.io/ and "fake" by https://ds.daidr.me/ why is that?
  Lead (2026-07-14): hid-playstation logs "Invalid byte count transferred,
  expected 49 got 45 / Failed to retrieve DualShock4 firmware info: -22" at
  every plug-in — feature report 0xA3 (firmware info) comes back 4 bytes
  short over USB. main.cpp's generic BT→USB feature forwarding strips
  1 id + 4 CRC bytes from the BT response; for 0xA3 that leaves 45 instead
  of the 49 a real DS4 returns. Confirmed in dualshock-tools source
  (js/controllers/ds4-controller.js getInfo): clone = 0xA3 response not
  exactly 49 bytes, plus feature 0x81 must succeed (it does). Fix
  implemented (0xA3 zero-padded to 48+id bytes); verify against both web
  tools after reflashing. ds.daidr.me source not found; likely probes the
  same report.

- Make sure that 2 or more of these firmware dongles are working on one system

- Auto turn off device after 15 minutes of non usage, make this confiurable

- use tools/config_tool.py to make configurations accessible

- **Polling rates: proper testing of all three modes.** Setting them works
  now (item below), but only mode 2 has been measured (~760 Hz effective,
  median report gap 1.00 ms — capped by the DS4's ~800 Hz BT rate). Test
  each of mode 0 (250 Hz), 1 (500 Hz), 2 (1000 Hz): measure the actual
  input-report rate and gap distribution via hidraw timestamps, confirm the
  enumerated bInterval per mode, check input latency subjectively in a game,
  and verify audio (speaker + mic) stays glitch-free per mode — especially
  mode 2 alongside isochronous audio. Also check whether mode 2's
  send-only-when-dirty path drops the report rate when the controller idles
  and whether hosts/games mind.

- ~~Polling rate: config SET_REPORT never reaches the firmware.~~ **Resolved
  2026-07-14: does not reproduce against the current firmware.**
  `config_tool.py set polling_rate_mode=2` updates RAM and flash, and after a
  0xF6/0x03 USB reconnect the gamepad endpoints enumerate with bInterval 1
  (1000 Hz). The earlier failure was most likely an older flashed build (the
  dongle has been reflashed since for the volume-mapping commits). Mode 2 is
  now active on the test dongle; revert with
  `tools/config_tool.py set polling_rate_mode=1` plus a replug if problems
  show up.

- Microphone input (headset mic through the TRRS jack): BT report format is
  undocumented; needs a capture session against a PS4. Descriptor is present,
  streams silence. Findings 2026-07-14: a PC line-out wired into the jack is
  NOT enough — the DS4 senses a mic via DC load on the TRRS sleeve and
  reports headphones=1 mic=0 (input-report ext byte bit 6), and it streams no
  extra BT reports then, regardless of output volume-flag pokes (tried via
  0xF6/0x06 raw output debug cmd). Next: plug a real CTIA headset (mic=1),
  then watch the debug console ([BT] logger in on_bt_data) for the mic report
  id; alternatively put ~2.2 kΩ sleeve→GND in parallel with the line feed.

- Dual audio sinks (speaker + headphone jack as two USB audio functions):
  first attempt failed; retry on top of the fixed sequential L2CAP pairing
  path before assuming the audio code was at fault.

- Higher audio quality: captures show joint-stereo bitpool 51 (report 0x18)
  is accepted — nearly double the bitrate of the current bitpool-25 0x17 path.

- ~~Volume curve fine-tuning / perceived loudness curve.~~ **Resolved
  2026-07-14 with measured data** (tools/volume_sweep.py, jack→line-in RMS
  sweep of the raw byte 0..255): the DS4 headphone amp is linear in dB at
  1.0 dB per byte (0.97 measured) from byte ~20 to byte 80 and saturates
  hard at byte 80 (bytes 80..255 identical within 0.01 dB). Firmware now
  maps byte = 80 + dB and advertises −60..0 dB as the feature-unit range.
  End-to-end verification: sweeping the host hardware control 0..60 tracks
  the analog output dB-for-dB (±0.15 dB) over the full range, with mild
  compression only at the last −56/−60 dB steps. All previous by-ear
  observations were taken with the byte pinned at max (sticky-volume bug)
  and are void.
