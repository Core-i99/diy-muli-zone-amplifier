from pathlib import Path
import queue
from luma.core.interface.serial import spi
from luma.core.render import canvas
from luma.lcd.device import ili9488
import time
from PIL import ImageFont
import sys
import logging
import threading
import copy
import signal
import shutil
import subprocess
import os

# Try to import RPi.GPIO; provide a no-op fallback when not available (e.g., on dev machines)
try:
    import RPi.GPIO as GPIO
except Exception:
    logging.warning("RPi.GPIO not available; using fallback no-op GPIO")

    class _NoOpGPIO:
        # Minimal constants to allow code to reference them without hardware
        BCM = None
        IN = None
        OUT = None
        PUD_UP = None
        PUD_DOWN = None
        RISING = None
        FALLING = None
        BOTH = None

        def setmode(self, *_):
            return None

        def setwarnings(self, *_):
            return None

        def setup(self, *_):
            return None

        def output(self, *_):
            return None

        def cleanup(self, *_):
            return None

        def input(self, *_):
            # Always return False/LOW when running in a non-RPi environment
            return False

    GPIO = _NoOpGPIO()


serial = spi(port=0, device=0, gpio_DC=23, gpio_RST=24)
device = ili9488(serial, rotate=2, gpio_LIGHT=5)

# Load bold TTF fonts (preferred). If not available, fall back to default PIL font.
def _load_font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()

# Prefer a local font in Code_RPI/fonts/Roboto-Bold.ttf if present, otherwise fall back
local_font = Path(__file__).resolve().parent / "fonts" / "Roboto-Bold.ttf"
if local_font.exists():
    _FONT_PATH_BOLD = str(local_font)
else:
    print(f"Font file {local_font} not found, exiting.")
    sys.exit(1)
    

# Preload fonts at sizes used by the UI
HEADER_TWO_LINE_FONT = _load_font(_FONT_PATH_BOLD, 25)
HEADER_SINGLE_LINE_FONT = _load_font(_FONT_PATH_BOLD, 30)
ZONE_FONT = _load_font(_FONT_PATH_BOLD, 50)


# Attempt to import the I2C reader from the nearby module. If unavailable
# (for instance running on a dev machine without smbus2), provide a
# fallback that raises at call time — the code that calls this will handle
# exceptions and keep previous values.
try:
    from I2C import read_raw_after_trigger
except Exception as exc:  # pragma: no cover - fallback for non-RPi/dev
    logging.warning("Could not import I2C.read_raw_after_trigger: %s", exc)

    def read_raw_after_trigger(bus_num: int = 1, addr: int = 8, num_bytes: int = 3, tries: int = 3, delay: float = 0.05):
        """Fallback stub when smbus2/I2C is not available.

        This raises to let callers decide how to handle missing hardware.
        """
        raise RuntimeError("I2C read not available in this environment")


def read_zone_state(addr: int, bus_num: int = 1, num_bytes: int = 3):
    """Read a zone controller at `addr` and return (enabled: bool, volume: int).

    On error returns (None, None) to allow callers to detect a failed read and
    keep previous values. Uses the same parsing as `I2C.py` (little-endian
    int16 + bool).
    """
    try:
        raw = read_raw_after_trigger(bus_num, addr, num_bytes)
    except Exception as exc:
        logging.warning("I2C read failed for addr %s: %s", addr, exc)
        # Signal caller that the read failed so it can keep previous state.
        return None, None

    if not raw or len(raw) < 3:
        logging.warning("Unexpected raw response from addr %s: %r", addr, raw)
        return None, None

    # parse little-endian int16 (volume) and a trailing enabled byte
    volume = int(raw[0]) | (int(raw[1]) << 8)
    # convert to signed int16 if needed
    if volume >= 0x8000:
        volume -= 0x10000
    enabled = bool(raw[2])
    # Validate volume range. A signed int16 wrap (e.g. -256) or other
    # corrupted values are better treated as a failed read so we don't
    # overwrite the previous valid state.
    if volume is None or volume < 0 or volume > 100:
        logging.warning("Invalid volume read from addr %s: %r (skipping)", addr, volume)
        return None, None
    return enabled, volume


