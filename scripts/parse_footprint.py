#!/usr/bin/env python3
"""Parse Zephyr rom_report/ram_report tree output into github-action-benchmark JSON.

Emits module-level summaries suitable for trend tracking:
  - Total ROM/RAM
  - Top-level partitions: WORKSPACE, ZEPHYR_BASE, (hidden), (no paths), OUTPUT_DIR
  - Weather-station libraries (clock_display, config_cmd, fake_sensors, etc.)
  - Zephyr major components (kernel, drivers, subsys, lib, soc, etc.)
  - Thread stack sizes (RAM only)
"""

import argparse
import json
import logging
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

logger = logging.getLogger("parse_footprint")

# Matches tree lines like:
# "│   ├── nrfx_gpiote.c                       132   2.85%  -"
# "└── ZEPHYR_BASE                                               13558  48.72%  -"
TREE_LINE_RE = re.compile(
    r"^(?P<indent>(?:[│ ]{4})*(?:[├└]── |[├└]─))"
    r"(?P<name>\S+)"
    r"\s+(?P<size>\d+)"
    r"\s+(?P<pct>[\d.]+)%"
)

# Matches the Root/Total line
ROOT_LINE_RE = re.compile(r"^Root\s+(?P<size>\d+)\s+[\d.]+%")

# Separator line
SEPARATOR_RE = re.compile(r"^[=]+")

# Thread stack symbols to extract for stack tracking
STACK_SYMBOLS = {"z_main_thread", "z_idle_threads", "z_interrupt_stacks"}

# Weather-station libraries to track individually
WS_LIBS = {
    "clock_display",
    "config_cmd",
    "fake_remote_sensor",
    "fake_sensors",
    "http_dashboard",
    "location_registry",
    "lvgl_display",
    "mqtt_publisher",
    "pipe_publisher",
    "pipe_transport",
    "remote_sensor",
    "sensor_event",
    "sensor_event_log",
    "sensor_registry",
    "sensor_trigger",
    "sntp_sync",
}

# Zephyr top-level components to track
ZEPHYR_COMPONENTS = {
    "kernel",
    "drivers",
    "subsys",
    "lib",
    "soc",
    "boards",
    "dts",
    "cmake",
    "arch",
}


@dataclass
class TreeNode:
    name: str
    size: int
    children: list["TreeNode"] = field(default_factory=list)


def parse_report(text: str, label: str) -> tuple[list[TreeNode], int]:
    """Parse a rom_report or ram_report tree into root nodes and total size."""
    roots: list[TreeNode] = []
    total_size = 0
    stack: list[TreeNode] = []
    tree_lines = 0

    for line in text.splitlines():
        line = line.rstrip()

        m = ROOT_LINE_RE.match(line)
        if m:
            total_size = int(m.group("size"))
            continue

        if SEPARATOR_RE.match(line) or not line.strip():
            continue
        if line.startswith("Path") or line.strip().isdigit():
            continue

        m = TREE_LINE_RE.match(line)
        if not m:
            continue

        tree_lines += 1
        indent = m.group("indent")
        name = m.group("name")
        size = int(m.group("size"))
        depth = len(indent) // 4 - 1

        node = TreeNode(name=name, size=size)

        if depth == 0:
            roots.append(node)
            stack = [node]
        else:
            while len(stack) > depth:
                stack.pop()
            if stack:
                stack[-1].children.append(node)
            stack.append(node)

    logger.info("%s: %d lines, %d tree lines parsed, total=%d bytes", label, len(text.splitlines()), tree_lines, total_size)

    if total_size == 0:
        logger.warning("%s: total size is zero — report may be empty or malformed", label)

    if tree_lines == 0:
        logger.error("%s: no tree lines found — report is likely empty or build failed", label)

    return roots, total_size


def _find_child(node: TreeNode, name: str) -> TreeNode | None:
    for child in node.children:
        if child.name == name:
            return child
    return None


def _collect_direct_children_sizes(node: TreeNode) -> dict[str, int]:
    """Return {child_name: size} for immediate children."""
    return {c.name: c.size for c in node.children}


def _find_lib_under_ws_lib(ws_lib_node: TreeNode) -> TreeNode | None:
    """Navigate into lib/<libname> to find the actual library node.

    The tree can have intermediate path nodes like:
        lib
          └── clock_display
              └── src
                  └── clock_display.c  <-- has the size

    Or sometimes:
        lib
          └── clock_display  <-- has the size directly
    """
    if not ws_lib_node.children:
        return ws_lib_node
    if len(ws_lib_node.children) == 1 and ws_lib_node.children[0].name not in ("src", "include"):
        return ws_lib_node.children[0]
    return ws_lib_node


