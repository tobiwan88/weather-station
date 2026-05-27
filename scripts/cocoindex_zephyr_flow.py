#!/usr/bin/env python3
"""CocoIndex Zephyr Corpus Indexer — Layer 2.

Indexes Zephyr RTOS public API headers, samples, subsystems, DTS bindings,
driver headers, board files, and NXP HAL into a semantic search database.

Usage:
    python scripts/cocoindex_zephyr_flow.py update   # Build/refresh index
    python scripts/cocoindex_zephyr_flow.py mcp      # Run as MCP server
    python scripts/cocoindex_zephyr_flow.py status   # Show index stats
    python scripts/cocoindex_zephyr_flow.py search "query"  # Semantic search

Requires: sentence-transformers, mcp, pydantic (installed with cocoindex-code).
If running from the system Python, use the cocoindex-code venv:
    /home/zephyr/.local/share/pipx/venvs/cocoindex-code/bin/python scripts/cocoindex_zephyr_flow.py update
"""

import sys
try:
    import sentence_transformers  # noqa: F401
    import mcp  # noqa: F401
    import pydantic  # noqa: F401
except ImportError:
    print(
        "Error: Required dependencies not found. "
        "Run with the cocoindex-code venv Python:\n"
        "  /home/zephyr/.local/share/pipx/venvs/cocoindex-code/bin/python scripts/cocoindex_zephyr_flow.py update",
        file=sys.stderr,
    )
    sys.exit(1)

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from collections.abc import AsyncIterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# ---------------------------------------------------------------------------
# ZEPHYR_BASE discovery
# ---------------------------------------------------------------------------

ZEPHYR_BASE_DEFAULT = "/home/zephyr/workspace/zephyr"


def resolve_zephyr_base() -> Path:
    """Resolve ZEPHYR_BASE from env var or default."""
    env = os.environ.get("ZEPHYR_BASE")
    if env:
        return Path(env).resolve()
    return Path(ZEPHYR_BASE_DEFAULT).resolve()


# ---------------------------------------------------------------------------
# Scope definitions
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class IndexScope:
    """A directory + include/exclude patterns for indexing."""
    name: str
    path: str
    include_patterns: list[str]
    exclude_patterns: list[str] = None

    def __post_init__(self):
        if self.exclude_patterns is None:
            object.__setattr__(self, "exclude_patterns", [])


SCOPES = [
    IndexScope(
        name="includes",
        path="include/zephyr/",
        include_patterns=["**/*.h"],
    ),
    IndexScope(
        name="samples",
        path="samples/",
        include_patterns=[
            "**/*.c", "**/*.h", "**/CMakeLists.txt", "**/prj.conf",
            "**/*.overlay", "**/*.dts", "**/*.conf", "**/Kconfig*",
            "**/*.yaml",
        ],
    ),
    IndexScope(
        name="subsys",
        path="subsys/",
        include_patterns=["**/*.h", "**/Kconfig*"],
    ),
    IndexScope(
        name="dts_bindings",
        path="dts/bindings/",
        include_patterns=["**/*.yaml"],
    ),
    IndexScope(
        name="drivers",
        path="drivers/",
        include_patterns=["**/include/**/*.h", "**/Kconfig*", "**/Kconfig"],
    ),
    IndexScope(
        name="boards_mcxn947",
        path="boards/",
        include_patterns=[
            "**/frdm_mcxn947*/**/*.dts", "**/frdm_mcxn947*/**/*.h",
            "**/frdm_mcxn947*/**/Kconfig*", "**/nxp/mcxn947*/**",
        ],
    ),
    IndexScope(
        name="hal_nxp",
        path="modules/hal/nxp/",
        include_patterns=["**/*.h", "**/Kconfig*"],
    ),
]

# ---------------------------------------------------------------------------
# Database path
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DB_DIR = PROJECT_ROOT / ".cocoindex_zephyr"
DB_PATH = DB_DIR / "zephyr_corpus.db"


# ---------------------------------------------------------------------------
# Embedder setup (matches ccc's sentence-transformers config)
# ---------------------------------------------------------------------------

def get_embedder():
    """Create a sentence-transformers embedder matching ccc's configuration."""
    from sentence_transformers import SentenceTransformer
    import numpy as np

    model_name = "Snowflake/snowflake-arctic-embed-xs"
    _cache = {}

    class Embedder:
        async def embed(self, text: str, **kwargs: Any) -> list[float]:
            if "model" not in _cache:
                _cache["model"] = SentenceTransformer(model_name)
            prompt_name = kwargs.get("prompt_name")
            if prompt_name == "query":
                text = f"Represent this sentence for searching relevant passages: {text}"
            embedding = _cache["model"].encode(text, convert_to_numpy=True)
            return embedding.tolist()

    return Embedder()