# Hardcoded radio stations mapping (name -> stream URL). Using an
# insertion-ordered dict so code can rely on a stable order when
# presenting and cycling stations.
RADIO_STATIONS = {
    "Test": "http://stream.rockantenne.de/70er-rock/stream/mp3",
    "Radio 2 Antwerpen": "http://icecast.vrtcdn.be/ra2ant-high.mp3",
    "Nostalgie": "https://playerservices.streamtheworld.com/api/livestream-redirect/NOSTALGIEWHATAFEELING.mp3",
    "JOE FM": "https://playerservices.streamtheworld.com/api/livestream-redirect/JOE_SC",
    "Radio 1": "http://icecast.vrtcdn.be/radio1-high.mp3",
    "Studio Brussel": "http://icecast.vrtcdn.be/stubru-high.mp3",
    "MNM": "http://icecast.vrtcdn.be/mnm-high.mp3",
}


# Hardware pin assignments for the input selector (BCM numbering).
# These are module-level so helper functions and callbacks can access them.
INPUT_SELECT_A = 16
INPUT_SELECT_B = 12
# Backlight control pin (BCM)
BACKLIGHT_PIN = 6

# Backlight state / activity tracking
# timestamp of last state change (seconds since epoch)
_last_state_change_ts = time.time()
# current backlight logical state (True=on, False=off)
_backlight_on = True
# lock to protect backlight state
_backlight_lock = threading.Lock()


def set_input_select_pins(input_source: str):
    """Set the INPUT_SELECT_A / INPUT_SELECT_B pins according to the
    given input_source string. Mapping follows the motherboard logic:
      Internet Radio -> A=LOW,  B=LOW
      CD1           -> A=LOW,  B=HIGH
      CD2           -> A=HIGH, B=LOW
      CD3           -> A=HIGH, B=HIGH

    This function is defensive: it tolerates the development fallback
    GPIO implementation and logs errors rather than raising.
    """
    # Determine logical HIGH/LOW values in a portable way.
    high = getattr(GPIO, 'HIGH', 1) or 1
    low = getattr(GPIO, 'LOW', 0) or 0

    mapping = {
        'Internet Radio': (low, low),
        'CD1': (low, high),
        'CD2': (high, low),
        'CD3': (high, high),
    }

    a_state, b_state = mapping.get(input_source, (low, low))
    try:
        GPIO.output(INPUT_SELECT_A, a_state)
        GPIO.output(INPUT_SELECT_B, b_state)
    except Exception:
        # Don't fail the UI if GPIO isn't available; log and continue.
        logging.exception("Failed to set input select pins for %s", input_source)


def set_backlight(on: bool):
    """Set the hardware backlight pin (best-effort).

    This is safe to call in environments without real GPIO; failures are
    logged but ignored so the UI continues to work on development machines.
    """
    global _backlight_on
    high = getattr(GPIO, 'HIGH', 1) or 1
    low = getattr(GPIO, 'LOW', 0) or 0
    try:
        GPIO.output(BACKLIGHT_PIN, low if on else high)
    except Exception:
        # non-fatal on dev machines
        logging.debug("set_backlight: GPIO.output not available")
    with _backlight_lock:
        _backlight_on = bool(on)


def mark_state_changed():
    """Record that the state has changed and ensure backlight is on.

    Call this whenever any value in `state` is modified so the idle timer
    will be reset and the backlight will be turned on.
    """
    global _last_state_change_ts
    _last_state_change_ts = time.time()
    try:
        set_backlight(True)
    except Exception:
        logging.exception("mark_state_changed: set_backlight failed")


def backlight_watcher(stop_evt, timeout_seconds: float = 60.0, check_interval: float = 1.0):
    """Background watcher that turns the backlight off after `timeout_seconds`
    seconds of no recorded state changes. The watcher respects `stop_evt` and
    exits promptly when set.
    """
    while not stop_evt.is_set():
        try:
            now = time.time()
            last = _last_state_change_ts
            with _backlight_lock:
                currently_on = _backlight_on
            if last is not None and (now - last) >= float(timeout_seconds) and currently_on:
                try:
                    set_backlight(False)
                except Exception:
                    logging.exception("backlight_watcher: failed to turn backlight off")
            stop_evt.wait(check_interval)
        except Exception:
            logging.exception("backlight_watcher encountered an unexpected error")
            stop_evt.wait(check_interval)



