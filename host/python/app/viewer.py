#!/usr/bin/env python3
"""
viewer.py — UART CSV live viewer for espnow-uart-bridge

Reads CSV lines coming from *child_uart_bridge* (over USB serial),
tracks HDR (schema) automatically, logs to a CSV file, and plots
selected fields in real-time.

Usage examples:
  python viewer.py --port COM7 --baud 115200 --plot ax,ay,az --save out.csv
  python viewer.py --port /dev/ttyUSB0 --plot ax,ay,az,dt_ms --window 20

Requirements:
  pip install pyserial matplotlib

Notes:
  - Expects lines like:
      HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50
      DAT,<src_seq>,<t_ms>,<values...>
  - If fields length and values length mismatch, extra values are ignored,
    missing values are filled with NaN.
"""

from __future__ import annotations

import argparse
import csv
import sys
import threading
import time
from collections import deque, defaultdict
from dataclasses import dataclass
from queue import Queue, Empty
from typing import List, Dict, Optional, Tuple
from datetime import datetime
from pathlib import Path

try:
    import serial  # pyserial
    from serial.tools import list_ports
except Exception as e:
    print("pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    raise

import matplotlib
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import math


@dataclass
class DataPoint:
    """One parsed DAT row."""
    t_ms: float        # sender clock in milliseconds (from CSV)
    fields: Dict[str, float]  # mapped numeric values (name -> value)
    src_seq: int


class SerialReader(threading.Thread):
    """Reads lines from a serial port and pushes parsed data into a queue."""

    def __init__(
        self,
        port: str,
        baud: int,
        out_queue: Queue,
        save_csv: Optional[str] = None,
        reconnect: bool = True,
        log_prefix: str = "[READER] ",
    ) -> None:
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.out_queue = out_queue
        self.reconnect = reconnect
        self.log_prefix = log_prefix
        self._stop = threading.Event()
        self._ser: Optional[serial.Serial] = None
        self._csv_writer = None
        self._csv_file = None
        self._csv_header_written = False
        self._fields: List[str] = []  # schema learned from HDR

        if save_csv:
            self._csv_file = open(save_csv, "a", newline="", encoding="utf-8")
            self._csv_writer = csv.writer(self._csv_file)
            print(f"{self.log_prefix}Logging CSV to: {save_csv}")

    def stop(self):
        self._stop.set()
        try:
            if self._ser and self._ser.is_open:
                self._ser.close()
        except Exception:
            pass
        if self._csv_file:
            try:
                self._csv_file.flush()
                self._csv_file.close()
            except Exception:
                pass

    def _open_serial(self) -> serial.Serial:
        while not self._stop.is_set():
            try:
                ser = serial.Serial(self.port, self.baud, timeout=1)
                print(f"{self.log_prefix}Opened {self.port} @ {self.baud}")
                return ser
            except Exception as e:
                print(f"{self.log_prefix}Open failed: {e}")
                if not self.reconnect:
                    raise
                try:
                    ports = [p.device for p in list_ports.comports()]
                    if ports:
                        print(f"{self.log_prefix}Available ports: {ports}")
                except Exception:
                    pass
                time.sleep(2)
        raise RuntimeError("stopped")

    def _handle_hdr(self, line: str):
        # Example: HDR,1,GLDR,fields=dt_ms,ax,ay,az,...,rate=50
        idx = line.find("fields=")
        if idx < 0:
            return
        payload = line[idx + len("fields="):].strip()
        # cut off trailing ",rate=..." if present
        rate_idx = payload.find("rate=")
        if rate_idx > 0:
            comma_before = payload.rfind(",", 0, rate_idx)
            if comma_before >= 0:
                payload = payload[:comma_before]
        fields = [s.strip() for s in payload.split(",") if s.strip()]
        if fields:
            self._fields = fields
            self._csv_header_written = False  # force rewrite header on next DAT
            print(f"{self.log_prefix}HDR fields = {self._fields}")

    def _write_csv_header_if_needed(self):
        if self._csv_writer and not self._csv_header_written and self._fields:
            header = ["src_seq", "t_ms"] + self._fields
            self._csv_writer.writerow(header)
            self._csv_file.flush()
            self._csv_header_written = True

    def _parse_dat(self, line: str) -> Optional[DataPoint]:
        # DAT,<src_seq>,<t_ms>,<values...>
        try:
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 3:
                return None
            if parts[0] != "DAT":
                return None
            src_seq = int(float(parts[1]))
            t_ms = float(parts[2])
            values = [float(x) if x not in ("", None) else math.nan for x in parts[3:]]
            mapping = {}
            if self._fields:
                n = min(len(self._fields), len(values))
                for i in range(n):
                    mapping[self._fields[i]] = values[i]
                for i in range(n, len(self._fields)):
                    mapping[self._fields[i]] = math.nan
            else:
                for i, v in enumerate(values):
                    mapping[f"f{i}"] = v
            return DataPoint(t_ms=t_ms, fields=mapping, src_seq=src_seq)
        except Exception:
            return None

    def run(self):
        while not self._stop.is_set():
            try:
                if self._ser is None or not self._ser.is_open:
                    self._ser = self._open_serial()

                raw = self._ser.readline()
                if not raw:
                    continue
                try:
                    line = raw.decode("utf-8", errors="ignore").strip()
                except Exception:
                    continue
                if not line:
                    continue

                if line.startswith("HDR,"):
                    self._handle_hdr(line)
                    self.out_queue.put(("HDR", line))
                    continue

                if line.startswith("DAT,"):
                    dp = self._parse_dat(line)
                    if dp is None:
                        continue
                    if self._csv_writer is not None:
                        self._write_csv_header_if_needed()
                        row = ["%d" % dp.src_seq, "%.3f" % dp.t_ms]
                        for name in (self._fields or sorted(dp.fields.keys())):
                            val = dp.fields.get(name, math.nan)
                            row.append(f"{val:.6f}" if isinstance(val, float) else str(val))
                        self._csv_writer.writerow(row)
                        self._csv_file.flush()
                    self.out_queue.put(("DAT", dp))
                    continue

                self.out_queue.put(("LOG", line))

            except serial.SerialException as e:
                print(f"{self.log_prefix}Serial error: {e}")
                if self._ser:
                    try:
                        self._ser.close()
                    except Exception:
                        pass
                    self._ser = None
                if not self.reconnect:
                    break
                time.sleep(2)
            except Exception as e:
                print(f"{self.log_prefix}Error: {e}")
                time.sleep(0.2)


def choose_auto_fields(all_names: List[str]) -> List[str]:
    order = ["ax","ay","az","gx","gy","gz","dt_ms"]
    chosen = [n for n in order if n in all_names]
    if not chosen:
        chosen = all_names[:3]
    return chosen[:6]  # cap number of plotted lines


def resolve_save_path(save_arg: str) -> str:
    """
    Resolve a user-provided --save argument into a unique, timestamped CSV path.
    Rules:
      - If name contains strftime tokens (e.g., %Y%m%d), expand them.
      - If path ends with a separator or points to a directory -> logs/log_YYYYmmdd-HHMMSS.csv
      - If file has an extension -> append _YYYYmmdd-HHMMSS before the extension
      - If no extension -> treat as stem and append _YYYYmmdd-HHMMSS.csv
      - If result exists, append _1, _2, ... to make it unique
    """
    raw = save_arg
    p = Path(raw).expanduser()
    now = datetime.now()

    # 1) strftime-style pattern in filename
    if "%" in p.name:
        p = p.with_name(now.strftime(p.name))
    # 2) directory-like (ends with slash/backslash OR existing dir)
    elif raw.endswith(("/", "\\")) or (p.exists() and p.is_dir()):
        p = p / f"log_{now.strftime('%Y%m%d-%H%M%S')}.csv"
    # 3) file with extension
    elif p.suffix:
        p = p.with_name(f"{p.stem}_{now.strftime('%Y%m%d-%H%M%S')}{p.suffix}")
    # 4) bare stem (no extension)
    else:
        p = p.with_name(f"{p.name}_{now.strftime('%Y%m%d-%H%M%S')}.csv")

    p.parent.mkdir(parents=True, exist_ok=True)

    # Ensure uniqueness if the path already exists
    q = p
    i = 1
    while q.exists():
        q = p.with_name(f"{p.stem}_{i}{p.suffix}")
        i += 1
    return str(q)


def run_viewer(args):
    q: Queue = Queue(maxsize=10000)

    reader = SerialReader(
        port=args.port,
        baud=args.baud,
        out_queue=q,
        save_csv=args.save,
        reconnect=not args.no_reconnect,
    )
    reader.start()

    buffer_sec = args.window
    max_points = args.max_points

    t0 = None
    current_fields: List[str] = []
    times = deque(maxlen=max_points)  # seconds since first sample
    series: Dict[str, deque] = defaultdict(lambda: deque(maxlen=max_points))

    plt.figure(figsize=(10, 5))
    ax = plt.gca()
    ax.set_title("espnow-uart-bridge live")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("value")
    ax.grid(True, linestyle="--", alpha=0.3)

    plot_lines: Dict[str, any] = {}
    plotted_fields: List[str] = []

    # auto-ylim state
    prev_ymin: Optional[float] = None
    prev_ymax: Optional[float] = None

    def ensure_lines(field_names: List[str]):
        nonlocal plot_lines, plotted_fields
        for name in field_names:
            if name not in plot_lines:
                (line_obj,) = ax.plot([], [], label=name)
                plot_lines[name] = line_obj
        for name in list(plot_lines.keys()):
            if name not in field_names:
                line_obj = plot_lines.pop(name)
                try:
                    line_obj.remove()
                except Exception:
                    pass
        plotted_fields = list(field_names)
        ax.legend(loc="upper left")

    target_fields = [s.strip() for s in (args.plot.split(",") if args.plot else []) if s.strip()]
    if target_fields:
        ensure_lines(target_fields)

    def update(_frame):
        nonlocal t0, current_fields, target_fields, prev_ymin, prev_ymax

        # consume queue
        while True:
            try:
                kind, payload = q.get_nowait()
            except Empty:
                break

            if kind == "HDR":
                line = payload
                idx = line.find("fields=")
                if idx >= 0:
                    payload_fields = line[idx + len("fields="):].strip()
                    rate_idx = payload_fields.find("rate=")
                    if rate_idx > 0:
                        comma_before = payload_fields.rfind(",", 0, rate_idx)
                        if comma_before >= 0:
                            payload_fields = payload_fields[:comma_before]
                    names = [s.strip() for s in payload_fields.split(",") if s.strip()]
                    if names:
                        current_fields = names
                        if not target_fields:
                            target_fields = choose_auto_fields(current_fields)
                            ensure_lines(target_fields)
                continue

            if kind == "DAT":
                dp: DataPoint = payload
                if t0 is None:
                    t0 = dp.t_ms / 1000.0
                t = dp.t_ms / 1000.0 - t0
                times.append(t)
                for name, val in dp.fields.items():
                    series[name].append(val)

        if not times or not target_fields:
            return []

        # update each plotted line
        tmax = times[-1]
        tmin = max(0.0, tmax - buffer_sec)
        for name in target_fields:
            ys = series.get(name, [])
            if hasattr(ys, "__len__") and len(ys) > 0:
                xs = list(times)[-len(ys):]
                # window
                i0 = 0
                for i, tx in enumerate(xs):
                    if tx >= tmin:
                        i0 = i
                        break
                xs_win = xs[i0:]
                ys_win = list(ys)[-len(xs):][i0:]
                line = plot_lines.get(name)
                if line:
                    line.set_data(xs_win, ys_win)

        # --- Auto Y limits ---
        if args.ylim is not None:
            ymin, ymax = args.ylim
        else:
            ymins, ymaxs = [], []
            for name in target_fields:
                ys = series.get(name, [])
                if hasattr(ys, "__len__") and len(ys) > 0:
                    dat = [v for v in list(ys)[-max_points:] if isinstance(v, (int, float))]
                    if dat:
                        ymins.append(min(dat))
                        ymaxs.append(max(dat))
            if ymins and ymaxs:
                ymin = min(ymins); ymax = max(ymaxs)
                # include zero if requested
                if args.include_zero:
                    ymin = min(ymin, 0.0)
                    ymax = max(ymax, 0.0)
                # avoid zero range
                if ymin == ymax:
                    ymin -= 1.0; ymax += 1.0
                # add margin
                pad = (ymax - ymin) * args.ylim_margin
                ymin -= pad; ymax += pad
                # smoothing
                if prev_ymin is None:
                    prev_ymin, prev_ymax = ymin, ymax
                else:
                    alpha = args.ylim_smooth  # 0..1
                    prev_ymin = prev_ymin + alpha * (ymin - prev_ymin)
                    prev_ymax = prev_ymax + alpha * (ymax - prev_ymax)
                ymin, ymax = prev_ymin, prev_ymax
            else:
                ymin, ymax = ax.get_ylim()

        ax.set_xlim(max(0.0, tmax - buffer_sec), tmax + 0.05 * buffer_sec)
        ax.set_ylim(ymin, ymax)
        return list(plot_lines.values())

    ani = FuncAnimation(plt.gcf(), update, interval=max(50, int(1000 / max(10, args.refresh_hz))), blit=False)

    def on_key(event):
        nonlocal prev_ymin, prev_ymax
        if event.key == 'a':
            # toggle fixed/auto: if currently fixed via --ylim, clear it
            if args.ylim is None:
                # fix at current
                args.ylim = ax.get_ylim()
                print(f"[VIEW] fixed ylim = {args.ylim}")
            else:
                args.ylim = None
                prev_ymin = prev_ymax = None
                print("[VIEW] auto ylim enabled")
        elif event.key == '0':
            args.include_zero = not args.include_zero
            prev_ymin = prev_ymax = None
            print(f"[VIEW] include_zero = {args.include_zero}")

    plt.gcf().canvas.mpl_connect('key_press_event', on_key)

    try:
        print("[VIEW] Press Ctrl+C to exit. Keys: [a]=toggle auto/fixed ylim, [0]=toggle include zero")
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        reader.stop()
        time.sleep(0.2)


def parse_ylim(value: str) -> Optional[Tuple[float, float]]:
    try:
        parts = [p.strip() for p in value.split(",")]
        if len(parts) != 2:
            raise ValueError
        lo = float(parts[0]); hi = float(parts[1])
        return (lo, hi)
    except Exception:
        raise argparse.ArgumentTypeError("Use format: min,max (e.g., --ylim -20,20)")


def main():
    p = argparse.ArgumentParser(description="UART CSV live viewer for espnow-uart-bridge")
    p.add_argument("--port", required=True, help="Serial port (e.g., COM7 or /dev/ttyUSB0)")
    p.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    p.add_argument(
        "--save",
        type=str,
        default=None,
        help=(
            "Path to save CSV log. A timestamp suffix (_YYYYmmdd-HHMMSS) is added automatically "
            "to avoid overwriting. If the path ends with a separator or points to a directory, "
            "a file like log_YYYYmmdd-HHMMSS.csv is created there. You can also include strftime "
            "tokens in the filename (e.g., logs/%Y%m%d_run.csv)."
        ),
    )
    p.add_argument("--plot", type=str, default=None, help="Comma-separated field names to plot (e.g., ax,ay,az). If omitted, picks automatically when HDR arrives.")
    p.add_argument("--window", type=float, default=15.0, help="Plot time window in seconds (default: 15)")
    p.add_argument("--max-points", type=int, default=5000, help="Max points kept in memory per series (default: 5000)")
    p.add_argument("--refresh-hz", type=float, default=15.0, help="UI refresh rate (default: 15 Hz)")
    p.add_argument("--no-reconnect", action="store_true", help="Disable auto reconnect on serial errors")

    # --- new auto-ylim controls ---
    p.add_argument("--ylim", type=parse_ylim, default=None, help="Fix Y limits as 'min,max'. If omitted, auto scale is used.")
    p.add_argument("--ylim-margin", type=float, default=0.05, help="Relative margin added to auto Y range (default: 0.05 = 5%)")
    p.add_argument("--ylim-smooth", type=float, default=0.25, help="Smoothing factor (0..1) for auto Y limits to avoid jitter (default: 0.25)")
    p.add_argument("--include-zero", action="store_true", help="Force Y range to include zero in auto mode")

    args = p.parse_args()
    # clamp smoothing
    if args.ylim_smooth < 0.0: args.ylim_smooth = 0.0
    if args.ylim_smooth > 1.0: args.ylim_smooth = 1.0

    # Resolve --save to a unique, timestamped path
    if args.save:
        args.save = resolve_save_path(args.save)

    run_viewer(args)


if __name__ == "__main__":
    main()
