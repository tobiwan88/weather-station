# SPDX-License-Identifier: Apache-2.0
"""
MQTT harness — subscriber that collects messages published by the gateway.

Uses paho-mqtt to subscribe to the gateway's topic tree and exposes methods
for waiting on and asserting the content of received messages. Tests never
interact with the MQTT client directly.

The fixture in conftest.py skips tests automatically when no broker is
available, so this harness is not imported if paho-mqtt is missing.
"""

from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass, field

try:
    import paho.mqtt.client as mqtt

    _PAHO_AVAILABLE = True
except ImportError:
    _PAHO_AVAILABLE = False


@dataclass
class MqttMessage:
    topic: str
    payload: dict


class MqttHarness:
    """Collects MQTT messages published by the gateway for assertion in tests."""

    def __init__(self, broker_host: str = "localhost", broker_port: int = 1883) -> None:
        self.broker_host = broker_host
        self.broker_port = broker_port
        self._messages: list[MqttMessage] = []
        self._lock = threading.Lock()
        self._client: mqtt.Client | None = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def connect(self, topic: str = "weather/#") -> bool:
        """Connect to the broker and subscribe to ``topic``.

        Returns ``True`` on success, ``False`` if the broker is unreachable
        (so the conftest fixture can skip cleanly).
        """
        if not _PAHO_AVAILABLE:
            return False

        self._client = mqtt.Client()
        self._client.on_message = self._on_message
        try:
            self._client.connect(self.broker_host, self.broker_port, keepalive=10)
        except (ConnectionRefusedError, OSError):
            return False

        self._client.subscribe(topic)
        self._client.loop_start()
        return True

    def disconnect(self) -> None:
        if self._client is not None:
            self._client.loop_stop()
            self._client.disconnect()

    # ------------------------------------------------------------------
    # Internal callback
    # ------------------------------------------------------------------

    def _on_message(self, client, userdata, msg) -> None:
        try:
            payload = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            payload = {}
        with self._lock:
            self._messages.append(MqttMessage(topic=msg.topic, payload=payload))

    # ------------------------------------------------------------------
    # Test helpers
    # ------------------------------------------------------------------

    def clear(self) -> None:
        """Discard all previously collected messages."""
        with self._lock:
            self._messages.clear()

    def messages(self) -> list[MqttMessage]:
        """Return a snapshot of collected messages."""
        with self._lock:
            return list(self._messages)

    def wait_for_messages(
        self, count: int = 1, timeout: float = 15.0, poll_interval: float = 0.2
    ) -> list[MqttMessage]:
        """Block until at least ``count`` messages have arrived or timeout expires.

        Returns the collected messages. Never raises — callers assert on the
        returned list so failure output shows what was actually received.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                if len(self._messages) >= count:
                    return list(self._messages)
            time.sleep(poll_interval)
        with self._lock:
            return list(self._messages)

    def topics(self) -> list[str]:
        """Return unique topics seen so far."""
        with self._lock:
            return list({m.topic for m in self._messages})
