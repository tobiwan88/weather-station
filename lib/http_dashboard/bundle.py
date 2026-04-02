#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
bundle.py — inline CSS and JS into an HTML template at build time.

Replaces <!-- @@CSS@@ --> with <style>...</style>
and     <!-- @@JS@@  --> with <script>...</script>

Fails loudly if either marker is missing (catches template drift).
"""

import argparse
import pathlib
import sys


def main():
    parser = argparse.ArgumentParser(description="Bundle CSS/JS into HTML template")
    parser.add_argument("--html", required=True, help="HTML template path")
    parser.add_argument("--css",  required=True, help="CSS source path")
    parser.add_argument("--js",   required=True, help="JS source path")
    parser.add_argument("--out",  required=True, help="Output HTML path")
    args = parser.parse_args()

    html_path = pathlib.Path(args.html)
    css_path  = pathlib.Path(args.css)
    js_path   = pathlib.Path(args.js)
    out_path  = pathlib.Path(args.out)

    html = html_path.read_text(encoding="utf-8")
    css  = css_path.read_text(encoding="utf-8")
    js   = js_path.read_text(encoding="utf-8")

    CSS_MARKER = "<!-- @@CSS@@ -->"
    JS_MARKER  = "<!-- @@JS@@ -->"

    if CSS_MARKER not in html:
        print(f"ERROR: {CSS_MARKER!r} not found in {html_path}", file=sys.stderr)
        sys.exit(1)
    if JS_MARKER not in html:
        print(f"ERROR: {JS_MARKER!r} not found in {html_path}", file=sys.stderr)
        sys.exit(1)

    html = html.replace(CSS_MARKER, f"<style>\n{css}\n</style>")
    html = html.replace(JS_MARKER,  f"<script>\n{js}\n</script>")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(html, encoding="utf-8")
    print(f"Bundled {html_path.name} + {css_path.name} + {js_path.name} → {out_path}")


if __name__ == "__main__":
    main()