def render_display(state):
    """Render the full display based on the provided state dict.

    state keys:
      - input_source: str ("CD1","CD2","CD3","Internet Radio")
      - radio_station_selected: bool
      - station_name: str or None
            - zones: list[bool] length 3 (True=ON)
            - volumes: list[int] length 3 (volume per zone)
    """
    with canvas(device) as draw:
        # Background
        draw.rectangle([(0, 0), (480, 320)], fill="black")

        # Header area
        draw.rectangle([(0, 0), (480, 80)], fill="blue")

        # Header text positioning and content. Use preloaded bold fonts.
        if state["input_source"] == "Internet Radio" and state["radio_station_selected"] and state.get("station_name"):
            text = f"INPUT: Internet Radio\n{state['station_name']}"
            text_y = 10
            header_font = HEADER_TWO_LINE_FONT
        else:
            text = f"Invoer: {state['input_source']}"
            text_y = 20
            header_font = HEADER_SINGLE_LINE_FONT

        bbox = draw.multiline_textbbox((0, 0), text, font=header_font)
        text_width = bbox[2] - bbox[0]
        screen_width = device.bounding_box[2] - device.bounding_box[0]
        x = int((screen_width - text_width) / 2)
        draw.multiline_text((x, text_y), text, fill="white", font=header_font, align="center")

        # Zone status and volume
        # Be defensive: the system may be configured with fewer than 3
        # zone controllers (for example, a single zone). Use the actual
        # configured lengths and fall back to sensible defaults when
        # accessing lists to avoid IndexError during rendering.
        zones = state.get("zones", []) or []
        volumes = state.get("volumes", []) or []
        n_zones = max(1, len(zones), len(volumes))

        for i in range(n_zones):
            y = 90 + i * 75
            draw.text((5, y), f"ZONE {i+1}", fill="white", font=ZONE_FONT)

            zone_on = zones[i] if i < len(zones) else False
            status = "AAN" if zone_on else "UIT"
            color = "lightgreen" if zone_on else "red"
            draw.text((225, y), status, fill=color, font=ZONE_FONT)

            # show volume as a smaller number on the right (safe-index)
            vol = volumes[i] if i < len(volumes) else 0
            vol_text = f"{vol}%"
            # measure width and place to the right side
            bbox_v = draw.textbbox((0, 0), vol_text, font=ZONE_FONT)
            vol_w = bbox_v[2] - bbox_v[0]
            screen_width = device.bounding_box[2] - device.bounding_box[0]
            draw.text((screen_width - vol_w - 10, y), vol_text, fill="white", font=ZONE_FONT)


def choose_input(state):
    print("Select input:")
    choices = ["CD1", "CD2", "CD3", "Internet Radio"]
    for idx, c in enumerate(choices, start=1):
        print(f"  {idx}. {c}")
    sel = input("Choose input number: ").strip()
    try:
        i = int(sel) - 1
        if 0 <= i < len(choices):
            state["input_source"] = choices[i]
            # If Internet Radio was chosen, immediately ask to select a station.
            if state["input_source"] == "Internet Radio":
                choose_radio_station(state)
            else:
                # If not Internet Radio, clear radio selection
                state["radio_station_selected"] = False
                state["station_name"] = None
            # record state change (reset backlight idle timer)
            try:
                mark_state_changed()
            except Exception:
                logging.exception("choose_input: mark_state_changed failed")
    except ValueError:
        print("Invalid choice")


def choose_radio_station(state):
    if state["input_source"] != "Internet Radio":
        print("Input is not Internet Radio. Switch input to Internet Radio to choose a station.")
        return
    print("Radio stations:")
    print("  0. <No station selected>")
    for idx, name in enumerate(RADIO_STATIONS, start=1):
        print(f"  {idx}. {name}")
    sel = input("Choose station number: ").strip()
    try:
        i = int(sel)
        if i == 0:
            state["radio_station_selected"] = False
            state["station_name"] = None
        elif 1 <= i <= len(RADIO_STATIONS):
            state["radio_station_selected"] = True
            state["station_name"] = RADIO_STATIONS[i - 1]
        else:
            print("Invalid station number")
    except ValueError:
        print("Invalid choice")
    # record state change (reset backlight idle timer)
    try:
        mark_state_changed()
    except Exception:
        logging.exception("choose_radio_station: mark_state_changed failed")


def toggle_zone(state):
    print("Toggle zone (1-3)")
    sel = input("Zone number: ").strip()
    try:
        i = int(sel) - 1
        if 0 <= i < 3:
            state["zones"][i] = not state["zones"][i]
            print(f"Zone {i+1} set to {'ON' if state['zones'][i] else 'OFF'}")
            # record state change (reset backlight idle timer)
            try:
                mark_state_changed()
            except Exception:
                logging.exception("toggle_zone: mark_state_changed failed")
        else:
            print("Invalid zone number")
    except ValueError:
        print("Invalid input")


