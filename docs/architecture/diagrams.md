# Architecture Diagrams

Diagrams are authored as Mermaid source files in `docs/architecture/diagrams/`.
They are embedded inline in the most relevant architecture page and collected here
as a reference catalog. Click any diagram to zoom (lightbox).

## System Overview

Layered component view of the weather-station firmware.

```mermaid
--8<-- "system-overview.mmd"
```

## zbus Channel Map

Publishers, channels, and subscribers across the zbus event bus.

```mermaid
--8<-- "zbus-channels.mmd"
```

## Sensor Data Flow

Sensor trigger → event → consumers sequence.

```mermaid
--8<-- "data-flow.mmd"
```

## Library Dependencies

Inter-library dependency graph.

```mermaid
--8<-- "library-deps.mmd"
```

## Boot Sequence

SYS_INIT boot ordering by priority.

```mermaid
--8<-- "init-sequence.mmd"
```

## HTTP Config Flow

POST /api/config side-effects.

```mermaid
--8<-- "http-flow.mmd"
```

## Concurrency — Snapshot Pattern

How the HTTP dashboard safely shares ring buffer data between an ISR-derived producer
and an HTTP handler thread without holding the lock during serialisation.

```mermaid
--8<-- "snapshot-pattern.mmd"
```

## FOTA Update Flow

End-to-end firmware update: HTTP upload → MCUboot swap → confirm.

```mermaid
--8<-- "fota-flow.mmd"
```

## Integration Test Pipeline

How Twister, the native_sim binary, and pytest interact during a test run.

```mermaid
--8<-- "integration-test-pipeline.mmd"
```

## Fake Sensor Data Flow

Fake sensor lifecycle: board overlay → DT_FOREACH → SYS_INIT → trigger → publish.

```mermaid
--8<-- "fake-sensor-flow.mmd"
```
