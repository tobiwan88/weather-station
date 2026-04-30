# SPDX-License-Identifier: Apache-2.0
"""
Root conftest for weather-station integration tests.

Registers pytest markers and wires up harness fixtures that abstract the
three integration surfaces: UART shell, HTTP dashboard, and MQTT broker.

All fixtures use function scope because the DUT is booted once per test
(pytest_dut_scope: function in testcase.yaml).  Each test gets a fresh
zephyr.exe instance; the previous one is killed automatically by Twister
in the DUT fixture teardown.

Log format
----------
Every log record — whether it originates from the device UART or a test
harness — is emitted in the same structure::

    HH:MM:SS LEVEL [source    ] message

  * ``source`` is the Python logger name: ``device``, ``http``, ``shell``,
    ``mqtt``.
  * Device lines are further prefixed with ``module:`` so the Zephyr module
    name is visible at a glance.

Use ``--log-cli-level=DEBUG`` to see all harness traffic; INFO is the default.
"""

import logging
import os
import subprocess
from pathlib import Path

import pytest

_log = logging.getLogger("conftest")

from harnesses.device_logger import DeviceLogger
from harnesses.http_harness import HttpHarness
from harnesses.mqtt_harness import MqttHarness
from harnesses.sensor_node_harness import SensorNodeHarness
from harnesses.shell_harness import ShellHarness

# ---------------------------------------------------------------------------
# Unified log format
# ---------------------------------------------------------------------------

_LOG_FORMAT = "%(asctime)s %(levelname)-5s [%(name)-10s] %(message)s"
_DATE_FORMAT = "%H:%M:%S"


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "smoke: quick sanity checks, run first")
    config.addinivalue_line("markers", "shell: tests using Zephyr shell interaction")
    config.addinivalue_line("markers", "http: tests using HTTP dashboard API")
    config.addinivalue_line("markers", "mqtt: tests using MQTT broker (requires mosquitto)")
    config.addinivalue_line("markers", "e2e: full end-to-end data-flow tests")
    config.addinivalue_line("markers", "system: two-process system tests requiring SENSOR_NODE_EXE")

    # Set root level so all loggers (urllib3, twister_harness, …) are captured.
    # Do NOT call logging.basicConfig() here — that would install a second root
    # StreamHandler alongside pytest's own log-cli handler and produce every line
    # twice.  Instead, override pytest's format options so the rich format is used
    # regardless of what --log-cli-format twister passes on the command line.
    logging.root.setLevel(logging.DEBUG)
    if hasattr(config, "option"):
        config.option.log_cli_format = _LOG_FORMAT
        config.option.log_cli_date_format = _DATE_FORMAT
        config.option.log_format = _LOG_FORMAT
        config.option.log_date_format = _DATE_FORMAT


def pytest_terminal_summary(terminalreporter, exitstatus, config) -> None:
    """Emit a compact pass/fail/skip tally so the log ends with a clear summary.

    The tally is written via the Python logging system so it appears in
    twister_harness.log at the same position and format as all other records.
    """
    stats = terminalreporter.stats
    passed  = len(stats.get("passed",  []))
    failed  = len(stats.get("failed",  []))
    skipped = len(stats.get("skipped", []))
    _log.info("─" * 60)
    _log.info("RESULTS  passed=%d  failed=%d  skipped=%d", passed, failed, skipped)
    for r in stats.get("failed", []):
        _log.error("FAILED  %s", r.nodeid)
    _log.info("─" * 60)


# ---------------------------------------------------------------------------
# Device log drain
# ---------------------------------------------------------------------------


@pytest.fixture()
def device_logger(dut) -> DeviceLogger:
    """Structured consumer for raw UART output from the DUT.

    Function-scoped: a new instance is created for each test's fresh DUT.
    Call ``device_logger.drain()`` explicitly, or rely on the autouse
    ``_drain_device_logs`` fixture which drains after every test.
    """
    return DeviceLogger(dut)


@pytest.fixture(autouse=True)
def _drain_device_logs(device_logger: DeviceLogger) -> None:
    """Drain device UART output after every test.

    Runs in teardown (after ``yield``) so the log lines are attributed to the
    test that was executing when they were produced.  Harness-level log records
    (``http``, ``shell``) appear inline during the test; device records appear
    in the teardown section of pytest's live-log output.
    """
    yield
    device_logger.drain()


# ---------------------------------------------------------------------------
# GDB crash watcher
# ---------------------------------------------------------------------------