def poll_zones(state, addresses=[8, 9, 10], bus_num: int = 1): # 8, 9, 10
    """Poll each zone controller via I2C and update state['zones'] and state['volumes'].

    This function is resilient: on read errors it keeps previous values and
    logs a warning.
    """
    # Ensure volumes list exists
    if "volumes" not in state:
        state["volumes"] = [0] * 3

    for idx, addr in enumerate(addresses):
        try:
            res = read_zone_state(addr, bus_num)
            # read_zone_state returns (None, None) on invalid/corrupted read.
            if not res or res[0] is None:
                logging.warning("Skipping update for addr %s due to invalid read", addr)
                continue
            enabled, volume = res
            # print(f"Polled addr {addr}: enabled={enabled}, volume={volume}")
        except Exception as exc:
            logging.warning("Unhandled exception while polling addr %s: %s", addr, exc)
            # keep previous values
            continue

        # update state but do not crash if lists are unexpectedly short
        if len(state.get("zones", [])) <= idx:
            # expand to fit
            while len(state.setdefault("zones", [])) <= idx:
                state["zones"].append(False)
        if len(state.get("volumes", [])) <= idx:
            while len(state["volumes"]) <= idx:
                state["volumes"].append(0)

        state["zones"][idx] = bool(enabled)
        state["volumes"][idx] = int(volume)


def monitor_rotary_encoder(stop_evt, pin_a: int, pin_b: int, btn_pin: int, on_button=None, on_rotate=None, on_long_press=None, poll_interval: float = 0.002, long_press_duration: float = 3.0):
    """Background thread: poll rotary encoder pins and print events.

    - Detects simple quadrature transitions and prints LEFT/RIGHT on rotation.
    - Detects button press (assumes active-low with pull-up) and prints when pressed.

    This uses GPIO.input which in the development environment may be the
    NoOpGPIO and will simply return False (no events).
    """
    # Implement the common CLK/DT algorithm (user example):
    # - pin_a: CLK
    # - pin_b: DT
    # - btn_pin: switch (active-low expected)

    try:
        last_clk = GPIO.input(pin_a)
    except Exception:
        last_clk = 0

    try:
        sw_prev = GPIO.input(btn_pin)
    except Exception:
        sw_prev = 1

    while not stop_evt.is_set():
        try:
            current_clk = GPIO.input(pin_a)
            current_dt = GPIO.input(pin_b)

            # Only act on the rising edge of the CLK signal. Acting on both
            # edges can produce duplicate/contradictory events (LEFT and RIGHT)
            # for a single step when the encoder transitions through two edges.
            # Using the rising-edge reduces duplicate prints and makes the
            # direction detection stable.
            if last_clk == 0 and current_clk == 1:
                # At the rising edge of CLK, the state of DT determines
                # direction. The exact mapping may vary with wiring; this
                # mapping preserves the previous behavior but restricts it to
                # a single edge.
                if current_dt != current_clk:
                    # RIGHT
                    if on_rotate:
                        try:
                            print("Rotary encoder rotated RIGHT")
                            on_rotate("RIGHT")
                        except Exception:
                            logging.exception("on_rotate callback raised an exception")
                    else:
                        print(f"Rotary encoder rotated RIGHT")
                    # print(f"Rotary encoder rotated RIGHT")
                else:
                    # LEFT
                    if on_rotate:
                        try:
                            print("Rotary encoder rotated LEFT")
                            on_rotate("LEFT")
                        except Exception:
                            logging.exception("on_rotate callback raised an exception")
                    else:
                        print(f"Rotary encoder rotated LEFT")
                    # print(f"Rotary encoder rotated LEFT")
                # very small debounce
                time.sleep(0.001)

            last_clk = current_clk

            # Button detection (active-low) with long-press handling
            try:
                sw = GPIO.input(btn_pin)
            except Exception:
                sw = 1

            if sw != sw_prev:
                # Button pressed (active-low)
                if sw == 0:
                    # basic debounce
                    time.sleep(0.03)
                    try:
                        if GPIO.input(btn_pin) != 0:
                            # bounce, ignore
                            sw_prev = GPIO.input(btn_pin)
                            continue
                    except Exception:
                        pass

                    press_start = time.time()
                    long_triggered = False
                    # Wait while button remains pressed, check for long-press
                    while True:
                        try:
                            currently = GPIO.input(btn_pin)
                        except Exception:
                            currently = 1
                        # If released, break out
                        if currently != 0:
                            break

                        # If we've exceeded the long-press duration, trigger once
                        if not long_triggered and (time.time() - press_start) >= float(long_press_duration):
                            if on_long_press:
                                try:
                                    on_long_press()
                                except Exception:
                                    logging.exception("on_long_press callback raised an exception")
                            else:
                                # no callback provided; print a notice
                                print("Long press detected")
                            long_triggered = True

                        # Sleep briefly and allow stop event to interrupt
                        if stop_evt.wait(0.05):
                            break

                    # If button was released before long-press threshold, treat as normal press
                    if not long_triggered:
                        if on_button:
                            try:
                                on_button()
                            except Exception:
                                logging.exception("on_button callback raised an exception")
                        else:
                            print("Rotary encoder pressed")

                sw_prev = sw
        except Exception:
            # Safe fallback when GPIO isn't the real module
            pass

        stop_evt.wait(poll_interval)



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
        print('Setting volume to', percent)
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


