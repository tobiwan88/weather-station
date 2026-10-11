# <Feature> — Implementation Plan

Source: <REQ path or description> · Date: YYYY-MM-DD

## Summary
<One paragraph: what the feature does and why.>

## Subtasks

### - [ ] IMPL-001: <imperative title>
- **Goal:** <one sentence>
- **Files (create):** `lib/x/src/x.c`, `lib/x/include/x/x.h`, `tests/x/testcase.yaml`
- **Files (modify):** `apps/gateway/prj.conf` (or `none`)
- **Depends on:** none
- **Parallel with:** IMPL-003 (or `none`)
- **Acceptance:** `scripts/ws.sh 'west twister -p native_sim/native/64 -T weather-station/tests/x --inline-logs -v -N'`
- **Interfaces:** public signatures and Kconfig symbols other subtasks rely on.

<Repeat for each subtask. Keep each block self-contained.>

## Dependency graph
```
IMPL-001 ──┐
IMPL-002 ──┼──► IMPL-004
IMPL-003 ──┘
```

## File conflicts
<Shared files and the subtasks they join, or "No conflicts".>

## Done
- [ ] Every subtask ticked after implementer and reviewer pass
- [ ] `scripts/ws.sh 'west build -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'` passes
- [ ] `scripts/ws.sh 'west build -b native_sim/native/64 -d build/native_sim_native_64/sensor-node weather-station/apps/sensor-node'` passes
- [ ] Twister suites listed above pass
- [ ] `pre-commit run --all-files` is clean

## Open questions
<Unresolved questions, or "None".>
