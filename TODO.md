# TODO

- The volume does scale within 0% and 5% up from 10% it just gets very loud.
  Looks like 10% is the max

- **Polling rate: config SET_REPORT never reaches the firmware.** The dongle
  supports three HID polling modes (`polling_rate_mode`: 0 = 250 Hz,
  1 = 500 Hz, 2 = 1000 Hz "real-time"; current/default is 1). Setting it via
  `tools/config_tool.py set polling_rate_mode=2` silently fails: GET feature
  reports (0xF7 config, 0xF8 version) work, but the 0xF6 SET_REPORT is never
  seen by `tud_hid_set_report_cb` / `pico_cmd_set` (no `[CMD]` output on the
  debug console, config unchanged after re-read; the tool's readback shows a
  bogus clamp to 255). Needs investigation of the TinyUSB control SET_REPORT
  path for feature reports not present in the HID report descriptor
  (0xF6–0xF9 are firmware-only IDs). Once settable, mode 2 is expected to be
  safe for audio: the isochronous audio endpoints run at 1 ms frames
  regardless of the HID bInterval, and full-speed bus bandwidth has ample
  headroom (~390 of ~1150 bytes per frame worst-case).

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