@pytest.fixture()
def gdb_crash_watcher(dut, tmp_path):
    """Attach GDB to the DUT and capture a backtrace on any crash.

    Sets breakpoints on nsi_print_error_and_exit, z_fatal_error, and SIGABRT
    before the test body runs. On crash, the GDB commands block prints a full
    bt + thread list to gdb_crash.log and to the test log.

    Opt-in: only tests that list this fixture get GDB attached.
    """
    if dut._process is None:
        _log.warning("gdb_crash_watcher: DUT process not running, skipping GDB attach")
        yield
        return

    pid = dut._process.pid
    gdb_log = tmp_path / "gdb_crash.log"
    gdbinit = Path(__file__).parent / "crash_watch.gdbinit"

    cmd = [
        "gdb", "--batch",
        "-ex", "set pagination off",
        "-ex", f"set logging file {gdb_log}",
        "-ex", "set logging overwrite on",
        "-ex", "set logging enabled on",
        "-x", str(gdbinit),
        "-p", str(pid),
    ]

    _log.debug("gdb_crash_watcher: attaching gdb to pid %d", pid)
    gdb_proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    yield

    try:
        gdb_proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        gdb_proc.terminate()
        gdb_proc.wait(timeout=2)

    stdout = (gdb_proc.stdout.read() or b"").decode(errors="replace")
    if stdout.strip():
        _log.error("GDB stdout:\n%s", stdout)

    if gdb_log.exists():
        content = gdb_log.read_text()
        if content.strip():
            _log.error("GDB crash log (%s):\n%s", gdb_log, content)


# ---------------------------------------------------------------------------
# Boot sentinel
# ---------------------------------------------------------------------------


@pytest.fixture()
def device_ready(shell):
    """Signal that the DUT is fully initialised and ready for testing.

    Depends on the twister-harness ``shell`` fixture which blocks until the
    UART shell prompt is visible.  Since ``main()`` runs before the Zephyr
    shell thread prints its prompt, the prompt appearing guarantees:

    * All ``SYS_INIT`` callbacks have completed (HTTP server, MQTT publisher,
      fake sensors, …).
    * ``main.c`` has emitted ``LOG_INF("device: ready")``, which is the
      well-known boot sentinel visible in ``handler.log`` for debugging.

    All harness fixtures that interact with the device should depend on this
    fixture instead of ``dut`` directly so tests never race against boot.
    """
    shell.wait_for_prompt()


# ---------------------------------------------------------------------------
# Harness fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def shell_harness(shell, device_ready):
    """Weather-station shell abstraction wrapping the twister_harness Shell."""
    return ShellHarness(shell)


@pytest.fixture()
def http_harness(device_ready):
    """HTTP client for the dashboard API.

    Depends on ``device_ready`` (which waits for the shell prompt) instead of
    ``dut`` directly, so the HTTP server has had time to bind port 8080 before
    we attempt the first connection.
    """
    harness = HttpHarness(base_url="http://localhost:8080")
    harness.wait_until_ready()
    return harness


@pytest.fixture()
def authed_harness(http_harness):
    """Return http_harness authenticated via the login form (session cookie).

    Logs in as admin/admin (the first-boot defaults).  All subsequent requests
    on this harness carry the session cookie automatically via requests.Session.
    """
    ok = http_harness.login("admin", "admin")
    if not ok:
        pytest.fail("authed_harness: login failed; authenticated tests require a successful /api/login session setup")
    return http_harness


@pytest.fixture()
def api_token_harness(http_harness, shell_harness):
    """Return http_harness authenticated via bearer API token (no session cookie).

    Reads the active API token via ``http_dashboard token show`` and stores it
    so that all authenticated POST requests include the Authorization header.
    Used for tests that specifically exercise the bearer-token auth path.
    """
    try:
        token = http_harness.get_token_from_shell(shell_harness)
        _log.debug("api_token_harness: retrieved token (len=%d)", len(token))
        http_harness.set_token(token)
    except AssertionError as exc:
        _log.warning("api_token_harness: token retrieval failed (%s)", exc)
    return http_harness


@pytest.fixture()
def sensor_node_harness():
    exe = os.environ.get("SENSOR_NODE_EXE", "")
    if not exe:
        pytest.skip("SENSOR_NODE_EXE not set — skipping system test")
    harness = SensorNodeHarness(exe, fifo_path="/tmp/ws-node-0")
    harness.start()
    harness.wait_for_ready()
    yield harness
    harness.stop()


@pytest.fixture()
def mqtt_harness():
    """MQTT subscriber collecting messages published by the gateway.

    Skips the fixture (and any test that requests it) when no broker is
    reachable on localhost:1883, so the suite stays green in CI environments
    without Mosquitto.
    """
    harness = MqttHarness(broker_host="localhost", broker_port=1883)
    if not harness.connect():
        pytest.skip("MQTT broker not available on localhost:1883")
    yield harness
    harness.disconnect()
