# SPDX-License-Identifier: Apache-2.0
"""
Shell harness — Page Object for the weather-station Zephyr shell.

Wraps twister_harness.Shell and provides typed, parsed methods for each
weather-station shell sub-command. Tests never send raw strings; they call
methods here. If a command's output format changes, only this file needs
updating.
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass

from twister_harness import Shell

_log = logging.getLogger("shell")


@dataclass
class SensorEntry:
    uid: int
    kind: str
    location: str
    value_milli: int
    unit: str


class ShellHarness:
    """Typed interface to the weather-station shell sub-commands."""

    def __init__(self, shell: Shell) -> None:
        self._shell = shell

    def wait_for_prompt(self, timeout: float | None = None) -> None:
        """Block until the shell prompt is ready."""
        self._shell.wait_for_prompt(timeout)

    def _exec(self, cmd: str) -> list[str]:
        """Execute a shell command and return lines, with structured logging."""
        _log.debug("exec: %s", cmd)
        lines = self._shell.exec_command(cmd)
        _log.debug("← %d line(s)", len(lines))
        return lines

    # ------------------------------------------------------------------
    # kernel commands
    # ------------------------------------------------------------------

    def get_uptime_ms(self) -> int:
        """Return device uptime in milliseconds via ``kernel uptime``."""
        lines = self._exec("kernel uptime")
        for line in lines:
            # Zephyr shell prints: "Uptime: 1234 ms"
            m = re.search(r"(\d+)\s*ms", line)
            if m:
                return int(m.group(1))
        raise AssertionError(f"Could not parse uptime from: {lines}")

    def get_version(self) -> str:
        """Return the Zephyr version string via ``kernel version``."""
        lines = self._exec("kernel version")
        for line in lines:
            if "Zephyr version" in line:
                return line.strip()
        raise AssertionError(f"Could not find version string in: {lines}")

    # ------------------------------------------------------------------
    # fake_sensors commands
    # ------------------------------------------------------------------

    def list_sensors(self) -> list[SensorEntry]:
        """Parse ``fake_sensors list`` output into SensorEntry objects.

        Example row:
            0x0001  temperature   living room           21000 mdeg C
        """
        lines = self._exec("fake_sensors list")
        sensors: list[SensorEntry] = []
        # Match lines starting with 0x<hex>
        row_re = re.compile(
            r"(0x[0-9a-fA-F]+)\s+"
            r"(\S+)\s+"
            r"(.{0,20}?)\s{2,}"
            r"(-?\d+)\s+(.+)"
        )
        for line in lines:
            m = row_re.match(line.strip())
            if m:
                sensors.append(
                    SensorEntry(
                        uid=int(m.group(1), 16),
                        kind=m.group(2),
                        location=m.group(3).strip(),
                        value_milli=int(m.group(4)),
                        unit=m.group(5).strip(),
                    )
                )
        return sensors

    def trigger_all(self) -> str:
        """Broadcast a manual trigger via ``fake_sensors trigger``.

        Returns the confirmation line from the shell.
        """
        lines = self._exec("fake_sensors trigger")
        for line in lines:
            if "trigger" in line.lower():
                return line.strip()
        return ""

    def set_temperature(self, uid: int, mdegc: int) -> None:
        """Set a fake temperature sensor value via shell."""
        self._exec(f"fake_sensors temperature_set {uid:#06x} {mdegc}")

    def set_humidity(self, uid: int, mpct: int) -> None:
        """Set a fake humidity sensor value via shell."""
        self._exec(f"fake_sensors humidity_set {uid:#06x} {mpct}")

    def set_co2(self, uid: int, mppm: int) -> None:
        """Set a fake CO2 sensor value via shell."""
        self._exec(f"fake_sensors co2_set {uid:#06x} {mppm}")

    def set_voc(self, uid: int, miaq: int) -> None:
        """Set a fake VOC sensor value via shell."""
        self._exec(f"fake_sensors voc_set {uid:#06x} {miaq}")

    # ------------------------------------------------------------------
    # help
    # ------------------------------------------------------------------

    def help(self) -> list[str]:
        """Return lines from the ``help`` command."""
        return self._exec("help")
