#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Collect patch and context for code review.
# Outputs structured JSON with patch content, changed files, commit messages,
# pre-commit results, and file headers (first 30 lines of each changed file).
#
# Usage:
#   scripts/collect-review-context.sh [git-ref] [file...]
#
# Examples:
#   scripts/collect-review-context.sh                  # uncommitted changes
#   scripts/collect-review-context.sh HEAD~1..HEAD     # last commit
#   scripts/collect-review-context.sh master..HEAD     # commits since master
#   scripts/collect-review-context.sh -- file1.c file2.h  # specific files
#
# Exit codes:
#   0 = Context collected successfully
#   1 = No changes to review

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Parse arguments
GIT_REF=""
FILES=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --) shift; FILES+=("$@"); break ;;
        *)
            if [[ "$1" == *..* || "$1" == *~* ]]; then
                GIT_REF="$1"
            else
                FILES+=("$1")
            fi
            shift
            ;;
    esac
done

# Resolve changed files
declare -a CHANGED_FILES=()
if [[ ${#FILES[@]} -gt 0 ]]; then
    CHANGED_FILES=("${FILES[@]}")
elif [[ -n "$GIT_REF" ]]; then
    while IFS= read -r f; do
        [[ -n "$f" ]] && CHANGED_FILES+=("$f")
    done < <(git diff --name-only "$GIT_REF" 2>/dev/null || true)
else
    while IFS= read -r f; do
        [[ -n "$f" ]] && CHANGED_FILES+=("$f")
    done < <(git diff --name-only HEAD 2>/dev/null || true)
    while IFS= read -r f; do
        [[ -n "$f" ]] && CHANGED_FILES+=("$f")
    done < <(git diff --name-only --cached 2>/dev/null || true)
    if [[ ${#CHANGED_FILES[@]} -gt 0 ]]; then
        mapfile -t CHANGED_FILES < <(printf '%s\n' "${CHANGED_FILES[@]}" | sort -u)
    fi
fi

if [[ ${#CHANGED_FILES[@]} -eq 0 ]]; then
    echo '{"script": "collect-review-context", "verdict": "EMPTY", "summary": "No changes to review."}'
    exit 1
fi

json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\r'/\\r}"
    s="${s//$'\t'/\\t}"
    echo -n "$s"
}

# ============================================================
# Collect patch
# ============================================================
PATCH_CONTENT=""
if [[ -n "$GIT_REF" ]]; then
    PATCH_CONTENT=$(git diff "$GIT_REF" 2>/dev/null || true)
else
    PATCH_CONTENT=$(git diff HEAD 2>/dev/null || true)
    PATCH_CONTENT+=$'\n'
    PATCH_CONTENT+=$(git diff --cached 2>/dev/null || true)
fi

# ============================================================
# Collect diff stat
# ============================================================
DIFF_STAT=""
if [[ -n "$GIT_REF" ]]; then
    DIFF_STAT=$(git diff --stat "$GIT_REF" 2>/dev/null || true)
else
    DIFF_STAT=$(git diff --stat HEAD 2>/dev/null || true)
    DIFF_STAT+=$'\n'
    DIFF_STAT+=$(git diff --stat --cached 2>/dev/null || true)
fi

# ============================================================
# Collect commit messages (if reviewing commits)
# ============================================================
COMMIT_MESSAGES=""
if [[ -n "$GIT_REF" ]]; then
    COMMIT_MESSAGES=$(git log --oneline "$GIT_REF" 2>/dev/null || true)
fi

# ============================================================
# Run pre-commit on changed files
# ============================================================
PRECOMMIT_OUTPUT=""
PRECOMMIT_OUTPUT=$(git diff --name-only ${GIT_REF:+"$GIT_REF"} | xargs -r pre-commit run --files 2>&1 || true)

# ============================================================
# Collect file headers (first 30 lines of each changed file)
# ============================================================
declare -a FILE_HEADERS=()
for f in "${CHANGED_FILES[@]}"; do
    if [[ -f "$REPO_ROOT/$f" ]]; then
        HEADER=$(head -30 "$REPO_ROOT/$f" 2>/dev/null || true)
        FILE_HEADERS+=("{\"path\": \"$(json_escape "$f")\", \"header\": \"$(json_escape "$HEADER")\"}")
    fi
done

# ============================================================
# Emit JSON
# ============================================================
TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

FILES_JSON=""
for i in "${!CHANGED_FILES[@]}"; do
    [[ $i -gt 0 ]] && FILES_JSON+=","$'\n'
    FILES_JSON+="    \"$(json_escape "${CHANGED_FILES[$i]}")\""
done

HEADERS_JSON=""
for i in "${!FILE_HEADERS[@]}"; do
    [[ $i -gt 0 ]] && HEADERS_JSON+=","$'\n'
    HEADERS_JSON+="    ${FILE_HEADERS[$i]}"
done

PATCH_LINES=$(echo "$PATCH_CONTENT" | wc -l)

cat <<EOF
{
  "script": "collect-review-context",
  "timestamp": "$TIMESTAMP",
  "verdict": "READY",
  "files_changed": [
$FILES_JSON
  ],
  "file_count": ${#CHANGED_FILES[@]},
  "patch_lines": $PATCH_LINES,
  "diff_stat": "$(json_escape "$DIFF_STAT")",
  "patch_content": "$(json_escape "$PATCH_CONTENT")",
  "commit_messages": "$(json_escape "$COMMIT_MESSAGES")",
  "pre_commit_output": "$(json_escape "$PRECOMMIT_OUTPUT")",
  "file_headers": [
$HEADERS_JSON
  ],
  "summary": "Collected context for ${#CHANGED_FILES[@]} files, $PATCH_LINES lines of diff."
}
EOF

exit 0