class PlayerWorker:
    """Background worker that watches `player_container['target_url']`.

    The worker monitors the shared `player_container` dict for a key
    `'target_url'`. When the target changes it will stop the current stream
    and start the new one. This keeps connect/disconnect work off the UI
    thread so rotate/click handlers remain responsive.
    """
    def __init__(self, player: I2SPlayer, player_container: dict):
        self.player = player
        self.player_container = player_container
        self._stop = threading.Event()
        self._vol_q = queue.Queue()
        self._current_url = None
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            try:
                target = self.player_container.get('target_url')
                if target != self._current_url:
                    # Stop existing stream if any
                    if self._current_url is not None:
                        try:
                            self.player.stop()
                        except Exception:
                            logging.exception("Error stopping player while switching stream")
                    self._current_url = target
                    # Start new stream if requested
                    if target:
                        try:
                            self.player.play(target)
                        except Exception:
                            logging.exception("Failed to start playback for %s", target)

                time.sleep(0.1)
            except Exception:
                logging.exception("PlayerWorker main loop error")

    def set_target(self, url: str):
        self.player_container['target_url'] = url

    def stop_target(self):
        self.player_container['target_url'] = None

    def shutdown(self, wait: float = 1.0):
        try:
            self._stop.set()
            try:
                # ensure the player is stopped
                self.player.stop()
            except Exception:
                pass
            self._thread.join(timeout=wait)
        except Exception:
            pass



def zone_poller(stop_evt, state, state_lock, render_queue, interval: float = 0.050):
    """
    Background thread function: poll zones periodically until stop_evt is set.

    This is the module-level equivalent of the former nested function. It
    accepts explicit references to shared objects rather than closing over
    them from an outer scope.
    """
    while not stop_evt.is_set():
        try:
            with state_lock:
                previous_state = copy.deepcopy(state)
                poll_zones(state)
                # If the state has changed, enqueue a snapshot for the render thread
                if state != previous_state:
                    # make a shallow copy (deep enough for our small state) to render
                    snapshot = copy.deepcopy(state)
                    # mark that the state changed so backlight idle timer resets
                    try:
                        mark_state_changed()
                    except Exception:
                        logging.exception("zone_poller: mark_state_changed failed")
                    try:
                        # If queue is full (previous render pending), replace it with newest state
                        render_queue.put_nowait(snapshot)
                    except Exception:
                        try:
                            # remove older item and put the latest snapshot
                            _ = render_queue.get_nowait()
                        except Exception:
                            pass
                        try:
                            render_queue.put_nowait(snapshot)
                        except Exception:
                            # if still failing, skip this render
                            logging.warning("Render queue full; skipping render request")
        except Exception:
            logging.exception("poll_zones encountered an error in background thread")
        # wait with timeout, exits sooner if stop event is set
        stop_evt.wait(interval)


def render_worker(stop_evt, render_queue, state, state_lock):
    """Background render thread: consumes state snapshots and calls render_display.
    """
    while not stop_evt.is_set():
        try:
            try:
                snapshot = render_queue.get(timeout=0.1)
            except Exception:
                # timed out waiting for a render request; loop and check stop_evt
                continue

            try:
                render_display(snapshot)
            except Exception:
                logging.exception("render_worker: render_display failed")
            finally:
                try:
                    render_queue.task_done()
                except Exception:
                    pass
        except Exception:
            logging.exception("Unexpected error in render_worker loop")


