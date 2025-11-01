#!/usr/bin/env python3
"""
run_and_watch.py

Run a Python script and restart it when the file is changed (or when it crashes).

Usage:
  python3 run_and_watch.py [path/to/script.py] [--interval 1.0]

This script uses only the Python standard library so no extra packages are required on the RPI.
"""
from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import time
from typing import Optional


def now() -> str:
    return time.strftime("%Y-%m-%d %H:%M:%S")


class Runner:
    def __init__(self, target: str, interval: float = 1.0) -> None:
        self.target = os.path.abspath(target)
        self.interval = interval
        self.proc: Optional[subprocess.Popen] = None
        self._stop = False

        if not os.path.exists(self.target):
            raise FileNotFoundError(f"Target script not found: {self.target}")

        # working directory for the child process: directory containing the script
        self.cwd = os.path.dirname(self.target) or os.getcwd()

    def _get_mtime(self) -> float:
        try:
            return os.path.getmtime(self.target)
        except OSError:
            return 0.0

    def start_child(self) -> None:
        print(f"{now()} Starting child: {self.target}")
        # Use the same python executable
        self.proc = subprocess.Popen([sys.executable, self.target], cwd=self.cwd)

    def stop_child(self, timeout: float = 5.0) -> None:
        if not self.proc:
            return
        if self.proc.poll() is not None:
            # already exited
            self.proc = None
            return

        print(f"{now()} Stopping child (graceful)")
        try:
            self.proc.send_signal(signal.SIGINT)
        except Exception:
            try:
                self.proc.terminate()
            except Exception:
                pass

        start = time.time()
        while time.time() - start < timeout:
            if self.proc.poll() is not None:
                break
            time.sleep(0.1)

        if self.proc.poll() is None:
            print(f"{now()} Child did not exit, killing")
            try:
                self.proc.kill()
            except Exception:
                pass

        # wait a bit to reap
        try:
            self.proc.wait(timeout=1)
        except Exception:
            pass
        self.proc = None

    def run(self) -> None:
        last_mtime = self._get_mtime()
        self.start_child()

        try:
            while not self._stop:
                # If child died for any reason, restart it
                if self.proc and self.proc.poll() is not None:
                    rc = self.proc.returncode
                    print(f"{now()} Child exited with code {rc}. Restarting...")
                    self.proc = None
                    self.start_child()

                time.sleep(self.interval)

                new_mtime = self._get_mtime()
                if new_mtime != last_mtime:
                    print(f"{now()} Detected change in {self.target} (mtime {new_mtime}). Restarting child.")
                    last_mtime = new_mtime
                    self.stop_child()
                    # tiny delay to allow editors/transfer to finish writing
                    time.sleep(0.2)
                    self.start_child()

        except KeyboardInterrupt:
            print(f"{now()} Received KeyboardInterrupt, shutting down")
            self._stop = True

        finally:
            self.stop_child()

    def shutdown(self) -> None:
        self._stop = True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run a Python script and restart it when the file changes")
    parser.add_argument("script", nargs="?", default="main.py", help="path to the Python script to run (default: main.py)")
    parser.add_argument("--interval", type=float, default=1.0, help="poll interval in seconds (default: 1.0)")
    args = parser.parse_args(argv)

    runner = Runner(args.script, args.interval)

    # forward termination signals to shutdown cleanly
    def _handle(signum, frame):
        print(f"{now()} Received signal {signum}, shutting down")
        runner.shutdown()

    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    try:
        runner.run()
        return 0
    except Exception as exc:
        print(f"Error: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
