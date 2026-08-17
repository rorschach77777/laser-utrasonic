# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LST (主控板上位机) is a Qt6/QML desktop application for controlling an embedded laser system main board over MODBUS-RTU (RS485). The UI is in Chinese. Window size is 1280x800.

## Build

Requires Qt 6.5+ with modules: Quick, QuickControls2, SerialBus, SerialPort. Built with CMake 3.16+ and C++17.

```bash
# Configure (from project root)
mkdir -p build && cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.5.3/mingw_64

# Build
cmake --build build

# The executable is appLST (WIN32 app)
```

Tested with Desktop Qt 6.5.3 MinGW 64-bit on Windows.

## Architecture

Three-layer MVC/MVVM structure:

```
QML Pages (View) ──binds──▸ Controllers (ViewModel) ──calls──▸ DeviceComm (Modbus RTU client)
                                    │                                    │
                                    ▼                                    ▼
                              Models (QObject)                   QModbusRtuSerialClient
```

**Dependency injection**: All controllers and `DeviceComm` are instantiated in `main.cpp` and exposed to QML via `engine.rootContext()->setContextProperty()`.

**Async communication**: `DeviceComm` methods accept `std::function<void(const ModbusResponse&)>` callbacks, connected through `QModbusReply::finished`.

**Controller reuse**: `PumpController` is instantiated 4 times (pump1, pump2, slavePump1, slavePump2) with different register base addresses. `TimingController` is instantiated 2 times (main, slave) the same way. Offset constants live in `RegisterDefs.h` under `namespace Reg`.

**Polling**: `DashboardController` runs two timers — 500ms (system status) and 1000ms (device sub-status, temperature, slave pumps). Polling starts after successful connection + device info read.

## Key Source Paths

- `main.cpp` — App entry, wires all controllers and DeviceComm
- `src/communication/DeviceComm.h/cpp` — Modbus RTU wrapper around `QModbusRtuSerialClient`
- `src/models/RegisterDefs.h` — All register address constants (`namespace Reg::Input`, `Reg::Hold`, offset sub-namespaces)
- `src/models/` — QObject data models with Q_PROPERTY + NOTIFY signals
- `src/controllers/` — Business logic: read/write registers, parse responses into models
- `qml/pages/` — 11 pages (Connection, Dashboard, DFB, MultiModePump, Pump, SlaveBoard, MainTiming, SlaveTiming, Temperature, SystemSettings, Command)
- `qml/components/` — Reusable UI: CardPanel, ParamEditor (SpinBox with decimals), PulseChannelEditor, StatusIndicator, etc.
- `qml/Theme.qml` — Singleton with color/style constants (Material Indigo primary, Teal accent)
- `docs/MainBoard_Host_MODBUS_Protocol.md` — MODBUS protocol spec V1.10, register map, system architecture

## Conventions

- Models are pure data containers (no I/O). Controllers own models and handle communication.
- Register addresses are always defined in `RegisterDefs.h`, never hardcoded in controllers.
- Models decode raw register values to engineering units where needed (e.g., 0.01°C/LSB, 0.1mA/LSB).
- `SystemStatusModel` decodes status/fault bit-fields into individual bool properties.
- QML pages are disabled when `deviceComm.connected` is false (except ConnectionPage).
- Connection flow: ConnectionPage → `connectDevice()` → `readDeviceInfo()` → `deviceInfoReadFinished(true)` → start polling + switch to Dashboard.

## No Tests

There is currently no test infrastructure in this project.
