#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build + test gate: build gateway, build sensor-node, shell smoke test, twister,
# pre-commit. Run inside the build environment (see scripts/ws.sh):
#
#   scripts/ws.sh 'cd weather-station && scripts/build-gate.sh [options]'
#
# Options:
#   --pristine        force pristine builds (auto when .conf/.overlay/Kconfig changed)
#   --skip-smoke      skip the shell smoke test
#   --skip-tests      skip twister
#   --skip-precommit  skip pre-commit
#   --sca             also run CodeChecker against the baseline (informational)
#   -T <path>         twister test root (default: tests/)
#
# Progress goes to stderr; a JSON summary goes to stdout.
# Exit code: 0 = all executed steps passed, 1 = a step failed.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_ROOT="$(cd "$REPO_ROOT/.." && pwd)"
BOARD=native_sim/native/64
BUILD_ROOT="$WS_ROOT/build/native_sim_native_64"
export ZEPHYR_BASE="${ZEPHYR_BASE:-$WS_ROOT/zephyr}"

pristine="" skip_smoke="" skip_tests="" skip_precommit="" run_sca="" test_root="tests/"
while [[ $# -gt 0 ]]; do
	case "$1" in
	--pristine) pristine=yes ;;
	--skip-smoke) skip_smoke=yes ;;
	--skip-tests) skip_tests=yes ;;
	--skip-precommit) skip_precommit=yes ;;
	--sca) run_sca=yes ;;
	-T)
		[[ $# -ge 2 ]] || { echo "-T needs a path" >&2; exit 2; }
		test_root="$2"
		shift
		;;
	*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

cd "$REPO_ROOT"
if [[ -z "$pristine" ]] &&
	git diff --name-only HEAD -- '*.conf' '*.overlay' '*Kconfig*' | grep -q .; then
	pristine=yes
fi

failed=0
log_dir=$(mktemp -d)

# step <name> <command...>: run, record PASS/FAIL, keep the log tail on failure.
step() {
	local name=$1
	shift
	local log="$log_dir/${name// /_}.log" start=$SECONDS rc=0
	echo "=== $name" >&2
	"$@" >"$log" 2>&1 || rc=$?
	local status=PASS detail=""
	if [[ $rc -ne 0 ]]; then
		status=FAIL
		failed=1
		detail=$(tail -n 30 "$log")
		echo "$detail" >&2
	fi
	echo "    $status ($((SECONDS - start)) s)" >&2
	printf '%s\t%s\t%s\n' "$name" "$status" $((SECONDS - start)) >>"$log_dir/steps"
	[[ -n "$detail" ]] && printf '%s' "$detail" >"$log_dir/${name// /_}.fail"
}

build() {
	west build ${pristine:+-p always} -b "$BOARD" -d "$BUILD_ROOT/$1" "apps/$1"
}

smoke() {
	local out
	out=$(printf 'fake_sensors list\n' |
		timeout 10 "$BUILD_ROOT/gateway/zephyr/zephyr.exe" -uart_stdinout 2>&1)
	# The list must show at least one fake sensor row ("0x0001  temperature ...").
	grep -qE '^0x[0-9a-f]{4}  (temperature|humidity|co2|voc)' <<<"$out"
}

step "Build gateway" build gateway
gateway_ok=$((1 - failed))
step "Build sensor-node" build sensor-node
[[ -z "$skip_smoke" && $gateway_ok -eq 1 ]] && step "Shell smoke test" smoke
[[ -z "$skip_tests" ]] && step "Twister" west twister -p "$BOARD" -T "$test_root" -N -j 8 \
	--inline-logs --outdir "$WS_ROOT/twister-out"
[[ -z "$skip_precommit" ]] && step "Pre-commit" pre-commit run --all-files
[[ -n "$run_sca" ]] && step "CodeChecker" "$REPO_ROOT/scripts/run-codechecker.sh" --diff

python3 - "$log_dir" "$failed" <<'PY'
import json, os, sys
d, failed = sys.argv[1], sys.argv[2] != "0"
steps = []
for line in open(os.path.join(d, "steps")):
	name, status, dur = line.rstrip("\n").split("\t")
	fail = os.path.join(d, name.replace(" ", "_") + ".fail")
	steps.append({"name": name, "status": status, "duration_s": int(dur),
		      "detail": open(fail).read() if os.path.exists(fail) else ""})
print(json.dumps({"script": "build-gate", "verdict": "FAIL" if failed else "PASS",
		  "steps": steps}, indent=1))
PY
rm -rf "$log_dir"
exit $failed