def cycle_input(state, state_lock, player_container):
    """Cycle input source when encoder button pressed.

    player_container is a dict-like object with key 'player' which may be
    None if the player has not been initialized yet.
    """
    choices = ["Internet Radio", "CD1", "CD2", "CD3"]
    with state_lock:
        try:
            idx = choices.index(state.get("input_source", "Internet Radio"))
        except ValueError:
            idx = 0
        new = choices[(idx + 1) % len(choices)]
        state["input_source"] = new
        if new == "Internet Radio":
            # ensure a station is selected when switching to Internet Radio
            if not state.get("radio_station_selected"):
                if RADIO_STATIONS:
                    station_names = list(RADIO_STATIONS.keys())
                    state["radio_station_selected"] = True
                    state["station_name"] = station_names[0]
                else:
                    state["radio_station_selected"] = False
                    state["station_name"] = None
        else:
            # Clear any radio selection when switching away
            state["radio_station_selected"] = False
            state["station_name"] = None

        # Update selector hardware pins to reflect the newly selected input.
        try:
            set_input_select_pins(new)
        except Exception:
            logging.exception("Failed to update input selector pins after switching to %s", new)

        # immediate feedback on display
        try:
            render_display(state)
        except Exception:
            logging.exception("Failed to render display after cycling input")

        # record state change (reset backlight idle timer)
        try:
            mark_state_changed()
        except Exception:
            logging.exception("cycle_input: mark_state_changed failed")

        # If we've switched away from Internet Radio, stop playback (if available).
        if new != "Internet Radio":
            try:
                worker = player_container.get('worker')
                if worker:
                    worker.stop_target()
                else:
                    player = player_container.get('player')
                    if player:
                        try:
                            player.stop()
                        except Exception:
                            logging.exception("Failed to stop player when switching input")
            except Exception:
                pass

        # If we've switched to Internet Radio, start playback for the selected station (if any)
        if new == "Internet Radio" and state.get("radio_station_selected") and state.get("station_name"):
            try:
                url = RADIO_STATIONS.get(state.get("station_name"))
                worker = player_container.get('worker')
                if worker and url:
                    worker.set_target(url)
                else:
                    # fall back to direct call if worker not present
                    player = player_container.get('player')
                    if player and url:
                        try:
                            player.play(url)
                        except Exception:
                            logging.exception("Failed to start playback for station %s", state.get("station_name"))
            except Exception:
                # player may not be initialized yet; that's fine
                pass

    print(f"Input switched to {state['input_source']}")


def rotate_handler(direction: str, state=None, state_lock=None, player_container=None):
    """Handle encoder rotation to change radio station.

    Accepts the same parameters as cycle_input so it can operate without
    closing over main's locals.
    """
    if state is None or state_lock is None:
        return
    # direction is expected to be "LEFT" or "RIGHT"
    with state_lock:
        if state.get("input_source") != "Internet Radio":
            return
        if not RADIO_STATIONS:
            return
        station_names = list(RADIO_STATIONS.keys())
        # Determine current index
        if state.get("radio_station_selected") and state.get("station_name") in station_names:
            idx = station_names.index(state.get("station_name"))
        else:
            idx = 0
        if direction == "RIGHT":
            idx = (idx + 1) % len(station_names)
        else:
            idx = (idx - 1) % len(station_names)
        state["radio_station_selected"] = True
        state["station_name"] = station_names[idx]
        try:
            render_display(state)
        except Exception:
            logging.exception("Failed to render display after station change")

        # record state change (reset backlight idle timer)
        try:
            mark_state_changed()
        except Exception:
            logging.exception("rotate_handler: mark_state_changed failed")

    # Attempt to start playback for the newly selected station. Be defensive if player
    # isn't initialized yet or playback fails. Use worker when available to avoid UI blocking.
    try:
        station = state.get('station_name')
        if station:
            url = RADIO_STATIONS.get(station)
            worker = None
            try:
                worker = player_container.get('worker') if player_container is not None else None
            except Exception:
                pass
            if worker and url:
                worker.set_target(url)
            else:
                player = None
                try:
                    player = player_container.get('player') if player_container is not None else None
                except Exception:
                    pass
                if url and player:
                    try:
                        player.play(url)
                    except Exception:
                        logging.exception("Failed to play station %s", station)
                else:
                    logging.warning("No URL configured for station '%s' or player not ready", station)
    except Exception:
        logging.exception("Error handling station change")

    print(f"Radio station: {state.get('station_name')}")


