# ESPHome MCP Server — External Component

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.6-blue)](https://esphome.io)
[![MCP](https://img.shields.io/badge/MCP-2025--03--26-green)](https://modelcontextprotocol.io)
[![Board](https://img.shields.io/badge/Board-LOLIN%20S3%20Pro-orange)](https://www.wemos.cc/en/latest/s3/s3_pro.html)

An [ESPHome](https://esphome.io) external component that runs a **Model Context Protocol (MCP)** server directly on your ESP32 device. Any MCP-compatible client (Claude Desktop, VS Code Copilot, custom agents) can discover and interact with every entity on your device over the local network — no cloud required.

***UNDER HEAVY DEVELOPMENT***

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Example — Demo Node with Random Sensors](#example--demo-node-with-random-sensors)
- [Configuration Reference](#configuration-reference)
- [How It Works](#how-it-works)
  - [Architecture](#architecture)
  - [Entity Auto-Discovery](#entity-auto-discovery)
  - [Internal Entities](#internal-entities)
- [MCP Protocol Reference](#mcp-protocol-reference)
  - [Transport](#transport)
  - [Initialization Handshake](#initialization-handshake)
  - [Tools](#tools)
  - [Resources](#resources)
- [Entity Type Reference](#entity-type-reference)
  - [Read-Only Entities](#read-only-entities)
  - [Controllable Entities](#controllable-entities)
  - [Scripts](#scripts)
  - [Metadata Exposed Per Type](#metadata-exposed-per-type)
- [Connecting MCP Clients](#connecting-mcp-clients)
  - [Claude Desktop](#claude-desktop)
  - [VS Code / Copilot](#vs-code--copilot)
  - [Custom Python Client](#custom-python-client)
  - [MCP Inspector (Testing)](#mcp-inspector-testing)
- [LOLIN S3 Pro Notes](#lolin-s3-pro-notes)
- [Troubleshooting](#troubleshooting)
- [Component File Structure](#component-file-structure)
- [License](#license)

---

## Features

- **Zero-config entity discovery** — every sensor, switch, light, climate, etc. is automatically exposed as MCP tools and resources
- **Full metadata** — unit of measurement, device class, state class, icon, accuracy decimals, supported modes, min/max ranges, options, effects, and more are embedded in tool descriptions so LLM clients can reason about capabilities without a separate discovery step
- **Script execution** — ESPHome `script` components become callable MCP tools with execute / stop / is_running support
- **Runs on-device** — no proxy, no cloud, pure LAN communication over TCP
- **Conditional compilation** — only entity types you actually use in your YAML get compiled (via ESPHome's `USE_*` preprocessor guards), keeping the binary small
- **PSRAM-aware** — large JSON payloads are handled safely on PSRAM-equipped boards like the LOLIN S3 Pro
- **Respects `internal`** — entities marked `internal: true` are never exposed to MCP

---

## Requirements

| Requirement | Value |
|---|---|
| ESPHome | `2026.6` or later |
| Board | Any ESP32 with WiFi (optimized for LOLIN S3 Pro) |
| Framework | `esp-idf` recommended (Arduino also supported) |
| PSRAM | Strongly recommended for devices with many entities |
| Network | Device and MCP client must be on the same LAN |

---

## Quick Start

Add the external component to any ESPHome device config:

```yaml
external_components:
  - source: github://andrew-backway/esphome-mcp_server@main
    components: [mcp_server]

mcp_server:
  port: 8080
```

Flash the device. Every non-internal entity is now available over MCP on port `8080`.

---

## Example — Demo Node with Random Sensors

A self-contained demo you can flash immediately. Uses `template` sensors with random lambda values and template switches — no real hardware required.

```yaml
esphome:
  name: mcp-demo
  friendly_name: "MCP Demo Node"

esp32:
  board: lolin_s3_pro
  variant: esp32s3
  framework:
    type: esp-idf
    version: recommended

psram:
  mode: octal

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

logger:
api:

external_components:
  - source: github://andrew-backway/esphome-mcp_server@main
    components: [mcp_server]

mcp_server:
  port: 8080
  auto_discover: true
  expose_scripts: true

# ── Simulated sensors (random values) ──

sensor:
  - platform: template
    name: "Temperature"
    unit_of_measurement: "°C"
    accuracy_decimals: 1
    device_class: temperature
    state_class: measurement
    icon: mdi:thermometer
    update_interval: 10s
    lambda: |-
      return 18.0 + (esp_random() % 120) / 10.0;  // 18.0 – 30.0 °C

  - platform: template
    name: "Humidity"
    unit_of_measurement: "%"
    accuracy_decimals: 0
    device_class: humidity
    state_class: measurement
    icon: mdi:water-percent
    update_interval: 10s
    lambda: |-
      return 30.0 + (esp_random() % 50);  // 30 – 80 %

  - platform: template
    name: "Battery Voltage"
    unit_of_measurement: "V"
    accuracy_decimals: 2
    device_class: voltage
    state_class: measurement
    icon: mdi:battery
    update_interval: 30s
    lambda: |-
      return 3.0 + (esp_random() % 120) / 100.0;  // 3.00 – 4.20 V

  - platform: template
    name: "Light Level"
    unit_of_measurement: "lx"
    accuracy_decimals: 0
    device_class: illuminance
    state_class: measurement
    icon: mdi:brightness-5
    update_interval: 10s
    lambda: |-
      return (esp_random() % 1000);  // 0 – 1000 lx

  - platform: uptime
    name: "Uptime"

  - platform: wifi_signal
    name: "WiFi Signal"

# ── Simulated switches ──

switch:
  - platform: template
    name: "Living Room Lamp"
    icon: mdi:lamp
    device_class: outlet
    optimistic: true
    restore_mode: RESTORE_DEFAULT_OFF

  - platform: template
    name: "Porch Light"
    icon: mdi:outdoor-lamp
    device_class: outlet
    optimistic: true
    restore_mode: RESTORE_DEFAULT_OFF

  - platform: template
    name: "Fan"
    icon: mdi:fan
    device_class: switch
    optimistic: true
    restore_mode: RESTORE_DEFAULT_OFF

# ── Binary sensors ──

binary_sensor:
  - platform: template
    name: "Front Door"
    device_class: door
    icon: mdi:door
    lambda: |-
      return (esp_random() % 10) < 2;  // ~20% chance open

  - platform: template
    name: "Motion Detected"
    device_class: motion
    icon: mdi:motion-sensor
    lambda: |-
      return (esp_random() % 10) < 3;  // ~30% chance active

# ── Text sensors ──

text_sensor:
  - platform: version
    name: "ESPHome Version"
  - platform: wifi_info
    ip_address:
      name: "IP Address"

# ── Scripts ──

script:
  - id: all_off
    then:
      - switch.turn_off: living_room_lamp
      - switch.turn_off: porch_light
      - switch.turn_off: fan
      - logger.log: "All off!"

  - id: all_on
    then:
      - switch.turn_on: living_room_lamp
      - switch.turn_on: porch_light
      - logger.log: "Everything on!"

  - id: simulate_presence
    mode: single
    then:
      - switch.turn_on: living_room_lamp
      - delay: 5min
      - switch.turn_off: living_room_lamp
      - delay: 2min
      - switch.turn_on: porch_light
      - delay: 3min
      - switch.turn_off: porch_light
      - logger.log: "Presence simulation complete"
```

### What the MCP client discovers

After flashing, an MCP client connecting to `<device-ip>:8080` receives these tools:

| Tool Name | Type | Actions | Metadata in Description |
|---|---|---|---|
| `sensor_get_temperature` | sensor | Read | unit: °C, accuracy: 1, device_class: temperature, state_class: measurement, icon: mdi:thermometer |
| `sensor_get_humidity` | sensor | Read | unit: %, accuracy: 0, device_class: humidity, state_class: measurement |
| `sensor_get_battery_voltage` | sensor | Read | unit: V, accuracy: 2, device_class: voltage |
| `sensor_get_light_level` | sensor | Read | unit: lx, device_class: illuminance |
| `sensor_get_uptime` | sensor | Read | unit: s |
| `sensor_get_wifi_signal` | sensor | Read | unit: dBm, device_class: signal_strength |
| `binary_sensor_get_front_door` | binary_sensor | Read | device_class: door |
| `binary_sensor_get_motion_detected` | binary_sensor | Read | device_class: motion |
| `switch_living_room_lamp` | switch | ON / OFF / TOGGLE | device_class: outlet, icon: mdi:lamp |
| `switch_porch_light` | switch | ON / OFF / TOGGLE | device_class: outlet |
| `switch_fan` | switch | ON / OFF / TOGGLE | device_class: switch, icon: mdi:fan |
| `text_sensor_get_esphome_version` | text_sensor | Read | — |
| `text_sensor_get_ip_address` | text_sensor | Read | — |
| `script_all_off` | script | execute / stop / is_running | — |
| `script_all_on` | script | execute / stop / is_running | — |
| `script_simulate_presence` | script | execute / stop / is_running | — |

---

## Configuration Reference

```yaml
mcp_server:
  # TCP port to listen on.
  # Default: 8080
  port: 8080

  # Automatically discover and expose all non-internal entities.
  # Default: true
  auto_discover: true

  # Expose ESPHome script components as callable MCP tools.
  # Default: true
  expose_scripts: true

  # Restrict which entity types are exposed (optional).
  # If omitted, ALL types are exposed.
  entity_types:
    - sensor
    - binary_sensor
    - switch
    - light
    - fan
    - cover
    - climate
    - text_sensor
    - number
    - select
    - lock
    - button
    - media_player
    - alarm_control_panel
    - event
    - valve
    - update
    - date
    - time
    - datetime
    - text
```

| Key | Type | Default | Description |
|---|---|---|---|
| `port` | int | `8080` | TCP port the MCP server listens on |
| `auto_discover` | bool | `true` | When true, all non-internal entities are exposed automatically |
| `expose_scripts` | bool | `true` | When true, `script` components become MCP tools |
| `entity_types` | list | *(all)* | Whitelist of entity types to expose. Omit to expose everything |

---

## How It Works

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│  ESP32 (LOLIN S3 Pro)                                    │
│                                                          │
│  ┌─────────────┐    ┌──────────────────────────────────┐ │
│  │  ESPHome    │    │  MCP Server Component            │ │
│  │  App Core   │◄──►│                                  │ │
│  │             │    │  ┌────────────┐ ┌──────────────┐ │ │
│  │ • Sensors   │    │  │ TCP Server │ │ JSON-RPC     │ │ │
│  │ • Switches  │    │  │ :8080     │ │ Session Mgr  │ │ │
│  │ • Lights    │    │  └─────┬──────┘ └──────┬───────┘ │ │
│  │ • Climate   │    │        │               │         │ │
│  │ • Scripts   │    │  ┌─────▼───────────────▼───────┐ │ │
│  │ • ...       │    │  │  Entity Auto-Discovery      │ │ │
│  │             │    │  │  • App.get_sensors()         │ │ │
│  │             │    │  │  • App.get_switches()        │ │ │
│  │             │    │  │  • App.get_lights()          │ │ │
│  │             │    │  │  • ... (all 21 entity types) │ │ │
│  │             │    │  └─────────────────────────────┘ │ │
│  └─────────────┘    └──────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         ▲                          ▲
         │ Native API               │ MCP (JSON-RPC over TCP)
         ▼                          ▼
  ┌──────────────┐          ┌───────────────┐
  │ Home         │          │ Claude Desktop│
  │ Assistant    │          │ VS Code       │
  │              │          │ Custom Agent  │
  └──────────────┘          └───────────────┘
```

The component registers itself as an ESPHome `Component` with `setup()` and `loop()` lifecycle methods. During `loop()`, it:

1. **Accepts** new TCP connections (non-blocking)
2. **Reads** JSON-RPC messages from each active session
3. **Routes** messages to MCP protocol handlers
4. **Responds** with JSON-RPC results
5. **Cleans up** disconnected sessions

### Entity Auto-Discovery

At runtime, the component calls ESPHome's `App.get_<type>()` methods (e.g., `App.get_sensors()`, `App.get_switches()`) to enumerate every registered entity. For each entity, it reads all available metadata from the `EntityBase` and type-specific traits to build MCP tool definitions and resource URIs.

This happens dynamically — if you add a new sensor to your YAML and re-flash, the MCP server automatically exposes it without any additional configuration.

### Internal Entities

Entities marked `internal: true` in YAML are **never** exposed to MCP. This is the standard ESPHome mechanism for hiding helper entities (e.g., relay outputs behind a thermostat, internal PID controllers). The MCP server checks `e->is_internal()` and skips any entity that returns true.

---

## MCP Protocol Reference

This component implements the [Model Context Protocol](https://modelcontextprotocol.io) specification version `2025-03-26`.

### Transport

The server uses **raw TCP with JSON-RPC 2.0** messages delimited by newlines. Each message is a single JSON object on one line.

```
Client connects to <device-ip>:8080
Client sends: {"jsonrpc":"2.0","id":1,"method":"initialize","params":{...}}\n
Server sends: {"jsonrpc":"2.0","id":1,"result":{...}}\n
```

### Initialization Handshake

The client must send `initialize` before any other method.

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": "2025-03-26",
    "capabilities": {},
    "clientInfo": {
      "name": "my-client",
      "version": "1.0.0"
    }
  }
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2025-03-26",
    "capabilities": {
      "tools": { "listChanged": false },
      "resources": { "subscribe": false, "listChanged": false }
    },
    "serverInfo": {
      "name": "esphome-mcp",
      "version": "2026.6.0"
    }
  }
}
```

After receiving the response, the client must send the `notifications/initialized` notification:

```json
{"jsonrpc":"2.0","method":"notifications/initialized"}
```

### Tools

Tools represent **actions** — reading a sensor value, toggling a switch, executing a script.

#### `tools/list`

Returns all discovered tools with full input schemas and rich descriptions.

**Request:**
```json
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
```

**Response (abbreviated):**
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {
        "name": "sensor_get_temperature",
        "description": "Read sensor: Temperature | unit: °C | accuracy_decimals: 1 | device_class: temperature | state_class: measurement | icon: mdi:thermometer",
        "inputSchema": {
          "type": "object",
          "properties": {}
        }
      },
      {
        "name": "switch_living_room_lamp",
        "description": "Control switch: Living Room Lamp | device_class: outlet | icon: mdi:lamp | assumed_state: false",
        "inputSchema": {
          "type": "object",
          "properties": {
            "state": {
              "type": "string",
              "enum": ["ON", "OFF", "TOGGLE"],
              "description": "Desired switch state"
            }
          },
          "required": ["state"]
        }
      },
      {
        "name": "script_all_off",
        "description": "Execute script: all_off",
        "inputSchema": {
          "type": "object",
          "properties": {
            "action": {
              "type": "string",
              "enum": ["execute", "stop", "is_running"],
              "description": "Action to perform on script"
            }
          }
        }
      }
    ]
  }
}
```

#### `tools/call`

Invokes a tool by name with arguments.

**Request — read a sensor:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "sensor_get_temperature",
    "arguments": {}
  }
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"name\":\"Temperature\",\"value\":23.4,\"unit\":\"°C\",\"device_class\":\"temperature\",\"accuracy_decimals\":1,\"has_state\":true}"
      }
    ]
  }
}
```

**Request — toggle a switch:**
```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "tools/call",
  "params": {
    "name": "switch_living_room_lamp",
    "arguments": { "state": "TOGGLE" }
  }
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "result": {
    "content": [{ "type": "text", "text": "Switch living_room_lamp -> TOGGLE" }]
  }
}
```

**Request — execute a script:**
```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "tools/call",
  "params": {
    "name": "script_simulate_presence",
    "arguments": { "action": "execute" }
  }
}
```

**Error response (unknown tool):**
```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "result": {
    "content": [{ "type": "text", "text": "Unknown tool: bogus_tool" }],
    "isError": true
  }
}
```

### Resources

Resources represent **state snapshots** — the current value of any entity, readable by URI.

#### `resources/list`

**Request:**
```json
{"jsonrpc":"2.0","id":7,"method":"resources/list"}
```

**Response (abbreviated):**
```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "result": {
    "resources": [
      {
        "uri": "esphome://sensor/temperature",
        "name": "Temperature",
        "description": "sensor entity | icon: mdi:thermometer | unit: °C | device_class: temperature",
        "mimeType": "application/json"
      },
      {
        "uri": "esphome://switch/living_room_lamp",
        "name": "Living Room Lamp",
        "description": "switch entity | icon: mdi:lamp | device_class: outlet",
        "mimeType": "application/json"
      }
    ]
  }
}
```

#### `resources/read`

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 8,
  "method": "resources/read",
  "params": {
    "uri": "esphome://sensor/temperature"
  }
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 8,
  "result": {
    "contents": [
      {
        "uri": "esphome://sensor/temperature",
        "mimeType": "application/json",
        "text": "{\"name\":\"Temperature\",\"object_id\":\"temperature\",\"state\":23.4,\"unit_of_measurement\":\"°C\",\"accuracy_decimals\":1,\"device_class\":\"temperature\",\"icon\":\"mdi:thermometer\",\"has_state\":true,\"is_internal\":false,\"disabled_by_default\":false}"
      }
    ]
  }
}
```

---

## Entity Type Reference

### Read-Only Entities

These are exposed as tools that take no arguments and return current state.

| Entity Type | Tool Name Pattern | Data Returned |
|---|---|---|
| `sensor` | `sensor_get_<object_id>` | name, value (float), unit, device_class, accuracy_decimals, state_class, has_state |
| `binary_sensor` | `binary_sensor_get_<object_id>` | name, state (bool), device_class, has_state |
| `text_sensor` | `text_sensor_get_<object_id>` | name, value (string), device_class, has_state |
| `event` | `event_get_<object_id>` | name, event_type, device_class |

### Controllable Entities

These are exposed as tools with input arguments.

| Entity Type | Tool Name Pattern | Arguments |
|---|---|---|
| `switch` | `switch_<object_id>` | `state`: ON / OFF / TOGGLE |
| `button` | `button_press_<object_id>` | *(none)* |
| `light` | `light_<object_id>` | `state`, `brightness` (0–255), `r`, `g`, `b` (0–255), `color_temp` (mireds), `effect` (name), `transition_length` (ms) |
| `fan` | `fan_<object_id>` | `state`, `speed` (int), `oscillating` (bool), `direction` (FORWARD / REVERSE) |
| `cover` | `cover_<object_id>` | `command` (OPEN / CLOSE / STOP), `position` (0–1), `tilt` (0–1) |
| `climate` | `climate_<object_id>` | `mode`, `target_temperature`, `target_temperature_low`, `target_temperature_high`, `fan_mode`, `swing_mode`, `preset` |
| `number` | `number_<object_id>` | `value` (float, within min/max/step) |
| `select` | `select_<object_id>` | `option` (one of the defined options) |
| `lock` | `lock_<object_id>` | `command` (LOCK / UNLOCK / OPEN) |
| `media_player` | `media_player_<object_id>` | `command` (PLAY / PAUSE / STOP / MUTE / UNMUTE), `volume` (0–1), `media_url` |
| `alarm_control_panel` | `alarm_<object_id>` | `command` (ARM_HOME / ARM_AWAY / ARM_NIGHT / ARM_VACATION / DISARM), `code` |
| `valve` | `valve_<object_id>` | `command` (OPEN / CLOSE / STOP), `position` (0–1) |
| `update` | `update_<object_id>` | `command` (CHECK / INSTALL) |
| `date` | `date_<object_id>` | `year`, `month`, `day` |
| `time` | `time_<object_id>` | `hour`, `minute`, `second` |
| `datetime` | `datetime_<object_id>` | `epoch` (unix timestamp) |
| `text` | `text_<object_id>` | `value` (string, within min/max length) |

### Scripts

| Tool Name Pattern | Arguments |
|---|---|
| `script_<object_id>` | `action`: `execute` (default), `stop`, `is_running` |

### Metadata Exposed Per Type

Every tool description includes all available metadata from ESPHome's entity system. This is embedded in the tool's `description` string so LLM clients can reason about entities without extra calls.

| Entity Type | Metadata Fields |
|---|---|
| `sensor` | name, unit_of_measurement, accuracy_decimals, device_class, state_class, icon |
| `binary_sensor` | name, device_class, icon |
| `text_sensor` | name, device_class, icon |
| `switch` | name, device_class, icon, assumed_state |
| `button` | name, device_class, icon |
| `light` | name, icon, supported color_modes (rgb, color_temp, white, cwww), min/max_mireds, effects count |
| `fan` | name, icon, supports_speed, supports_oscillation, supports_direction, speed_count |
| `cover` | name, device_class, icon, supports_position, supports_tilt, supports_stop |
| `climate` | name, icon, supported modes, fan_modes, presets, supports_two_point, visual min/max temp, temp step, supports_action, supports_swing |
| `number` | name, icon, device_class, unit, min, max, step, mode (slider/box) |
| `select` | name, icon, all options listed |
| `lock` | name, icon, supports_open |
| `media_player` | name, icon, supports_pause |
| `alarm_control_panel` | name, icon, requires_code, supported_features |
| `valve` | name, icon, device_class, supports_position, supports_stop |
| `text` | name, icon, min_length, max_length, mode (text/password), pattern |
| `date` / `time` / `datetime` | name, icon |
| `event` | name, icon, device_class |
| `update` | name, icon |

All types also include `entity_category` (none / config / diagnostic) and `disabled_by_default` from `EntityBase`.

---

## Connecting MCP Clients

### Claude Desktop

Add to your `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "esphome-demo": {
      "transport": "tcp",
      "host": "192.168.1.100",
      "port": 8080
    }
  }
}
```

Replace the IP with your device's address. You can find it in the ESPHome logs or via the `text_sensor` → `IP Address` entity.

Once connected, Claude can:
- *"What's the current temperature and humidity?"*
- *"Turn off all the lights"*
- *"Run the simulate presence script"*
- *"Is the front door open?"*

### VS Code / Copilot

Add to your VS Code `settings.json`:

```json
{
  "mcp": {
    "servers": {
      "esphome-demo": {
        "transport": "tcp",
        "host": "192.168.1.100",
        "port": 8080
      }
    }
  }
}
```

### Custom Python Client

```python
import socket
import json

def mcp_request(sock, method, params=None, req_id=1):
    msg = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params:
        msg["params"] = params
    sock.sendall((json.dumps(msg) + "\n").encode())
    data = b""
    while b"\n" not in data:
        data += sock.recv(4096)
    return json.loads(data.decode().strip())

# Connect
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.1.100", 8080))

# Initialize
resp = mcp_request(sock, "initialize", {
    "protocolVersion": "2025-03-26",
    "capabilities": {},
    "clientInfo": {"name": "python-client", "version": "1.0.0"}
})
print("Server:", resp["result"]["serverInfo"])

# Send initialized notification
sock.sendall(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')

# List tools
resp = mcp_request(sock, "tools/list", req_id=2)
for tool in resp["result"]["tools"]:
    print(f"  {tool['name']}: {tool['description'][:80]}")

# Read temperature
resp = mcp_request(sock, "tools/call", {
    "name": "sensor_get_temperature",
    "arguments": {}
}, req_id=3)
print("Temperature:", resp["result"]["content"][0]["text"])

# Toggle a switch
resp = mcp_request(sock, "tools/call", {
    "name": "switch_living_room_lamp",
    "arguments": {"state": "TOGGLE"}
}, req_id=4)
print("Switch:", resp["result"]["content"][0]["text"])

# Execute a script
resp = mcp_request(sock, "tools/call", {
    "name": "script_all_off",
    "arguments": {"action": "execute"}
}, req_id=5)
print("Script:", resp["result"]["content"][0]["text"])

sock.close()
```

### MCP Inspector (Testing)

Use the official MCP inspector for interactive testing:

```bash
npx @modelcontextprotocol/inspector --transport tcp --host 192.168.1.100 --port 8080
```

This opens a web UI where you can browse tools, call them, and inspect responses.

---

## LOLIN S3 Pro Notes

The [LOLIN S3 Pro](https://www.wemos.cc/en/latest/s3/s3_pro.html) is the recommended board for this component.

| Spec | Value | Relevance |
|---|---|---|
| **MCU** | ESP32-S3 dual-core 240MHz | Handles TCP server + ESPHome loop concurrently |
| **Flash** | 16 MB | Plenty of room for the component + other features |
| **PSRAM** | 8 MB (OPI) | Essential for large JSON tool/resource responses |
| **WiFi** | 802.11 b/g/n | LAN MCP communication |
| **USB** | USB-C OTG | Easy flashing and serial logging |
| **GPIO** | 36 pins | Ample for real sensor/actuator projects |

### Required YAML for S3 Pro

```yaml
esp32:
  board: lolin_s3_pro
  variant: esp32s3
  framework:
    type: esp-idf       # recommended over Arduino for networking stability
    version: recommended

psram:
  mode: octal            # MUST be octal for the S3 Pro's OPI PSRAM
```

### Memory Considerations

| Scenario | Approximate RAM Usage |
|---|---|
| MCP server core | ~8 KB |
| Per active TCP session | ~4 KB buffer |
| `tools/list` response (10 entities) | ~3 KB |
| `tools/list` response (50 entities) | ~15 KB |
| `tools/list` response (100 entities) | ~30 KB (PSRAM recommended) |

The server limits concurrent sessions to **4** by default to prevent memory exhaustion. With PSRAM enabled, this is conservative — the 8 MB PSRAM can handle significantly more.

---

## Troubleshooting

### Connection refused on port 8080

- Verify the device is connected to WiFi (check ESPHome logs)
- Ensure your client is on the same network / VLAN
- Check that no other service (e.g., web_server) is using the same port

### No tools returned

- Confirm you have at least one non-internal entity defined in YAML
- If using `entity_types` filter, verify the types match your entities
- Check ESPHome logs for `[mcp_server]` messages during startup

### Timeout / slow responses

- Enable PSRAM (`psram: mode: octal`) — without it, large JSON allocations can fail or be very slow
- Reduce the number of entities or use `entity_types` to filter
- Use `esp-idf` framework instead of Arduino for better networking performance

### Entity not showing up

- Check that the entity is **not** marked `internal: true`
- Verify the entity's platform compiles with the correct `USE_*` flag
- Check `dump_config` output in ESPHome logs — the MCP server reports its settings at boot

### Script won't execute

- Verify `expose_scripts: true` (the default)
- Scripts with `mode: single` will reject execution if already running — use `is_running` to check first
- Check ESPHome logs for script execution errors

### JSON parse errors on client

- Ensure your client sends newline-delimited JSON (`\n` after each message)
- The server expects one complete JSON object per line
- Very long entity names with special characters may need escaping — stick to ASCII names

---

## Component File Structure

```
esphome-mcp-server/
├── components/
│   └── mcp_server/
│       ├── __init__.py              # Python: config validation + C++ codegen
│       ├── mcp_server.h             # Component class declaration
│       ├── mcp_server.cpp           # TCP server, session management, MCP routing
│       ├── mcp_session.h            # Per-client session (JSON-RPC read/write)
│       ├── mcp_session.cpp
│       ├── mcp_entity_tools.h       # JsonBuilder helper + tool/resource API
│       ├── mcp_entity_tools.cpp     # Auto-discovery, tool execution, resource reads
│       ├── mcp_automation.h         # Script bridge (optional, can be in entity_tools)
│       └── mcp_automation.cpp
├── README.md                        # This file
└── example.yaml                     # The demo config above
```

---

## License

MIT — see [LICENSE](LICENSE).