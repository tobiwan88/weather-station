# SPDX-License-Identifier: Apache-2.0
"""
DeviceLogger — drains the DeviceAdapter UART queue and re-emits each line as a
structured Python log record under the ``device`` logger.

Usage
-----
Instantiate with the ``dut`` DeviceAdapter and call ``drain()`` at any point to
flush queued UART output into the pytest live-log stream::

    device_logger = DeviceLogger(dut)
    device_logger.drain()   # typically called after each test

Log records are emitted at the level carried in the Zephyr log header so that
``<err>`` lines show up as ERROR in pytest output.  Lines that do not match the
Zephyr format (shell prompts, blank lines, raw output) are emitted at DEBUG.

Boot sentinel
-------------
``main.c`` emits ``LOG_INF("device: ready")`` after all ``SYS_INIT`` callbacks
have run.  The pytest ``device_ready`` fixture in ``conftest.py`` gates on this
by calling ``shell.wait_for_prompt()``: since ``main()`` runs before the Zephyr
shell thread prints its prompt, the prompt appearing guarantees ``main.c`` has
already emitted the sentinel.  The Zephyr log line looks like::

    [00:00:00.xxx] <inf> integration: device: ready

and is detected by ``READY_PATTERN`` when searching ``handler.log``.
"""

from __future__ import annotations

import logging

from harnesses.log_parser import ZephyrLogEntry, _ANSI_RE, parse_line

_log = logging.getLogger("device")

# Regex matched against raw UART lines; must find the "device: ready" sentinel
# emitted by tests/integration/src/main.c.
READY_PATTERN = r"integration:.*device:.*ready"


class DeviceLogger:
    """Structured consumer for raw UART output from a DeviceAdapter."""

    def __init__(self, dut) -> None:
        self._dut = dut
        # Dedup key: (timestamp, module, message).  The UART terminal echoes
        # each log line twice (shell-echo form + cursor-erase+reprint form);
        # after broadening _ANSI_RE those two forms now parse to identical
        # ZephyrLogEntry objects, so we suppress the second occurrence.
        self._seen: set[tuple[str, str, str]] = set()

    def _emit_raw(self, raw: str) -> ZephyrLogEntry | None:
        """Parse *raw* and emit one log record, deduplicating Zephyr entries.

        * Zephyr log lines → emitted at their native level as ``module: msg``,
          skipped when an identical ``(timestamp, module, message)`` was already
          emitted this session.
        * Unrecognised lines → ANSI-stripped and emitted at DEBUG if non-empty.

        Returns the ``ZephyrLogEntry`` on a successful (and non-duplicate) parse,
        ``None`` otherwise.
        """
        entry = parse_line(raw)
        if entry:
            key = (entry.timestamp, entry.module, entry.message)
            if key not in self._seen:
                self._seen.add(key)
                _log.log(entry.level, "%s: %s", entry.module, entry.message)
                return entry
            return None
        stripped = _ANSI_RE.sub("", raw).strip()
        if stripped:
            _log.debug("%s", stripped)
        return None

    def drain(self) -> list[ZephyrLogEntry]:
        """Drain all pending UART lines from the device queue.

        Each line is parsed and emitted as a structured log record:

        * Zephyr log lines → logged at their native level under ``device`` logger,
          formatted as ``module: message``.
        * Unrecognised lines → ANSI-stripped and logged at DEBUG if non-empty.

        Returns the list of successfully parsed (and non-duplicate) ``ZephyrLogEntry``
        objects.
        """
        lines = self._dut.readlines(print_output=False)
        entries: list[ZephyrLogEntry] = []
        for raw in lines:
            entry = self._emit_raw(raw)
            if entry:
                entries.append(entry)
        return entries

    def wait_for_ready(self, timeout: float = 30.0) -> None:
        """Block until the DUT emits the ``device: ready`` boot sentinel.

        Reads raw lines from the device queue until ``READY_PATTERN`` matches.
        Each line is parsed and emitted as a structured log record so the boot
        sequence is fully visible in pytest live-log output.

        Raises ``TimeoutError`` if the sentinel is not seen within *timeout*
        seconds (e.g. DUT crashed at boot).
        """
        _log.info("waiting for device ready sentinel (timeout=%ss)", timeout)
        from twister_harness.exceptions import TwisterHarnessTimeoutException

        try:
            raw_lines = self._dut.readlines_until(
                regex=READY_PATTERN, timeout=timeout, print_output=False
            )
        except TwisterHarnessTimeoutException as exc:
            raise TimeoutError(
                f"DUT did not emit 'device: ready' within {timeout}s — "
                "check handler.log for crash details"
            ) from exc

        for raw in raw_lines:
            self._emit_raw(raw)

        _log.info("device ready")
