#!/usr/bin/env python3
"""Poll pim-32's virtual mt32pi-status.txt file and write a CSV time series.

The status file is served by the embedded FTP server, so no serial console is
needed. With an INA219 fitted and power_monitor=on, bus_mv/current_ma/power_mw
are real measurements; otherwise they are zero and the remaining columns are
software proxies (clock, temperature, throttle bits, audio activity).

Example:
    python scripts/dev/power-log.py 192.168.1.50 --password secret --out power.csv
"""

from __future__ import annotations

import argparse
import csv
import ftplib
import os
import sys
import time

FIELDS = (
    "version",
    "wfe",
    "plld_hz",
    "audio_started",
    "clock_hz",
    "temperature_c",
    "throttled",
    "power_saving",
    "audio_active",
    "audio_queue_percent",
    "audio_renders",
    "audio_sleeps",
    "audio_dropped",
    "midi_messages",
    "ina219",
    "bus_mv",
    "current_ma",
    "power_mw",
)


def read_status(ftp: ftplib.FTP, path: str) -> dict[str, str]:
    lines: list[str] = []
    ftp.retrlines(f"RETR {path}", lines.append)
    status: dict[str, str] = {}
    for line in lines:
        if "=" in line:
            key, value = line.split("=", 1)
            status[key.strip()] = value.strip()
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="Pi host name or address")
    parser.add_argument("--port", type=int, default=21)
    parser.add_argument("--user", default="mt32-pi")
    parser.add_argument(
        "--password",
        default=os.environ.get("MT32PI_FTP_PASSWORD", "mt32-pi"),
        help="FTP password (or set MT32PI_FTP_PASSWORD)",
    )
    parser.add_argument("--path", default="mt32pi-status.txt")
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument(
        "--count", type=int, default=0, help="number of samples (0 = until Ctrl-C)"
    )
    parser.add_argument("--out", help="CSV output path (default: stdout)")
    args = parser.parse_args()

    output = (
        open(args.out, "w", newline="", encoding="utf-8") if args.out else sys.stdout
    )
    writer = csv.DictWriter(
        output, fieldnames=("timestamp",) + FIELDS, extrasaction="ignore"
    )
    writer.writeheader()

    ftp = ftplib.FTP()
    ftp.connect(args.host, args.port, timeout=10)
    ftp.login(args.user, args.password)

    samples = 0
    try:
        while args.count == 0 or samples < args.count:
            try:
                status = read_status(ftp, args.path)
            except (ftplib.Error, OSError) as error:
                print(f"status read failed: {error}", file=sys.stderr)
                ftp.close()
                time.sleep(args.interval)
                ftp = ftplib.FTP()
                ftp.connect(args.host, args.port, timeout=10)
                ftp.login(args.user, args.password)
                continue

            row = {"timestamp": time.strftime("%Y-%m-%dT%H:%M:%S")}
            row.update(status)
            writer.writerow(row)
            output.flush()
            samples += 1
            if args.count == 0 or samples < args.count:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        ftp.close()
        if args.out:
            output.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
