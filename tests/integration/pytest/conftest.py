# SPDX-License-Identifier: Apache-2.0
"""
Root conftest for weather-station integration tests.

Registers pytest markers and wires up harness fixtures that abstract the
three integration surfaces: UART shell, HTTP dashboard, and MQTT broker.

All fixtures use session scope because the DUT is booted once
(pytest_dut_scope: session in testcase.yaml) and shared across tests.
"""

import pytest

from harnesses.http_harness import HttpHarness
from harnesses.mqtt_harness import MqttHarness
from harnesses.shell_harness import ShellHarness


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "smoke: quick sanity checks, run first")
    config.addinivalue_line("markers", "shell: tests using Zephyr shell interaction")
    config.addinivalue_line("markers", "http: tests using HTTP dashboard API")
    config.addinivalue_line("markers", "mqtt: tests using MQTT broker (requires mosquitto)")
    config.addinivalue_line("markers", "e2e: full end-to-end data-flow tests")


@pytest.fixture(scope="session")
def shell_harness(shell):
    """Weather-station shell abstraction wrapping the twister_harness Shell."""
    return ShellHarness(shell)


@pytest.fixture(scope="session")
def http_harness(dut):
    """HTTP client for the dashboard API.

    ``dut`` is requested so pytest waits for the device to be booted before
    the fixture is created — the HTTP server is only ready after boot.
    """
    harness = HttpHarness(base_url="http://localhost:8080")
    harness.wait_until_ready()
    return harness


@pytest.fixture(scope="session")
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
