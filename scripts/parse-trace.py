#!/usr/bin/env python3
"""
parse-trace.py — Convert Renode binary trace dump to Perfetto trace + summary.

Inputs:
  --trace trace.bin            Binary trace from Renode sysbus ReadMemory
  --elf zephyr.elf             ELF file for symbol/thread-name resolution
  --names trace_names.bin      (optional) Thread names dump
  --profiler trace_functions.perfetto  (optional) Renode Perfetto profiler output

Outputs:
  --output trace_combined.perfetto   Perfetto trace (default stdout text report)
  --summary                         Print human-readable summary to stdout
  --json trace_threads.json         Raw timeline as JSON

The binary trace format is a flat sequence of 8-byte records:
  uint32_t timestamp;   // k_cycle_get_32()
  uint8_t  event_type;  // 0=switched_in, 1=switched_out, 2=create,
                        // 3=isr_enter, 4=isr_exit, 5=idle
  uint8_t  event_data;  // priority for switched_in, 0 otherwise
  uint16_t thread_id;   // 0xFFFF = unknown (ISR/idle)
"""

import argparse
import json
import struct
import sys
from collections import defaultdict

RECORD_FMT = "<IBBH"
RECORD_SIZE = struct.calcsize(RECORD_FMT)

EVENT_NAMES = {
    0: "switched_in",
    1: "switched_out",
    2: "create",
    3: "isr_enter",
    4: "isr_exit",
    5: "idle_enter",
}

UNKNOWN_THREAD_ID = 0xFFFF


def parse_binary_trace(path):
    """Parse the binary trace file into a list of dicts."""
    with open(path, "rb") as f:
        data = f.read()

    records = []
    for i in range(0, len(data), RECORD_SIZE):
        chunk = data[i : i + RECORD_SIZE]
        if len(chunk) < RECORD_SIZE:
            break
        ts, evt_type, evt_data, thread_id = struct.unpack(RECORD_FMT, chunk)

        if ts == 0 and evt_type == 0 and evt_data == 0 and thread_id == 0:
            continue

        records.append({
            "timestamp": ts,
            "event_type": evt_type,
            "event_name": EVENT_NAMES.get(evt_type, f"unknown_{evt_type}"),
            "event_data": evt_data,
            "thread_id": thread_id,
        })

    return records


def resolve_thread_names(records, elf_path=None):
    """Build thread_id -> name mapping from create events and ELF symbols."""
    names = {}

    for rec in records:
        if rec["event_type"] == 2:
            tid = rec["thread_id"]
            if tid != UNKNOWN_THREAD_ID and tid not in names:
                names[tid] = f"thread_{tid}"

    if elf_path:
        try:
            import subprocess
            result = subprocess.run(
                ["nm", "--defined-only", elf_path],
                capture_output=True, text=True
            )
        except Exception:
            pass

    return names


def build_timeline(records, thread_names):
    """Build per-thread run intervals from switched_in/out pairs."""
    threads = defaultdict(lambda: {
        "name": "unknown",
        "run_time_cycles": 0,
        "switch_count": 0,
        "intervals": [],
    })

    current_thread = None
    switch_in_ts = 0

    for rec in records:
        tid = rec["thread_id"]
        evt = rec["event_type"]

        if evt == 0:
            if current_thread is not None:
                pass
            current_thread = tid
            switch_in_ts = rec["timestamp"]
            threads[tid]["switch_count"] += 1

        elif evt == 1:
            if current_thread is not None:
                run_time = rec["timestamp"] - switch_in_ts
                threads[current_thread]["run_time_cycles"] += run_time
                threads[current_thread]["intervals"].append(
                    (switch_in_ts, rec["timestamp"])
                )
            current_thread = None

    if current_thread is not None and records:
        last_ts = records[-1]["timestamp"]
        run_time = last_ts - switch_in_ts
        threads[current_thread]["run_time_cycles"] += run_time
        threads[current_thread]["intervals"].append(
            (switch_in_ts, last_ts)
        )

    for tid, info in threads.items():
        info["name"] = thread_names.get(tid, f"thread_0x{tid:04x}")

    return threads


