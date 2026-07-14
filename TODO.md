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

- The volume does scale within 0% and 5% up from 10% it just gets very loud.
  Looks like 10% is the max. Measure this via a loop back cable to microphone.
  (Likely explained by the sticky-volume root cause above; re-test after
  reflash.)

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
  streams silence.

- Dual audio sinks (speaker + headphone jack as two USB audio functions):
  first attempt failed; retry on top of the fixed sequential L2CAP pairing
  path before assuming the audio code was at fault.

- Higher audio quality: captures show joint-stereo bitpool 51 (report 0x18)
  is accepted — nearly double the bitrate of the current bitpool-25 0x17 path.

- **Volume curve fine-tuning.** The USB volume now maps onto DS4 byte 0..100
  (was 0..255, which hit max loudness at ~10% slider). With this mapping the
  slider scales across its full travel, but perceived loudness may top out
  at ~60% slider travel (byte ~87 with the 0..100 mapping — a cubic host
  slider sends ~-13 dB at 60%), so DS4_VOLUME_MAX in src/usb.cpp is set
  to 87. Confirm with a jack→line-in RMS sweep of the raw volume byte to
  find the exact saturation point, and check whether the byte-vs-dB
  response is linear enough or needs a curve.

- **Perceived loudness curve is not linear in slider travel** (feels
  logarithmic: big jumps at the bottom, compressed at the top). Three curves
  stack up: the host's cubic slider→dB mapping, our linear dB→byte mapping,
  and the DS4's unknown byte→gain law. If the RMS sweep (above) shows the
  byte→gain law is roughly linear in dB, the current mapping is correct and
  the feel comes from the host curve; otherwise insert a compensation curve
  in ds4_push_volume(). By-ear data so far is inconsistent (0..100 mapping
  pegged at ~60% slider, 0..87 mapping at ~20%) — no host-curve model fits
  both, so stop tuning by ear: do the measured sweep (jack→line-in, fixed
  tone, step the host volume, RMS per step) and derive the mapping from data.
