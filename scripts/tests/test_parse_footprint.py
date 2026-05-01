"""Tests for parse_footprint.py."""

from pathlib import Path

import pytest

from parse_footprint import (
    STACK_SYMBOLS,
    TreeNode,
    extract_stack_entries,
    flatten_tree,
    parse_report,
)

FIXTURES_DIR = Path(__file__).parent / "fixtures"


class TestParseReport:
    def test_parses_root_total(self):
        text = (
            "Path                                                           Size    %      Address\n"
            "========================================================================================\n"
            "Root                                                          27828 100.00%  -\n"
            "========================================================================================\n"
            "                                                              21652\n"
        )
        roots = parse_report(text)
        assert roots[0].name == "Total"
        assert roots[0].size == 27828

    def test_parses_tree_nodes(self):
        text = (
            "Path                                                           Size    %      Address\n"
            "========================================================================================\n"
            "Root                                                           1000 100.00%  -\n"
            "├── unmapped                                                    500  50.00%  -\n"
            "│   ├── _kernel                                                  32   3.20%  0x20000318\n"
            "│   └── z_main_thread                                           128  12.80%  0x200001a0\n"
            "└── ZEPHYR_BASE                                                 500  50.00%  -\n"
            "    └── drivers                                                 200  20.00%  -\n"
            "        └── sensor.c                                            100  10.00%  0x00001200\n"
            "========================================================================================\n"
            "                                                              1000\n"
        )
        roots = parse_report(text)
        total = roots[0]
        assert total.size == 1000
        assert len(total.children) == 2
        assert total.children[0].name == "unmapped"
        assert total.children[1].name == "ZEPHYR_BASE"

    def test_nested_children(self):
        text = (
            "Path                                                           Size    %      Address\n"
            "========================================================================================\n"
            "Root                                                           1000 100.00%  -\n"
            "└── ZEPHYR_BASE                                                 500  50.00%  -\n"
            "    └── drivers                                                 200  20.00%  -\n"
            "        └── sensor.c                                            100  10.00%  0x00001200\n"
            "========================================================================================\n"
            "                                                              1000\n"
        )
        roots = parse_report(text)
        zephyr = roots[0].children[0]
        drivers = zephyr.children[0]
        sensor = drivers.children[0]
        assert sensor.name == "sensor.c"
        assert sensor.size == 100
        assert not sensor.children


class TestFlattenTree:
    def test_skips_hidden_and_no_paths(self):
        nodes = [
            TreeNode(name="Total", size=1000, children=[
                TreeNode(name="(hidden)", size=10),
                TreeNode(name="(no paths)", size=500),
                TreeNode(name="ZEPHYR_BASE", size=490, children=[
                    TreeNode(name="drivers", size=200, children=[
                        TreeNode(name="sensor.c", size=100),
                    ]),
                ]),
            ]),
        ]
        entries = flatten_tree(nodes, "test-board", "ROM")
        names = [e["name"] for e in entries]
        assert not any("(hidden)" in n for n in names)
        assert not any("(no paths)" in n for n in names)

    def test_emits_leaf_nodes_only(self):
        nodes = [
            TreeNode(name="Total", size=1000, children=[
                TreeNode(name="ZEPHYR_BASE", size=490, children=[
                    TreeNode(name="sensor.c", size=100),
                ]),
            ]),
        ]
        entries = flatten_tree(nodes, "test-board", "ROM")
        names = [e["name"] for e in entries]
        assert "test-board / ROM / Total" in names
        assert "test-board / ROM / sensor.c" in names
        assert "test-board / ROM / ZEPHYR_BASE" not in names

    def test_total_entry(self):
        nodes = [TreeNode(name="Total", size=27828, children=[])]
        entries = flatten_tree(nodes, "gw", "ROM")
        assert len(entries) == 1
        assert entries[0]["name"] == "gw / ROM / Total"
        assert entries[0]["value"] == 27828


class TestExtractStackEntries:
    def test_finds_known_stacks(self):
        nodes = [
            TreeNode(name="Total", size=4637, children=[
                TreeNode(name="(no paths)", size=2748, children=[
                    TreeNode(name="z_main_thread", size=128),
                    TreeNode(name="z_idle_threads", size=128),
                    TreeNode(name="z_interrupt_stacks", size=2048),
                    TreeNode(name="_kernel", size=32),
                ]),
            ]),
        ]
        entries = extract_stack_entries(nodes, "test-board")
        names = {e["name"] for e in entries}
        assert "test-board / Stack / z_main_thread" in names
        assert "test-board / Stack / z_idle_threads" in names
        assert "test-board / Stack / z_interrupt_stacks" in names
        assert len(entries) == 3

    def test_ignores_non_stack_symbols(self):
        nodes = [
            TreeNode(name="Total", size=1000, children=[
                TreeNode(name="(no paths)", size=500, children=[
                    TreeNode(name="_kernel", size=32),
                    TreeNode(name="_cpus_active", size=4),
                ]),
            ]),
        ]
        entries = extract_stack_entries(nodes, "test-board")
        assert len(entries) == 0


class TestFixtureIntegration:
    def test_rom_fixture_parses(self):
        text = (FIXTURES_DIR / "rom_report_sample.txt").read_text()
        roots = parse_report(text)
        assert roots[0].name == "Total"
        assert roots[0].size == 27828

    def test_ram_fixture_parses(self):
        text = (FIXTURES_DIR / "ram_report_sample.txt").read_text()
        roots = parse_report(text)
        assert roots[0].name == "Total"
        assert roots[0].size == 4637