def shutdown(reason: str, stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread=None):
    """Unified shutdown/cleanup function used by signal handler and exception paths.

    Accepts explicit references to threads, player container and device so it can be
    called from the module level without closing over main's locals.
    """
    # We intentionally keep an idempotent shutdown here by setting an attribute
    # on the function object. This is simpler than nonlocal state when the
    # function is not nested.
    if getattr(shutdown, "_called", False):
        return
    setattr(shutdown, "_called", True)

    # Inform user and log
    print(reason)
    logging.info("Shutdown: %s", reason)

    # Stop playback if running
    try:
        worker = None
        try:
            worker = player_container.get('worker')
        except Exception:
            pass
        if worker:
            try:
                worker.shutdown()
            except Exception:
                logging.exception("Error while shutting down player worker during shutdown")
        else:
            player = None
            try:
                player = player_container.get('player')
            except Exception:
                pass
            if player:
                try:
                    player.stop()
                except Exception:
                    logging.exception("Error while stopping player during shutdown")
    except Exception:
        logging.exception("Error checking/stopping player during shutdown")

    # Signal background threads to stop
    try:
        stop_event.set()
    except Exception:
        pass

    # Give threads a moment to exit
    try:
        poller_thread.join(timeout=1.0)
    except Exception:
        pass
    try:
        encoder_thread.join(timeout=1.0)
    except Exception:
        pass
    try:
        if render_thread is not None:
            render_thread.join(timeout=1.0)
    except Exception:
        pass

    # Join backlight watcher thread if provided
    try:
        if backlight_thread is not None:
            try:
                backlight_thread.join(timeout=1.0)
            except Exception:
                pass
    except Exception:
        pass

    # Cleanup GPIO (no-op on fallback)
    try:
        GPIO.cleanup()
    except Exception:
        pass

    # Attempt to close/cleanup display device if supported (prefer close() to avoid GPIO mode errors)
    # try:
    #     close_fn = getattr(device, "close", None)
    #     if callable(close_fn):
    #         try:
    #             close_fn()
    #         except Exception:
    #             logging.exception("Error while closing display device")
    #     else:
    #         cleanup_fn = getattr(device, "cleanup", None)
    #         if callable(cleanup_fn):
    #             try:
    #                 cleanup_fn()
    #             except Exception:
    #                 logging.exception("Error while cleaning up display device")
    # except Exception:
    #     pass



