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
- Sometimes devices starts up with 5% even though shortly after it is 100% battery. maybe add a longer period of uncertainty

- ~~Device is detected as "clone"/"fake" by the web tools.~~ **Resolved
  2026-07-14, verified against both sites.** Cause: feature report 0xA3
  (firmware info) came back 45 instead of 49 bytes over USB — the BT
  response is 3 bytes shorter and the CRC stripping accounts for the rest;
  dualshock-tools' clone check (js/controllers/ds4-controller.js getInfo)
  requires exactly 49 bytes plus a successful 0x81 read. Fixed by
  zero-padding 0xA3 to 48+id bytes; both https://dualshock-tools.github.io/
  and https://ds.daidr.me/ now report the device as genuine, and
  hid-playstation no longer logs "expected 49 got 45" at plug-in.

- Make sure that 2 or more of these firmware dongles are working on one system

- Auto turn off device after 15 minutes of non usage, make this confiurable

- use tools/config_tool.py to make configurations accessible

- **Polling rates: proper testing of all three modes.** Rates measured
  2026-07-14 via hidraw timestamps after a config + USB reconnect per mode:
  mode 0 → bInterval 4, exactly 250 reports/s (median gap 4.00 ms);
  mode 1 → bInterval 2, exactly 500/s (2.00 ms); mode 2 → bInterval 1,
  ~755/s effective with 1.00 ms median gap (capped by the DS4's ~800 Hz BT
  rate). Remaining: subjective input-latency check in a game per mode, and
  verify audio (speaker + mic) stays glitch-free per mode — especially
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

- ~~Microphone input (headset mic through the TRRS jack).~~ **WORKING as of
  2026-07-14** — first capture verified (EarPods mic → BT → SBC decode → USB
  IN → parecord). Protocol (reverse-engineered, none of it documented):
  - The DS4 only offers the mic when it detects a DC load on the TRRS
    sleeve (real headset). ext byte bit 6 = mic present. A bare line-out
    into the jack is not detected.
  - Enable: output-report header byte 2 bit 2 (0x24 instead of 0x20).
    The controller then sends one mic-audio input report per output report
    it receives — it must be clocked like a PS4 does; firmware sends a
    no-op 0x11 output every 8 ms while the USB mic is open and speaker
    audio isn't already clocking the link (mic bits are also carried on
    the 0x17 speaker reports).
  - Mic audio arrives in input reports 0x12..0x19 (controller picks the
    size; observed 0x13 at 1 frame/report and 0x16 at 4 frames/report):
    state block (same layout as 0x11) + SBC frames found by 0x9C syncword.
    Format: 16 kHz mono, 16 blocks, 8 subbands, loudness, bitpool 29,
    66-byte frames (header 9c 31 1d).
  - Firmware decodes with btstack's bluedroid SBC decoder on core1 and
    upsamples 16k mono → 32k stereo (linear interpolation) for the USB IN
    endpoint.
  Remaining: voice-quality listening test, simultaneous speaker+mic
  (full duplex) test, decide mic_select semantics (builtin vs headset mic —
  the EnableMic field is 3 bits; bit meanings beyond 0x04 unknown), and
  whether the 8 ms keepalive should be replaced by echoing the host's
  output-report cadence.
  **Hardening verified on-device 2026-07-14 evening:** mic capture, full
  duplex (speaker tone + mic simultaneously), mic-after-duplex (decoder
  survives), and gamepad input during mic (~500 reports/s, sane touch data,
  no kernel errors) all pass. New minor item: capture delivers ~2.6 s per
  8 s window — stream start latency ~5 s after opening the source;
  investigate (PipeWire node resume vs. firmware enable handshake).
  Known issues found in the first long-run (fixes implemented and built,
  flash pending):
  - A misaligned frame (bare 0x9C matched inside SBC payload during the
    duplex test) wedged the OI decoder permanently — sbc frames kept being
    consumed but no PCM came out until reboot. Fix: match the full frame
    header 9c 31 1d and reinit the decoder on every mic open.
  - During duplex the controller sent report variants whose state block is
    not 0x11-layout; hid-playstation rejected the forwarded reports
    ("invalid num_touch_reports=213"), freezing gamepad input. Fix:
    touch-count sanity check before forwarding; needs a proper layout
    capture for those report ids later.
  - Otherwise stable: 3 h of continuous mic streaming (1.36M SBC frames)
    without a firmware crash, queues healthy.

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