# ---------------------------------------------------------------------------
# Chunking
# ---------------------------------------------------------------------------

CHUNK_SIZE = 1000
MIN_CHUNK_SIZE = 250
CHUNK_OVERLAP = 150


def chunk_text(text: str, language: str = "text") -> list[tuple[str, int, int]]:
    """Split text into chunks, returning (content, start_line, end_line) tuples."""
    lines = text.split("\n")
    chunks = []
    current_lines = []
    current_start = 1

    for i, line in enumerate(lines, 1):
        current_lines.append(line)
        current_text = "\n".join(current_lines)
        if len(current_text) >= CHUNK_SIZE:
            # Try to find a good break point
            end = i
            chunks.append((current_text, current_start, end))
            # Keep overlap
            overlap_count = 0
            overlap_chars = 0
            keep_lines = []
            for prev_line in reversed(current_lines):
                if overlap_chars + len(prev_line) > CHUNK_OVERLAP:
                    break
                keep_lines.insert(0, prev_line)
                overlap_chars += len(prev_line) + 1
                overlap_count += 1
            current_lines = keep_lines
            current_start = i - overlap_count + 1

    if current_lines:
        chunks.append(("\n".join(current_lines), current_start, len(lines)))

    return chunks


def detect_language(filename: str) -> str:
    """Detect code language from filename."""
    ext_map = {
        ".c": "c", ".h": "c", ".cpp": "cpp", ".cxx": "cpp", ".cc": "cpp",
        ".hpp": "cpp", ".hxx": "cpp", ".hh": "cpp",
        ".py": "python", ".pyi": "python",
        ".js": "javascript", ".jsx": "javascript",
        ".ts": "typescript", ".tsx": "typescript",
        ".yaml": "yaml", ".yml": "yaml",
        ".json": "json",
        ".dts": "dts", ".overlay": "dts",
        ".conf": "text",
        "CMakeLists.txt": "cmake",
    }
    base = os.path.basename(filename)
    if base in ext_map:
        return ext_map[base]
    _, ext = os.path.splitext(filename)
    return ext_map.get(ext, "text")


# ---------------------------------------------------------------------------
# Indexing
# ---------------------------------------------------------------------------

def collect_files(zephyr_base: Path) -> list[tuple[Path, str]]:
    """Collect all files matching scope patterns."""
    result = []
    for scope in SCOPES:
        scope_dir = zephyr_base / scope.path.rstrip("/")
        if not scope_dir.is_dir():
            print(f"  Warning: {scope.name} directory not found: {scope_dir}", file=sys.stderr)
            continue
        for pattern in scope.include_patterns:
            for f in scope_dir.glob(pattern):
                if f.is_file():
                    result.append((f, scope.name))
    # Deduplicate
    seen = set()
    unique = []
    for f, scope_name in result:
        if f not in seen:
            seen.add(f)
            unique.append((f, scope_name))
    return unique


def build_index(zephyr_base: Path, verbose: bool = False) -> dict[str, int]:
    """Build or refresh the Zephyr corpus index."""
    import sqlite3

    DB_DIR.mkdir(parents=True, exist_ok=True)

    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode=WAL")

    # Create tables
    conn.execute("""
        CREATE TABLE IF NOT EXISTS zephyr_chunks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT NOT NULL,
            scope TEXT NOT NULL,
            language TEXT NOT NULL,
            content TEXT NOT NULL,
            start_line INTEGER NOT NULL,
            end_line INTEGER NOT NULL,
            embedding BLOB
        )
    """)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_chunks_file ON zephyr_chunks(file_path)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_chunks_scope ON zephyr_chunks(scope)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_chunks_lang ON zephyr_chunks(language)")

    # Clear existing index
    conn.execute("DELETE FROM zephyr_chunks")
    conn.commit()

    files = collect_files(zephyr_base)
    if verbose:
        print(f"  Collected {len(files)} files across {len(SCOPES)} scopes")

    embedder = get_embedder()
    stats = {"files": 0, "chunks": 0, "errors": 0}

    for filepath, scope_name in files:
        try:
            content = filepath.read_text(encoding="utf-8", errors="ignore")
            if not content.strip():
                continue

            language = detect_language(filepath.name)
            chunks = chunk_text(content, language)

            for chunk_text_item, start_line, end_line in chunks:
                embedding = asyncio.run(embedder.embed(chunk_text_item))
                embedding_bytes = json.dumps(embedding).encode("utf-8")

                rel_path = filepath.relative_to(zephyr_base)
                conn.execute(
                    "INSERT INTO zephyr_chunks (file_path, scope, language, content, start_line, end_line, embedding) VALUES (?, ?, ?, ?, ?, ?, ?)",
                    (str(rel_path), scope_name, language, chunk_text_item, start_line, end_line, embedding_bytes),
                )
                stats["chunks"] += 1

            stats["files"] += 1
            if verbose and stats["files"] % 50 == 0:
                print(f"  Processed {stats['files']} files, {stats['chunks']} chunks...")

        except Exception as e:
            stats["errors"] += 1
            if verbose:
                print(f"  Error processing {filepath}: {e}", file=sys.stderr)

    conn.commit()
    conn.close()
    return stats