def main():
    # initial state
    state = {
        "input_source": "Internet Radio",
        "radio_station_selected": True,
            "station_name": (list(RADIO_STATIONS.keys())[1] if RADIO_STATIONS else None),
        # zones and volumes will be populated from I2C; provide sensible defaults
        # "zones": [False, False, False],
        # "volumes": [0, 0, 0],
        "zones": [False, False, False],
        "volumes": [0, 0, 0],
    }

    # try a first poll to populate zone states from controllers
    try:
        poll_zones(state)
    except Exception:
        logging.exception("Initial poll_zones failed")

    # start a background poller that continuously updates zone state
    stop_event = threading.Event()
    # Lock to protect shared `state` across threads (poller, encoder callback, main)
    state_lock = threading.Lock()

    # Queue for render requests (keep only the latest pending render)
    render_queue = queue.Queue(maxsize=1)

    # Start render worker thread which will perform display draws
    render_thread = threading.Thread(
        target=render_worker, args=(stop_event, render_queue, state, state_lock), daemon=True
    )
    render_thread.start()

    # Start background poller thread (module-level function receives explicit refs)
    poller_thread = threading.Thread(
        target=zone_poller, args=(stop_event, state, state_lock, render_queue), daemon=True
    )
    poller_thread.start()

    # initial render: request the render worker to draw the initial state
    try:
        render_queue.put_nowait(copy.deepcopy(state))
    except Exception:
        # if queue is unexpectedly full, ignore; render_worker will draw soon
        pass

    # Ensure GPIO pin 16 is driven to ground when the main loop starts.
    # Use BCM numbering (consistent with other GPIO uses like 23/24 above).
    try:
        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)
        # Setup rotary encoder pins (BCM numbering). Using CLK/DT convention.
        ENCODER_PIN_A = 27  # CLK
        ENCODER_PIN_B = 17  # DT
        ENCODER_BTN = 22    # SW (button)

        try:
            # Use pull-down for CLK/DT, pull-up for switch (active-low)
            GPIO.setup(ENCODER_PIN_A, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)
            GPIO.setup(ENCODER_PIN_B, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)
            GPIO.setup(ENCODER_BTN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
            GPIO.setup(INPUT_SELECT_A, GPIO.OUT, initial=GPIO.LOW)
            GPIO.setup(INPUT_SELECT_B, GPIO.OUT, initial=GPIO.LOW)
            GPIO.setup(BACKLIGHT_PIN, GPIO.OUT, initial=GPIO.LOW)
            # Ensure hardware matches the logical initial input
            try:
                set_input_select_pins(state.get('input_source', 'Internet Radio'))
            except Exception:
                logging.exception("Failed to initialize input select pins to initial state")
        except TypeError:
            # Some GPIO fallbacks may not accept keyword args; try without them
            try:
                GPIO.setup(ENCODER_PIN_A, GPIO.IN)
                GPIO.setup(ENCODER_PIN_B, GPIO.IN)
                GPIO.setup(ENCODER_BTN, GPIO.IN)
            except Exception:
                # ignore and continue; monitor thread will handle missing input
                pass
    except Exception:
        logging.exception("Failed to configure GPIO pin 16; continuing without GPIO control")

    # Ensure backlight is initially on and start backlight watcher thread
    try:
        set_backlight(True)
    except Exception:
        logging.exception("Failed to set initial backlight state")

    backlight_thread = threading.Thread(target=backlight_watcher, args=(stop_event, 60.0), daemon=True)
    backlight_thread.start()

    # Encoder callbacks use module-level functions; player_container will hold the player
    # reference and is populated after player creation below.
    player_container = { 'player': None }

    STREAM_URL = RADIO_STATIONS.get(state.get("station_name")) if state.get("radio_station_selected") else None
    ALSA_DEVICE = "default"
    VOLUME_PERCENT = 50

    player = I2SPlayer(alsa_device=ALSA_DEVICE)  # change alsa_device if your I2S DAC is on another card

    # Expose player via the shared container so module-level callbacks can access it
    player_container['player'] = player

    # Create a PlayerWorker to watch `player_container['target_url']` and handle
    # connect/disconnect in the background so UI callbacks are non-blocking.
    try:
        worker = PlayerWorker(player, player_container)
        player_container['worker'] = worker
    except Exception:
        logging.exception("Failed to create PlayerWorker; falling back to direct calls")

    # Set the initial target URL; the worker will pick this up and start playback.
    player_container['target_url'] = STREAM_URL

    # Start rotary encoder monitor thread (uses same stop_event)
    # Long-press handler: perform local cleanup then request OS shutdown
    def _handle_long_press():
        try:
            # perform application-level cleanup
            shutdown("Long press: initiating shutdown", stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread)
        except Exception:
            logging.exception("Error during graceful shutdown on long-press")
        try:
            # request system halt
            print("Executing system shutdown command")
            subprocess.run(["shutdown", "-h", "now"])
            print("Shutdown command executed")
        except Exception:
            logging.exception("Failed to execute system shutdown command on long-press")

    encoder_thread = threading.Thread(
        target=monitor_rotary_encoder,
        args=(
            stop_event,
            ENCODER_PIN_B,
            ENCODER_PIN_A,
            ENCODER_BTN,
            # on_button and on_rotate: call module-level handlers with explicit refs
            lambda: cycle_input(state, state_lock, player_container),
            lambda d: rotate_handler(d, state, state_lock, player_container),
            _handle_long_press,
        ),
        daemon=True,
    )
    encoder_thread.start()

    # Register OS signal handlers to call the module-level shutdown function.
    signal.signal(signal.SIGINT, lambda s, f: shutdown(f"Signal {s} received - initiating shutdown", stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread))
    signal.signal(signal.SIGTERM, lambda s, f: shutdown(f"Signal {s} received - initiating shutdown", stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread))

    print("Starting playback:", STREAM_URL)
    player.set_volume(VOLUME_PERCENT)

    # Main thread: wait until stop_event is set by signal or by calling shutdown.
    try:
        while not stop_event.is_set():
            # wait with timeout so we can respond to other events and exit promptly
            stop_event.wait(timeout=1.0)
    except KeyboardInterrupt:
        # Fallback in case Ctrl+C arrives before our signal handler was registered
        shutdown("Keyboard interrupt received", stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread)
    except Exception:
        logging.exception("Unexpected error in main loop")
        shutdown("Unexpected error in main loop", stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread)

    # Ensure we perform final cleanup before exiting
    shutdown("Exiting main", stop_event, poller_thread, encoder_thread, player_container, render_thread, device, backlight_thread)


if __name__ == "__main__":
    main()

