#!/usr/bin/env python3
"""
Measure the DS4 volume-byte -> analog gain law through a loopback cable
(DS4 headphone out -> PC line/mic in).

Plays a fixed sine tone into the dongle's USB audio sink, steps the raw DS4
volume byte via HID feature report 0xF6 funcid 0x05 (firmware >= the commit
adding the raw-volume debug command), records the loopback input per step and
prints RMS per byte as CSV: byte,rms,dbfs,peak.

The tone plays at a constant digital level; the PipeWire sink volume is forced
to 100% (0 dB software volume) so the only variable is the DS4 byte.

Requires: pip install hidapi; PipeWire/PulseAudio tools (pactl, paplay, parecord).

Examples:
  python volume_sweep.py --steps 0:255:8
  python volume_sweep.py --steps 0,32,64,87,100,128,160,200,255 --seconds 2
  python volume_sweep.py --source alsa_input.pci-0000_0d_00.4.analog-stereo
"""
import argparse
import array
import math
import struct
import subprocess
import sys
import tempfile
import time
import wave

from config_tool import open_device, REPORT_SET, SET_DATA_LEN

FUNC_RAW_VOLUME = 0x05

RATE = 48000
TONE_HZ = 440.0
TONE_DBFS = -12.0  # digital headroom so nothing clips before the DAC


def parse_steps(spec):
    if ":" in spec:
        parts = [int(x, 0) for x in spec.split(":")]
        start, stop = parts[0], parts[1]
        step = parts[2] if len(parts) > 2 else 1
        vals = list(range(start, stop + 1, step))
        if vals[-1] != stop:
            vals.append(stop)
        return vals
    return [int(x, 0) for x in spec.split(",")]


def make_tone_wav(path, seconds):
    amp = 32767 * (10 ** (TONE_DBFS / 20))
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = bytearray()
        for i in range(int(RATE * seconds)):
            s = int(amp * math.sin(2 * math.pi * TONE_HZ * i / RATE))
            frames += struct.pack("<hh", s, s)
        w.writeframes(bytes(frames))


def set_raw_volume(dev, byte):
    data = bytes([REPORT_SET, FUNC_RAW_VOLUME, byte]).ljust(SET_DATA_LEN + 1, b"\x00")
    dev.send_feature_report(data)


def record_rms(source, seconds):
    cmd = ["parecord", "--raw", f"--rate={RATE}", "--channels=2",
           "--format=s16le", f"--device={source}"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    want = int(RATE * seconds) * 4
    data = proc.stdout.read(want)
    proc.terminate()
    proc.wait()
    if len(data) < want // 2:
        sys.exit(f"short capture from {source}: got {len(data)} of {want} bytes")
    samples = array.array("h")
    samples.frombytes(data[: len(data) // 2 * 2])
    rms = math.sqrt(sum(s * s for s in samples) / len(samples))
    peak = max(abs(s) for s in samples)
    return rms, peak


def find_default(kind, needle):
    out = subprocess.run(["pactl", "list", "short", kind],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        name = line.split("\t")[1]
        if needle in name and not name.endswith(".monitor"):
            return name
    sys.exit(f"no {kind} matching '{needle}' found; pass --sink/--source explicitly")


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--steps", default="0:255:4",
                    help="bytes to sweep: start:stop[:step] or comma list (default 0:255:4)")
    ap.add_argument("--seconds", type=float, default=1.0,
                    help="capture length per step (default 1.0)")
    ap.add_argument("--settle", type=float, default=0.4,
                    help="wait after setting the byte before capturing (default 0.4)")
    ap.add_argument("--sink", default=None, help="dongle sink name (default: auto-detect Sony)")
    ap.add_argument("--source", default=None, help="loopback capture source (default: auto-detect pci analog input)")
    args = ap.parse_args()

    steps = parse_steps(args.steps)
    sink = args.sink or find_default("sinks", "Sony")
    source = args.source or find_default("sources", "alsa_input.pci")

    # constant digital level into the dongle
    subprocess.run(["pactl", "set-sink-mute", sink, "0"], check=True)
    subprocess.run(["pactl", "set-sink-volume", sink, "100%"], check=True)

    # generous per-step overhead margin (parecord spawn/teardown) so the tone
    # cannot run out before the last step
    total = args.settle + args.seconds + 2.5
    tone = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    make_tone_wav(tone.name, len(steps) * total + 10)

    dev = open_device()
    player = subprocess.Popen(["paplay", f"--device={sink}", tone.name],
                              stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.0)  # let the stream start
        print(f"# sink={sink}")
        print(f"# source={source}")
        print(f"# tone={TONE_HZ}Hz @ {TONE_DBFS}dBFS, {args.seconds}s per step")
        print("byte,rms,dbfs,peak")
        for byte in steps:
            if player.poll() is not None:
                sys.exit("tone player exited early")
            set_raw_volume(dev, byte)
            time.sleep(args.settle)
            rms, peak = record_rms(source, args.seconds)
            dbfs = 20 * math.log10(rms / 32768) if rms else float("-inf")
            print(f"{byte},{rms:.1f},{dbfs:.2f},{peak}", flush=True)
    finally:
        player.terminate()
        player.wait()
        dev.close()


if __name__ == "__main__":
    main()
