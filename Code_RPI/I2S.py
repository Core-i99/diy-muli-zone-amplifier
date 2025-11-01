import subprocess
import shutil
import threading
import time
import signal
import sys
import os

#!/usr/bin/env python3
"""
I2S.py

Simple internet radio player for Raspberry Pi using an I2S DAC (ALSA device).
- Decodes stream with ffmpeg and pipes raw PCM to aplay (ALSA) for the I2S device.
- Controls: play(url), stop(), set_volume(percent), list_alsa_devices()

Place this file in your project and run:
    python3 I2S.py <stream_url>        # start playback
    python3 I2S.py                     # prints usage

Notes:
- Ensure ffmpeg, aplay and amixer are installed on the Pi (apt install ffmpeg alsa-utils).
- Set the correct ALSA device name (default "hw:0,0" — change if your DAC uses another card/device).
"""


class I2SPlayer:
    def __init__(self, alsa_device="hw:0,0", ffmpeg_bin="ffmpeg", aplay_bin="aplay", amixer_bin="amixer"):
            self.alsa_device = alsa_device
            self.ffmpeg_bin = shutil.which(ffmpeg_bin) or ffmpeg_bin
            self.aplay_bin = shutil.which(aplay_bin) or aplay_bin
            self.amixer_bin = shutil.which(amixer_bin) or amixer_bin
            self.ff_proc = None
            self.aproc = None
            self._monitor_thread = None
            self._stop_event = threading.Event()

    def _check_tools(self):
            for p in (self.ffmpeg_bin, self.aplay_bin):
                    if shutil.which(p) is None:
                            raise RuntimeError(f"Required tool not found in PATH: {p}")

    def play(self, url, sample_rate=44100, channels=2):
            """
            Start streaming the given internet radio URL to the ALSA device.
            If already playing, stop first.
            """
            self.stop()
            self._check_tools()
            self._stop_event.clear()

            ff_args = [
                    self.ffmpeg_bin,
                    "-re",              # read input at native rate (good for live streams)
                    "-i", url,
                    "-vn",              # no video
                    "-ac", str(channels),
                    "-ar", str(sample_rate),
                    "-f", "s16le",      # raw PCM 16-bit little endian
                    "-"]                # output to stdout

            aplay_args = [
                    self.aplay_bin,
                    "-D", self.alsa_device,
                    "-f", "S16_LE",
                    "-c", str(channels),
                    "-r", str(sample_rate),
                    "-"                 # read from stdin
            ]

            # Start ffmpeg -> aplay pipeline
            self.ff_proc = subprocess.Popen(ff_args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            self.aproc = subprocess.Popen(aplay_args, stdin=self.ff_proc.stdout, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # Allow ffmpeg to close stdout when aplay exits
            self.ff_proc.stdout.close()

            # Start monitor thread to watch for process exit
            self._monitor_thread = threading.Thread(target=self._monitor_processes, daemon=True)
            self._monitor_thread.start()

    def _monitor_processes(self):
            # Wait until either process exits or stop requested
            while not self._stop_event.is_set():
                    if self.ff_proc and self.ff_proc.poll() is not None:
                            break
                    if self.aproc and self.aproc.poll() is not None:
                            break
                    time.sleep(0.2)
            # If not explicitly stopped, ensure termination
            if not self._stop_event.is_set():
                    self.stop()

    def stop(self):
            """Stop playback and terminate processes."""
            self._stop_event.set()
            if self.aproc and self.aproc.poll() is None:
                    try:
                            self.aproc.terminate()
                    except Exception:
                            pass
                    try:
                            self.aproc.wait(timeout=1)
                    except Exception:
                            try:
                                    self.aproc.kill()
                            except Exception:
                                    pass
            if self.ff_proc and self.ff_proc.poll() is None:
                    try:
                            self.ff_proc.terminate()
                    except Exception:
                            pass
                    try:
                            self.ff_proc.wait(timeout=1)
                    except Exception:
                            try:
                                    self.ff_proc.kill()
                            except Exception:
                                    pass
            self.aproc = None
            self.ff_proc = None

    def is_playing(self):
            return (self.ff_proc and self.ff_proc.poll() is None) and (self.aproc and self.aproc.poll() is None)

    def set_volume(self, percent, control="SoftMaster"):
            """
            Set volume for the ALSA mixer control (default "SoftMaster"). Percent: 0-100.
            Requires 'amixer' to be available.
            """
            try:
                pct = int(max(0, min(100, int(percent))))
            except Exception:
                raise ValueError("percent must be an integer 0..100")
            if shutil.which(self.amixer_bin) is None:
                raise RuntimeError(f"amixer not found: cannot set volume")
            cmd = [self.amixer_bin, "-q", "set", control, f"{pct}%"]
            print(cmd)
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def list_alsa_devices(self):
            """Return output of 'aplay -l' to help find the correct ALSA device name."""
            if shutil.which(self.aplay_bin) is None:
                    raise RuntimeError("aplay not found")
            p = subprocess.run([self.aplay_bin, "-l"], capture_output=True, text=True)
            return p.stdout.strip()

if __name__ == "__main__":
    STREAM_URL = "http://stream.rockantenne.de/70er-rock/stream/mp3"
    ALSA_DEVICE = "default"
    VOLUME_PERCENT = 0

    player = I2SPlayer(alsa_device=ALSA_DEVICE)  # change alsa_device if your I2S DAC is on another card
    def handle_sigint(signum, frame):
            player.stop()
            sys.exit(0)
    signal.signal(signal.SIGINT, handle_sigint)
    signal.signal(signal.SIGTERM, handle_sigint)

    print("Starting playback:", STREAM_URL)
    # player.set_volume(VOLUME_PERCENT)
    player.play(STREAM_URL)

    # Keep main thread alive while playback runs
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        player.stop()