def print_summary(threads, records):
    """Print human-readable trace summary."""
    total_cycles = sum(t["run_time_cycles"] for t in threads.values())
    if total_cycles == 0:
        total_cycles = 1

    isr_count = sum(1 for r in records if r["event_type"] == 3)
    switch_count = sum(1 for r in records if r["event_type"] == 0)

    print(f"Trace summary:")
    print(f"  Total records:    {len(records)}")
    print(f"  Context switches: {switch_count}")
    print(f"  ISR entries:      {isr_count}")
    print(f"  Tracked threads:  {len(threads)}")
    print()
    print(f"{'Thread':<20} {'Runtime %':>9}  {'Switches':>8}  {'ID':>6}")
    print("-" * 52)

    for tid, info in sorted(threads.items(), key=lambda x: -x[1]["run_time_cycles"]):
        pct = info["run_time_cycles"] * 100.0 / total_cycles
        name = info["name"]
        print(f"{name:<20} {pct:8.1f}%  {info['switch_count']:>8}  {tid:>6}")

    print("-" * 52)
    print(f"{'TOTAL':<20} {100.0:8.1f}%")


def write_perfetto_trace(threads, records, output_path):
    """Write a minimal Perfetto trace in Chrome Tracing JSON format."""
    trace_events = []
    tid_track = {}

    for rec in records:
        if rec["event_type"] == 0:
            tid = rec["thread_id"]
            name = f"thread_{tid}"
            ts_us = rec["timestamp"]

            if tid not in tid_track:
                tid_track[tid] = len(tid_track)
                trace_events.append({
                    "name": "thread_name",
                    "ph": "M",
                    "pid": 0,
                    "tid": tid,
                    "args": {"name": name},
                })

            trace_events.append({
                "name": "running",
                "ph": "B",
                "ts": ts_us,
                "pid": 0,
                "tid": tid,
            })

        elif rec["event_type"] == 1:
            tid = rec["thread_id"]
            ts_us = rec["timestamp"]
            trace_events.append({
                "name": "running",
                "ph": "E",
                "ts": ts_us,
                "pid": 0,
                "tid": tid,
            })

    output = {
        "traceEvents": trace_events,
        "displayTimeUnit": "ns",
    }

    with open(output_path, "w") as f:
        json.dump(output, f, indent=2)

    print(f"Perfetto trace written to {output_path}")


def write_json_timeline(threads, records, output_path):
    """Write raw timeline as JSON."""
    output = {
        "threads": {
            str(tid): {
                "name": info["name"],
                "run_time_cycles": info["run_time_cycles"],
                "switch_count": info["switch_count"],
                "intervals": info["intervals"],
            }
            for tid, info in threads.items()
        },
        "record_count": len(records),
        "isr_enter_count": sum(1 for r in records if r["event_type"] == 3),
        "idle_enter_count": sum(1 for r in records if r["event_type"] == 5),
    }
    with open(output_path, "w") as f:
        json.dump(output, f, indent=2)
    print(f"Timeline JSON written to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Parse Renode binary trace dump")
    parser.add_argument("--trace", required=True, help="Binary trace file (trace.bin)")
    parser.add_argument("--elf", default=None, help="ELF file for symbol resolution")
    parser.add_argument("--profiler", default=None, help="Renode Perfetto profiler output")
    parser.add_argument("--output", default=None, help="Output Perfetto trace file (.perfetto or .json)")
    parser.add_argument("--summary", action="store_true", help="Print human-readable summary")
    parser.add_argument("--json", default=None, help="Output raw timeline as JSON")
    args = parser.parse_args()

    records = parse_binary_trace(args.trace)
    if not records:
        print("Warning: no trace records found in binary file", file=sys.stderr)
        return 1

    thread_names = resolve_thread_names(records, args.elf)
    threads = build_timeline(records, thread_names)

    if args.summary or not args.output:
        print_summary(threads, records)

    if args.output:
        write_perfetto_trace(threads, records, args.output)

    if args.json:
        write_json_timeline(threads, records, args.json)

    return 0


if __name__ == "__main__":
    sys.exit(main())