# ---------------------------------------------------------------------------
# Search
# ---------------------------------------------------------------------------

def search(
    query: str,
    zephyr_base: Path,
    limit: int = 5,
    offset: int = 0,
    languages: list[str] | None = None,
    paths: list[str] | None = None,
) -> list[dict[str, Any]]:
    """Semantic search over the Zephyr corpus index."""
    import sqlite3
    import numpy as np
    from sentence_transformers import SentenceTransformer

    if not DB_PATH.exists():
        print("Error: Index not found. Run 'update' first.", file=sys.stderr)
        return []

    # Embed query
    model = SentenceTransformer("Snowflake/snowflake-arctic-embed-xs")
    query_embedding = model.encode(
        f"Represent this sentence for searching relevant passages: {query}",
        convert_to_numpy=True,
    ).tolist()

    conn = sqlite3.connect(str(DB_PATH))
    cursor = conn.execute(
        "SELECT id, file_path, scope, language, content, start_line, end_line, embedding FROM zephyr_chunks"
    )

    results = []
    for row in cursor:
        chunk_id, file_path, scope, language, content, start_line, end_line, embedding_bytes = row

        # Filter by language
        if languages and language not in languages:
            continue

        # Filter by path
        if paths:
            import fnmatch
            if not any(fnmatch.fnmatch(file_path, p) for p in paths):
                continue

        # Compute cosine similarity
        stored_embedding = json.loads(embedding_bytes)
        query_vec = np.array(query_embedding)
        stored_vec = np.array(stored_embedding)

        norm_q = np.linalg.norm(query_vec)
        norm_s = np.linalg.norm(stored_vec)
        if norm_q > 0 and norm_s > 0:
            score = float(np.dot(query_vec, stored_vec) / (norm_q * norm_s))
        else:
            score = 0.0

        results.append({
            "file_path": file_path,
            "scope": scope,
            "language": language,
            "content": content,
            "start_line": start_line,
            "end_line": end_line,
            "score": score,
        })

    conn.close()

    # Sort by score descending
    results.sort(key=lambda r: r["score"], reverse=True)
    return results[offset:offset + limit]


# ---------------------------------------------------------------------------
# Status
# ---------------------------------------------------------------------------

def show_status() -> None:
    """Show index statistics."""
    import sqlite3

    if not DB_PATH.exists():
        print("Index not found. Run 'update' first.")
        return

    conn = sqlite3.connect(str(DB_PATH))
    total_chunks = conn.execute("SELECT COUNT(*) FROM zephyr_chunks").fetchone()[0]
    total_files = conn.execute("SELECT COUNT(DISTINCT file_path) FROM zephyr_chunks").fetchone()[0]
    lang_rows = conn.execute(
        "SELECT language, COUNT(*) as cnt FROM zephyr_chunks GROUP BY language ORDER BY cnt DESC"
    ).fetchall()
    scope_rows = conn.execute(
        "SELECT scope, COUNT(*) as cnt FROM zephyr_chunks GROUP BY scope ORDER BY cnt DESC"
    ).fetchall()
    conn.close()

    print(f"Zephyr Corpus Index: {DB_PATH}")
    print(f"  Chunks: {total_chunks}")
    print(f"  Files:  {total_files}")
    if lang_rows:
        print("  Languages:")
        for lang, count in lang_rows:
            print(f"    {lang}: {count} chunks")
    if scope_rows:
        print("  Scopes:")
        for scope, count in scope_rows:
            print(f"    {scope}: {count} chunks")


# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------

