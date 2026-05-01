#!/usr/bin/env python3
"""Parse Zephyr rom_report/ram_report tree output into github-action-benchmark JSON."""

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

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


@dataclass
class TreeNode:
    name: str
    size: int
    children: list["TreeNode"] = field(default_factory=list)


def parse_report(text: str) -> list[TreeNode]:
    """Parse a rom_report or ram_report tree into a list of root-level nodes."""
    roots: list[TreeNode] = []
    total_size = 0
    stack: list[TreeNode] = []

    for line in text.splitlines():
        line = line.rstrip()

        # Capture total from Root line
        m = ROOT_LINE_RE.match(line)
        if m:
            total_size = int(m.group("size"))
            continue

        # Skip separators, headers, and summary lines
        if SEPARATOR_RE.match(line) or not line.strip():
            continue
        if line.startswith("Path") or line.strip().isdigit():
            continue

        # Parse tree lines
        m = TREE_LINE_RE.match(line)
        if not m:
            continue

        indent = m.group("indent")
        name = m.group("name")
        size = int(m.group("size"))

        # Calculate depth from indent string
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

    # Add Total as a synthetic root node
    total_node = TreeNode(name="Total", size=total_size, children=roots)
    return [total_node]


def flatten_tree(
    nodes: list[TreeNode],
    prefix: str,
    report_type: str,
) -> list[dict]:
    """Flatten tree nodes into benchmark JSON entries. Only emits leaf nodes and Total."""
    entries: list[dict] = []

    for node in nodes:
        if node.name == "Total":
            entries.append({
                "name": f"{prefix} / {report_type} / Total",
                "unit": "bytes",
                "value": node.size,
            })
            entries.extend(flatten_tree(node.children, prefix, report_type))
            continue

        # Skip (hidden) and (no paths) at ALL levels, not just leaves
        if node.name in ("(hidden)", "(no paths)"):
            continue

        if not node.children:
            # Leaf node — emit as data point
            entries.append({
                "name": f"{prefix} / {report_type} / {node.name}",
                "unit": "bytes",
                "value": node.size,
            })
        else:
            # Non-leaf: recurse into children only, don't emit intermediate sums
            entries.extend(flatten_tree(node.children, prefix, report_type))

    return entries


def extract_stack_entries(
    nodes: list[TreeNode],
    prefix: str,
) -> list[dict]:
    """Extract thread stack entries from the RAM tree."""
    entries: list[dict] = []

    def _walk(tree_nodes: list[TreeNode], path: list[str]) -> None:
        for node in tree_nodes:
            current_path = path + [node.name]
            if node.name in STACK_SYMBOLS:
                entries.append({
                    "name": f"{prefix} / Stack / {node.name}",
                    "unit": "bytes",
                    "value": node.size,
                })
            if node.children:
                _walk(node.children, current_path)

    _walk(nodes, [])
    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, help="Path to rom_report text file")
    parser.add_argument("--ram", required=True, help="Path to ram_report text file")
    parser.add_argument("--label", default="gateway-frdm-mcxn947", help="Benchmark label")
    parser.add_argument("-o", "--output", required=True, help="Output JSON file path")
    args = parser.parse_args()

    rom_path = Path(args.rom)
    ram_path = Path(args.ram)

    if not rom_path.exists():
        print(f"Error: ROM report not found at {rom_path}. Did the rom_report build succeed?", file=sys.stderr)
        sys.exit(1)
    if not ram_path.exists():
        print(f"Error: RAM report not found at {ram_path}. Did the ram_report build succeed?", file=sys.stderr)
        sys.exit(1)

    rom_text = rom_path.read_text()
    ram_text = ram_path.read_text()

    rom_roots = parse_report(rom_text)
    ram_roots = parse_report(ram_text)

    entries: list[dict] = []
    entries.extend(flatten_tree(rom_roots, args.label, "ROM"))
    entries.extend(flatten_tree(ram_roots, args.label, "RAM"))
    entries.extend(extract_stack_entries(ram_roots, args.label))

    Path(args.output).write_text(json.dumps(entries, indent=2) + "\n")
    print(f"Wrote {len(entries)} entries to {args.output}")


if __name__ == "__main__":
    main()
