# SPDX-License-Identifier: Apache-2.0
"""Harness for spawning and controlling the sensor-node binary as a subprocess."""
from __future__ import annotations

import logging
import os
import queue
import re
import subprocess
import threading
import time

_log = logging.getLogger("sensor_node")

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class SensorNodeHarness:
    """Controls a sensor-node.exe subprocess via stdin/stdout shell interaction."""

    def __init__(self, exe: str) -> None:
        self.exe = exe
        self._proc: subprocess.Popen | None = None
        self._lines: queue.Queue[str] = queue.Queue()
        self._reader: threading.Thread | None = None

    def start(self) -> None:
        self._proc = subprocess.Popen(
            [self.exe, "-uart_stdinout"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        self._reader = threading.Thread(target=self._drain, daemon=True)
        self._reader.start()
        _log.info("sensor-node started (pid=%d)", self._proc.pid)

    def _drain(self) -> None:
        # The shell prompt ("uart:~$ ") is not newline-terminated, so a
        # line-based read would never deliver it. Read raw chunks and flush a
        # pending partial line as soon as it contains the prompt.
        fd = self._proc.stdout.fileno()
        pending = ""
        while True:
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            pending += _ANSI_RE.sub("", chunk.decode(errors="replace"))
            *lines, pending = pending.split("\n")
            for line in lines:
                self._emit(line.rstrip("\r"))
            if "uart:~$" in pending:
                self._emit(pending)
                pending = ""

    def _emit(self, line: str) -> None:
        stripped = line.rstrip()
        _log.debug("node: %s", stripped)
        self._lines.put(stripped)

    def wait_for_ready(self, timeout: float = 15.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self._lines.get(timeout=0.5)
                if "uart:~$" in line:
                    return
            except queue.Empty:
                continue
        raise TimeoutError("sensor-node shell prompt not seen within timeout")

    def _exec(self, cmd: str, timeout: float = 3.0) -> list[str]:
        _log.debug("exec: %s", cmd)
        self._proc.stdin.write((cmd + "\n").encode())
        self._proc.stdin.flush()
        lines: list[str] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self._lines.get(timeout=0.2)
                lines.append(line)
                if "uart:~$" in line:
                    break
            except queue.Empty:
                continue
        return lines

    def trigger_all(self) -> None:
        self._exec("fake_sensors trigger")

    def set_temp(self, uid: int, mdegc: int) -> None:
        self._exec(f"fake_sensors temperature_set {uid:#06x} {mdegc}")

    def stop(self) -> None:
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait()
            _log.info("sensor-node stopped")
