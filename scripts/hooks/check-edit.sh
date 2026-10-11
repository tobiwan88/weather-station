#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Claude Code PostToolUse hook (Edit|Write): reject banned patterns in the text
# just written to firmware sources. Only the new text is checked, so existing
# code elsewhere in the file does not block unrelated edits. Exit 2 sends
# stderr back to the agent.
set -uo pipefail

exec python3 -c '
import json, re, sys

data = json.load(sys.stdin)
inp = data.get("tool_input", {})
path = inp.get("file_path", "")
if not re.search(r"/(lib|apps)/.*\.[ch]$", path):
    sys.exit(0)

if "edits" in inp:
    text = "\n".join(e.get("new_string", "") for e in inp["edits"])
else:
    text = inp.get("new_string", inp.get("content", ""))

banned = re.compile(r"\b(malloc|calloc|realloc|free|k_malloc|k_calloc|k_free|"
                    r"strcpy|sprintf|gets|atoi|printk)\s*\(")
problems = []
for line in text.splitlines():
    code = re.sub(r"//.*|/\*.*?\*/|\"(\\.|[^\"\\])*\"", "", line).strip()
    if code.startswith("*") or code.startswith("/*"):
        continue
    if banned.search(code):
        problems.append(line.strip())
    if path.endswith(".h") and re.match(r"ZBUS_CHAN_DEFINE(_WITH_ID)?\(", code):
        problems.append(line.strip() + "   <- define channels in one .c, ZBUS_CHAN_DECLARE in the header")

if problems:
    print(f"Banned pattern in {path} (no heap, no strcpy/sprintf/gets/atoi, "
          "no printk in lib/ or apps/):", file=sys.stderr)
    print("\n".join(problems), file=sys.stderr)
    sys.exit(2)
'