def extract_workspace_entries(roots: list[TreeNode], prefix: str, report_type: str) -> list[dict]:
    """Extract weather-station module breakdown."""
    entries: list[dict] = []
    ws_root = None
    for r in roots:
        if r.name == "WORKSPACE":
            ws_root = r
            break
    if not ws_root:
        logger.warning("%s: no WORKSPACE partition found in %s report", prefix, report_type)
        return entries

    entries.append({
        "name": f"{prefix} / {report_type} / Top / WORKSPACE",
        "unit": "bytes",
        "value": ws_root.size,
    })

    ws_node = _find_child(ws_root, "weather-station")
    if not ws_node:
        logger.warning("%s: no weather-station directory in WORKSPACE (%s report)", prefix, report_type)
        return entries

    entries.append({
        "name": f"{prefix} / {report_type} / WS / weather-station",
        "unit": "bytes",
        "value": ws_node.size,
    })

    apps_node = _find_child(ws_node, "apps")
    if apps_node:
        gw_node = _find_child(apps_node, "gateway")
        if gw_node:
            entries.append({
                "name": f"{prefix} / {report_type} / WS / apps / gateway",
                "unit": "bytes",
                "value": gw_node.size,
            })

    modules_node = _find_child(ws_node, "modules")
    if modules_node:
        hal_node = _find_child(modules_node, "hal")
        if hal_node:
            entries.append({
                "name": f"{prefix} / {report_type} / WS / modules/hal",
                "unit": "bytes",
                "value": hal_node.size,
            })

    lib_node = _find_child(ws_node, "lib")
    if lib_node:
        for child in lib_node.children:
            if child.name in WS_LIBS:
                lib_entry = _find_lib_under_ws_lib(child)
                entries.append({
                    "name": f"{prefix} / {report_type} / WS / lib / {child.name}",
                    "unit": "bytes",
                    "value": lib_entry.size,
                })
            else:
                logger.debug("%s: skipping unknown WS lib '%s' (%s report)", prefix, child.name, report_type)

    return entries


def extract_zephyr_entries(roots: list[TreeNode], prefix: str, report_type: str) -> list[dict]:
    """Extract Zephyr major component breakdown."""
    entries: list[dict] = []
    zb_root = None
    for r in roots:
        if r.name == "ZEPHYR_BASE":
            zb_root = r
            break
    if not zb_root:
        logger.warning("%s: no ZEPHYR_BASE partition found in %s report", prefix, report_type)
        return entries

    entries.append({
        "name": f"{prefix} / {report_type} / Top / ZEPHYR_BASE",
        "unit": "bytes",
        "value": zb_root.size,
    })

    children_sizes = _collect_direct_children_sizes(zb_root)
    for comp in ZEPHYR_COMPONENTS:
        if comp in children_sizes:
            entries.append({
                "name": f"{prefix} / {report_type} / Zephyr / {comp}",
                "unit": "bytes",
                "value": children_sizes[comp],
            })

    return entries


def extract_top_level_entries(roots: list[TreeNode], prefix: str, report_type: str) -> list[dict]:
    """Extract top-level partition sizes."""
    entries: list[dict] = []
    for r in roots:
        if r.name in ("(hidden)", "(no paths)", "OUTPUT_DIR"):
            entries.append({
                "name": f"{prefix} / {report_type} / Top / {r.name}",
                "unit": "bytes",
                "value": r.size,
            })
    return entries


def extract_stack_entries(roots: list[TreeNode], prefix: str) -> list[dict]:
    """Extract thread stack entries from the RAM tree."""
    entries: list[dict] = []

    def _walk(tree_nodes: list[TreeNode]) -> None:
        for node in tree_nodes:
            if node.name in STACK_SYMBOLS:
                entries.append({
                    "name": f"{prefix} / Stack / {node.name}",
                    "unit": "bytes",
                    "value": node.size,
                })
            if node.children:
                _walk(node.children)

    _walk(roots)
    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, help="Path to rom_report text file")
    parser.add_argument("--ram", required=True, help="Path to ram_report text file")
    parser.add_argument("--label", default="gateway-frdm-mcxn947", help="Benchmark label")
    parser.add_argument("-o", "--output", required=True, help="Output JSON file path")
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable debug logging")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )

    rom_path = Path(args.rom)
    ram_path = Path(args.ram)

    if not rom_path.exists():
        logger.error("ROM report not found at %s. Did the rom_report build succeed?", rom_path)
        sys.exit(1)
    if not ram_path.exists():
        logger.error("RAM report not found at %s. Did the ram_report build succeed?", ram_path)
        sys.exit(1)

    logger.info("Reading ROM report from %s", rom_path)
    logger.info("Reading RAM report from %s", ram_path)

    rom_text = rom_path.read_text()
    ram_text = ram_path.read_text()

    logger.info("ROM report: %d bytes raw, RAM report: %d bytes raw", len(rom_text), len(ram_text))

    rom_roots, rom_total = parse_report(rom_text, "ROM")
    ram_roots, ram_total = parse_report(ram_text, "RAM")

    entries: list[dict] = []

    # Totals first
    entries.append({"name": f"{args.label} / ROM / Total", "unit": "bytes", "value": rom_total})
    entries.append({"name": f"{args.label} / RAM / Total", "unit": "bytes", "value": ram_total})

    # Top-level partitions
    entries.extend(extract_top_level_entries(rom_roots, args.label, "ROM"))
    entries.extend(extract_top_level_entries(ram_roots, args.label, "RAM"))

    # Workspace (weather-station) breakdown
    entries.extend(extract_workspace_entries(rom_roots, args.label, "ROM"))
    entries.extend(extract_workspace_entries(ram_roots, args.label, "RAM"))

    # Zephyr breakdown
    entries.extend(extract_zephyr_entries(rom_roots, args.label, "ROM"))
    entries.extend(extract_zephyr_entries(ram_roots, args.label, "RAM"))

    # Stack sizes
    entries.extend(extract_stack_entries(ram_roots, args.label))

    output_path = Path(args.output)
    output_path.write_text(json.dumps(entries, indent=2) + "\n")
    logger.info("Wrote %d entries to %s", len(entries), output_path)


if __name__ == "__main__":
    main()