def run_mcp() -> None:
    """Run as MCP server (stdio mode)."""
    from mcp.server.fastmcp import FastMCP
    from pydantic import Field

    zephyr_base = resolve_zephyr_base()

    mcp = FastMCP(
        "cocoindex-zephyr",
        instructions=(
            "Semantic search over the Zephyr RTOS corpus -- headers, samples, "
            "subsystems, DTS bindings, drivers, and board files. "
            "Use this to find Zephyr API usage, sample code, driver patterns, "
            "DTS binding examples, and board configurations. "
            "Start with a small limit (e.g., 5); "
            "if most results look relevant, use offset to paginate for more."
        ),
    )

    @mcp.tool(
        name="search_zephyr",
        description=(
            "Semantic search across the Zephyr RTOS codebase -- "
            "finds code by meaning, not just text matching. "
            "Use this instead of grep/glob when you need to find implementations, "
            "understand how Zephyr subsystems work, "
            "or locate related code without knowing exact names or keywords. "
            "Accepts natural language queries "
            "(e.g., 'sensor driver API', 'MQTT publish example', 'DTS binding format') "
            "or code snippets. "
            "Returns matching code chunks with file paths, "
            "line numbers, and relevance scores."
        ),
    )
    async def search_zephyr(
        query: str = Field(
            description=(
                "Natural language query or code snippet to search for. "
                "Examples: 'sensor driver API', 'MQTT publish example', "
                "'DTS binding format', or paste a code snippet to find similar code."
            )
        ),
        limit: int = Field(
            default=5,
            ge=1,
            le=100,
            description="Maximum number of results to return (1-100)",
        ),
        offset: int = Field(
            default=0,
            ge=0,
            description="Number of results to skip for pagination",
        ),
        refresh_index: bool = Field(
            default=True,
            description=(
                "Whether to incrementally update the index before searching. "
                "Set to False for faster consecutive queries "
                "when the Zephyr corpus hasn't changed."
            ),
        ),
        languages: list[str] | None = Field(
            default=None,
            description="Filter by programming language(s). Example: ['c', 'yaml']",
        ),
        paths: list[str] | None = Field(
            default=None,
            description=(
                "Filter by file path pattern(s) using GLOB wildcards (* and ?). "
                "Example: ['include/zephyr/drivers/*', '*.h']"
            ),
        ),
    ) -> dict[str, Any]:
        """Query the Zephyr corpus index."""
        try:
            if refresh_index:
                build_index(zephyr_base, verbose=False)
            results = search(
                query=query,
                zephyr_base=zephyr_base,
                limit=limit,
                offset=offset,
                languages=languages,
                paths=paths,
            )
            return {
                "success": True,
                "results": results,
                "total_returned": len(results),
                "offset": offset,
            }
        except Exception as e:
            return {"success": False, "message": f"Query failed: {e!s}"}

    async def _serve() -> None:
        await mcp.run_stdio_async()

    asyncio.run(_serve())


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="CocoIndex Zephyr Corpus Indexer — Layer 2",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # update
    p_update = subparsers.add_parser("update", help="Build/refresh the Zephyr corpus index")
    p_update.add_argument("--verbose", "-v", action="store_true", help="Show progress")

    # mcp
    subparsers.add_parser("mcp", help="Run as MCP server (stdio mode)")

    # status
    subparsers.add_parser("status", help="Show index statistics")

    # search
    p_search = subparsers.add_parser("search", help="Semantic search over Zephyr corpus")
    p_search.add_argument("query", nargs="+", help="Search query")
    p_search.add_argument("--limit", type=int, default=5, help="Max results (default: 5)")
    p_search.add_argument("--offset", type=int, default=0, help="Results to skip")
    p_search.add_argument("--lang", action="append", help="Filter by language")
    p_search.add_argument("--path", action="append", help="Filter by path glob")

    args = parser.parse_args()

    zephyr_base = resolve_zephyr_base()
    print(f"ZEPHYR_BASE: {zephyr_base}")

    if not zephyr_base.is_dir():
        print(f"Error: ZEPHYR_BASE directory not found: {zephyr_base}", file=sys.stderr)
        sys.exit(1)

    if args.command == "update":
        print("Building Zephyr corpus index...")
        stats = build_index(zephyr_base, verbose=args.verbose)
        print(f"  Files: {stats['files']}")
        print(f"  Chunks: {stats['chunks']}")
        print(f"  Errors: {stats['errors']}")
        print("Done.")

    elif args.command == "mcp":
        print("Starting Zephyr corpus MCP server...", file=sys.stderr)
        run_mcp()

    elif args.command == "status":
        show_status()

    elif args.command == "search":
        query = " ".join(args.query)
        results = search(
            query=query,
            zephyr_base=zephyr_base,
            limit=args.limit,
            offset=args.offset,
            languages=args.lang,
            paths=args.path,
        )
        if not results:
            print("No results found.")
        else:
            for i, r in enumerate(results, 1):
                print(f"\n--- Result {i} (score: {r['score']:.3f}) ---")
                print(f"File: {r['file_path']}:{r['start_line']}-{r['end_line']} [{r['language']}] (scope: {r['scope']})")
                print(r["content"][:500])


if __name__ == "__main__":
    main()
