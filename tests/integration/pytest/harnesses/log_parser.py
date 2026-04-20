# SPDX-License-Identifier: Apache-2.0
"""
Zephyr log line parser.

Converts raw UART output lines from the device into structured ``ZephyrLogEntry``
objects. Tests and harnesses use this to surface device-side log output in a
uniform format alongside harness-side log records.

Zephyr format::

    [HH:MM:SS.mmm,mmm] \x1b[...m<inf> module: message\x1b[...m

Level tags: ``inf`` → INFO, ``dbg`` → DEBUG, ``wrn`` → WARNING, ``err`` → ERROR.
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass

# Strip all ANSI/VT CSI escape sequences (color, cursor movement, erase, …).
# The original pattern only matched SGR sequences ending in 'm'; broadening to
# any letter terminator also removes cursor-back ([8D) and erase-line ([J) codes
# that the UART terminal emits around shell prompts.  Without this, those codes
# survive stripping and the Zephyr log regex matches the same logical line twice
# (once in the shell-echo form, once in the erase+reprint form), producing
# duplicate device log records.
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# Match a Zephyr log line after ANSI stripping:
#   [00:00:01.234,567] <inf> http_dashboard: server ready on port 8080
_LOG_RE = re.compile(
    r"\[(\d{2}:\d{2}:\d{2}\.[\d,]+)\]"  # [HH:MM:SS.mmm,mmm]
    r"[^\<]*"                          # optional gap / extra ANSI residue
    r"<(inf|dbg|wrn|err)>\s+"         # <level>
    r"([\w]+):\s*(.*)"                 # module: message
)

_LEVEL_MAP: dict[str, int] = {
    "inf": logging.INFO,
    "dbg": logging.DEBUG,
    "wrn": logging.WARNING,
    "err": logging.ERROR,
}


@dataclass
class ZephyrLogEntry:
    """One parsed Zephyr log record."""

    timestamp: str   # wall-clock uptime string, e.g. "00:00:01.234,567"
    level: int       # Python logging level constant (logging.INFO etc.)
    level_tag: str   # original two-char tag: "inf", "dbg", "wrn", "err"
    module: str      # Zephyr module name, e.g. "http_dashboard"
    message: str     # log message body
    raw: str         # original unmodified line


def parse_line(line: str) -> ZephyrLogEntry | None:
    """Parse a single raw UART line into a ``ZephyrLogEntry``.

    Returns ``None`` when the line does not match the Zephyr log format
    (e.g. shell prompts, blank lines, non-log output).
    """
    clean = _ANSI_RE.sub("", line)
    m = _LOG_RE.search(clean)
    if not m:
        return None
    tag = m.group(2)
    return ZephyrLogEntry(
        timestamp=m.group(1),
        level=_LEVEL_MAP.get(tag, logging.DEBUG),
        level_tag=tag,
        module=m.group(3),
        message=m.group(4).strip(),
        raw=line,
    )
