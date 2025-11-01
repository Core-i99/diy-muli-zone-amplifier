
#!/usr/bin/env python3
"""
Simple I2C reader for the Zone Controller (ATtiny44).

This script reads the Zone struct the controller sends over I2C:
  struct Zone { int16_t volume; bool enabled; };

The controller is at I2C address 8 by default and sends the struct bytes
in little-endian order (int16_t => low byte first). We read 3 bytes and
print the enabled (on/off) state. Volume is returned too (for future use).

Requires: smbus2 (already listed in `Code_RPI/requirements.txt`).
"""

from __future__ import annotations

import argparse
import sys
from smbus2 import SMBus, i2c_msg
import time

DEFAULT_I2C_ADDR = 8
I2C_BUS = 1
NUM_BYTES = 3  # int16_t (2 bytes) + bool (1 byte)


def read_raw_after_trigger(bus_num: int = I2C_BUS, addr: int = DEFAULT_I2C_ADDR, num_bytes: int = NUM_BYTES, tries: int = 3, delay: float = 0.05):
	"""Try to trigger the device by sending an empty write (or a zero byte) and then read raw bytes.

	Returns a list of raw bytes. On failure raises the last exception encountered.
	"""
	last_exc = None
	for attempt in range(1, tries + 1):
		try:
			with SMBus(bus_num) as bus:
				# Try an empty write first (many devices accept a 0-length write)
				try:
					write_msg = i2c_msg.write(addr, [])
					bus.i2c_rdwr(write_msg)
				except Exception:
					# Fallback: write a single zero byte which may trigger the slave's receive handler
					try:
						bus.write_byte(addr, 0)
					except Exception:
						# ignore here and continue to the read attempt; we'll surface a read error if it fails
						pass

				# Short pause to let the slave process the request
				time.sleep(delay)

				# Now perform the read request which should trigger the slave to respond
				read_msg = i2c_msg.read(addr, num_bytes)
				bus.i2c_rdwr(read_msg)
				data = list(read_msg)

			# success
			return data
		except Exception as exc:
			last_exc = exc
			# small backoff before retrying
			time.sleep(delay * attempt)
	# if we get here, all attempts failed
	raise last_exc


def main(argv=None):
	parser = argparse.ArgumentParser(description="Read zone enabled state over I2C")
	parser.add_argument("--bus", "-b", type=int, default=I2C_BUS, help="I2C bus number (default: 1)")
	parser.add_argument("--addr", "-a", type=int, default=DEFAULT_I2C_ADDR, help="I2C address (default: 8)")
	parser.add_argument("--raw", action="store_true", help="Print raw bytes as well")
	args = parser.parse_args(argv)

	try:
		raw = read_raw_after_trigger(args.bus, args.addr, NUM_BYTES)
	except Exception as exc:
		print(f"Error reading I2C device at address {args.addr} on bus {args.bus}: {exc}", file=sys.stderr)
		return 2

	# Print raw bytes always when --raw is requested, otherwise parse as before
	if args.raw:
		print("raw bytes:", [int(x) for x in raw])
		return 0

	# Try parsing for convenience (fall back to raw print on parse errors)
	try:
		if len(raw) >= 3:
			volume = int(raw[0]) | (int(raw[1]) << 8)
			enabled = bool(raw[2])
			print(f"zone enabled: {enabled}")
			print(f"volume (raw int16): {volume}")
		else:
			print(f"Unexpected raw length: {len(raw)}; bytes: {raw}")
	except Exception:
		print("Could not parse bytes; raw:", raw)
	return 0
	return 0


if __name__ == "__main__":
	raise SystemExit(main())

