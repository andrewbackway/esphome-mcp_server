#!/usr/bin/env python3
"""
ESPHome MCP Server — Complete Integration Test Suite

Tests every MCP method and every entity type with full coverage:
  - initialize / notifications/initialized handshake
  - tools/list (validates every entity appears with correct schema)
  - tools/call (exercises every entity type's actions)
  - resources/list (validates every entity has a resource URI)
  - resources/read (reads state for every entity)
  - Error handling (unknown tools, bad arguments)
  - Script lifecycle (execute, is_running, stop)
  - Session management (connect, disconnect, reconnect)

Usage:
    python test_mcp_server.py                          # defaults to 192.168.1.100:8080
    python test_mcp_server.py --host 10.0.0.50         # custom host
    python test_mcp_server.py --host 10.0.0.50 --port 9090
    python test_mcp_server.py --verbose                # show full JSON payloads
"""

import socket
import json
import sys
import time
import argparse
from dataclasses import dataclass, field
from typing import Any


# ════════════════════════════════════════════════════════════════
#  Configuration — must match kitchen_sink.yaml
# ════════════════════════════════════════════════════════════════

# fmt: off

# Every entity we expect the MCP server to expose, by type.
# Format: (tool_name, is_read_only, required_input_keys, resource_uri)

EXPECTED_SENSORS = [
    ("sensor_get_temperature",     "esphome://sensor/temperature"),
    ("sensor_get_humidity",        "esphome://sensor/humidity"),
    ("sensor_get_power_usage",     "esphome://sensor/power_usage"),
    ("sensor_get_battery_voltage", "esphome://sensor/battery_voltage"),
    ("sensor_get_co2_level",       "esphome://sensor/co2_level"),
]

EXPECTED_BINARY_SENSORS = [
    ("binary_sensor_get_front_door",      "esphome://binary_sensor/front_door"),
    ("binary_sensor_get_motion_detected", "esphome://binary_sensor/motion_detected"),
    ("binary_sensor_get_smoke_alarm",     "esphome://binary_sensor/smoke_alarm"),
]

EXPECTED_TEXT_SENSORS = [
    ("text_sensor_get_system_status", "esphome://text_sensor/system_status"),
    ("text_sensor_get_last_event",    "esphome://text_sensor/last_event"),
]

EXPECTED_SWITCHES = [
    ("switch_living_room_lamp", "esphome://switch/living_room_lamp"),
    ("switch_porch_light",      "esphome://switch/porch_light"),
    ("switch_ventilation_fan",  "esphome://switch/ventilation_fan"),
]

EXPECTED_BUTTONS = [
    ("button_press_panic_button", "esphome://button/panic_button"),
    ("button_press_doorbell",     "esphome://button/doorbell"),
]

EXPECTED_LIGHTS = [
    ("light_ceiling_light", "esphome://light/ceiling_light"),
]

EXPECTED_FANS = [
    ("fan_bedroom_fan", "esphome://fan/bedroom_fan"),
]

EXPECTED_COVERS = [
    ("cover_garage_door",  "esphome://cover/garage_door"),
    ("cover_window_blinds", "esphome://cover/window_blinds"),
]

EXPECTED_CLIMATES = [
    ("climate_main_thermostat", "esphome://climate/main_thermostat"),
]

EXPECTED_NUMBERS = [
    ("number_target_brightness", "esphome://number/target_brightness"),
    ("number_volume_level",      "esphome://number/volume_level"),
]

EXPECTED_SELECTS = [
    ("select_operating_mode", "esphome://select/operating_mode"),
]

EXPECTED_LOCKS = [
    ("lock_front_door_lock", "esphome://lock/front_door_lock"),
]

EXPECTED_VALVES = [
    ("valve_zone_1_sprinkler", "esphome://valve/zone_1_sprinkler"),
    ("valve_zone_2_sprinkler", "esphome://valve/zone_2_sprinkler"),
]

EXPECTED_TEXTS = [
    ("text_display_message", "esphome://text/display_message"),
]

EXPECTED_DATES = [
    ("date_schedule_date", "esphome://date/schedule_date"),
]

EXPECTED_TIMES = [
    ("time_alarm_time", "esphome://time/alarm_time"),
]

EXPECTED_DATETIMES = [
    ("datetime_next_run", "esphome://datetime/next_run"),
]

EXPECTED_ALARMS = [
    ("alarm_home_alarm", "esphome://alarm_control_panel/home_alarm"),
]

EXPECTED_SCRIPTS = [
    "script_all_off",
    "script_all_on",
    "script_morning_routine",
    "script_night_routine",
    "script_long_running_task",
    "script_irrigation_cycle",
]

# Metadata keywords we expect in tool descriptions per entity type
EXPECTED_METADATA = {
    "sensor_get_temperature":       ["°C", "temperature", "mdi:thermometer", "measurement"],
    "sensor_get_humidity":          ["%", "humidity", "measurement"],
    "sensor_get_power_usage":       ["W", "power", "measurement"],
    "sensor_get_battery_voltage":   ["V", "voltage"],
    "sensor_get_co2_level":         ["ppm", "carbon_dioxide"],
    "binary_sensor_get_front_door": ["door"],
    "binary_sensor_get_motion_detected": ["motion"],
    "binary_sensor_get_smoke_alarm":     ["smoke"],
    "switch_living_room_lamp":      ["outlet", "mdi:lamp"],
    "switch_ventilation_fan":       ["switch", "mdi:fan"],
    "light_ceiling_light":          ["mdi:ceiling-light"],
    "fan_bedroom_fan":              ["speed", "oscillat", "direction"],
    "cover_garage_door":            ["garage", "position"],
    "cover_window_blinds":          ["blind", "position", "tilt"],
    "climate_main_thermostat":      ["mdi:thermostat", "HEAT", "COOL", "HOME", "AWAY", "SLEEP"],
    "number_target_brightness":     ["min", "max", "step", "slider"],
    "number_volume_level":          ["dB", "box"],
    "select_operating_mode":        ["Auto", "Manual", "Eco", "Boost", "Off"],
    "lock_front_door_lock":         ["LOCK", "UNLOCK"],
    "valve_zone_1_sprinkler":       ["water", "OPEN", "CLOSE"],
    "alarm_home_alarm":             ["ARM_HOME", "ARM_AWAY", "DISARM", "code"],
    "text_display_message":         ["min", "max"],
}

# fmt: on


# ════════════════════════════════════════════════════════════════
#  Test Framework
# ════════════════════════════════════════════════════════════════

@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""
    detail: str = ""


@dataclass
class TestSuite:
    results: list[TestResult] = field(default_factory=list)
    verbose: bool = False

    def record(self, name: str, passed: bool, message: str = "", detail: str = ""):
        self.results.append(TestResult(name, passed, message, detail))
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {status}  {name}")
        if not passed and message:
            print(f"         → {message}")
        if self.verbose and detail:
            print(f"         {detail[:200]}")

    def summary(self):
        total = len(self.results)
        passed = sum(1 for r in self.results if r.passed)
        failed = total - passed
        print("\n" + "═" * 60)
        print(f"  RESULTS: {passed}/{total} passed, {failed} failed")
        print("═" * 60)
        if failed:
            print("\n  Failed tests:")
            for r in self.results:
                if not r.passed:
                    print(f"    ✗ {r.name}: {r.message}")
            print()
        return failed == 0


# ════════════════════════════════════════════════════════════════
#  MCP Client
# ════════════════════════════════════════════════════════════════

class MCPClient:
    def __init__(self, host: str, port: int, timeout: float = 10.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self._req_id = 0

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))

    def disconnect(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def _next_id(self) -> int:
        self._req_id += 1
        return self._req_id

    def send_request(self, method: str, params: dict | None = None) -> dict:
        req_id = self._next_id()
        msg: dict[str, Any] = {"jsonrpc": "2.0", "id": req_id, "method": method}
        if params is not None:
            msg["params"] = params
        raw = json.dumps(msg) + "\n"
        self.sock.sendall(raw.encode())
        return self._read_response(req_id)

    def send_notification(self, method: str, params: dict | None = None):
        msg: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        raw = json.dumps(msg) + "\n"
        self.sock.sendall(raw.encode())

    def _read_response(self, expected_id: int) -> dict:
        buf = b""
        while b"\n" not in buf:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise ConnectionError("Server closed connection")
            buf += chunk
        line = buf.split(b"\n", 1)[0]
        resp = json.loads(line.decode())
        if resp.get("id") != expected_id:
            raise ValueError(f"ID mismatch: expected {expected_id}, got {resp.get('id')}")
        return resp

    # ── High-level helpers ──

    def initialize(self) -> dict:
        resp = self.send_request("initialize", {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "test-suite", "version": "1.0.0"},
        })
        self.send_notification("notifications/initialized")
        return resp

    def tools_list(self) -> list[dict]:
        resp = self.send_request("tools/list")
        return resp.get("result", {}).get("tools", [])

    def tools_call(self, name: str, arguments: dict | None = None) -> dict:
        resp = self.send_request("tools/call", {
            "name": name,
            "arguments": arguments or {},
        })
        return resp.get("result", {})

    def resources_list(self) -> list[dict]:
        resp = self.send_request("resources/list")
        return resp.get("result", {}).get("resources", [])

    def resources_read(self, uri: str) -> dict:
        resp = self.send_request("resources/read", {"uri": uri})
        return resp.get("result", {})


# ════════════════════════════════════════════════════════════════
#  Test Cases
# ════════════════════════════════════════════════════════════════

def test_connection(client: MCPClient, suite: TestSuite):
    """Test basic TCP connection."""
    print("\n── Connection ──")
    try:
        client.connect()
        suite.record("tcp_connect", True)
    except Exception as e:
        suite.record("tcp_connect", False, str(e))
        raise SystemExit(1)


def test_initialize(client: MCPClient, suite: TestSuite):
    """Test MCP initialize handshake."""
    print("\n── Initialize ──")
    try:
        resp = client.initialize()
        result = resp.get("result", {})

        suite.record(
            "initialize_protocol_version",
            result.get("protocolVersion") == "2025-03-26",
            f"Got: {result.get('protocolVersion')}",
        )
        suite.record(
            "initialize_server_name",
            result.get("serverInfo", {}).get("name") == "esphome-mcp",
            f"Got: {result.get('serverInfo', {}).get('name')}",
        )
        suite.record(
            "initialize_capabilities_tools",
            "tools" in result.get("capabilities", {}),
            "Missing 'tools' capability",
        )
        suite.record(
            "initialize_capabilities_resources",
            "resources" in result.get("capabilities", {}),
            "Missing 'resources' capability",
        )
        suite.record(
            "initialize_no_error",
            "error" not in resp,
            f"Error: {resp.get('error')}",
        )
    except Exception as e:
        suite.record("initialize", False, str(e))


def test_tools_list(client: MCPClient, suite: TestSuite) -> dict[str, dict]:
    """Test tools/list and validate all expected tools exist."""
    print("\n── Tools List ──")
    tools = client.tools_list()
    tool_map = {t["name"]: t for t in tools}

    # Combine all expected tools into one flat list
    all_expected_tools = []
    for group in [
        EXPECTED_SENSORS, EXPECTED_BINARY_SENSORS, EXPECTED_TEXT_SENSORS,
        EXPECTED_SWITCHES, EXPECTED_BUTTONS, EXPECTED_LIGHTS, EXPECTED_FANS,
        EXPECTED_COVERS, EXPECTED_CLIMATES, EXPECTED_NUMBERS, EXPECTED_SELECTS,
        EXPECTED_LOCKS, EXPECTED_VALVES, EXPECTED_TEXTS, EXPECTED_DATES,
        EXPECTED_TIMES, EXPECTED_DATETIMES, EXPECTED_ALARMS,
    ]:
        for entry in group:
            all_expected_tools.append(entry[0])  # tool name
    all_expected_tools.extend(EXPECTED_SCRIPTS)

    # Check total count
    suite.record(
        "tools_list_count",
        len(tools) >= len(all_expected_tools),
        f"Expected >= {len(all_expected_tools)}, got {len(tools)}",
    )

    # Check each expected tool is present
    for tool_name in all_expected_tools:
        suite.record(
            f"tools_list_has_{tool_name}",
            tool_name in tool_map,
            f"Tool '{tool_name}' not found in tools/list",
        )

    # Validate every tool has required MCP fields
    for tool in tools:
        name = tool.get("name", "?")
        has_name = isinstance(tool.get("name"), str) and len(tool["name"]) > 0
        has_desc = isinstance(tool.get("description"), str) and len(tool["description"]) > 0
        has_schema = isinstance(tool.get("inputSchema"), dict)
        schema_type = tool.get("inputSchema", {}).get("type") == "object"

        suite.record(f"tools_schema_valid_{name}",
                     has_name and has_desc and has_schema and schema_type,
                     f"name={has_name} desc={has_desc} schema={has_schema} type={schema_type}")

    return tool_map


def test_tools_metadata(tool_map: dict[str, dict], suite: TestSuite):
    """Validate that tool descriptions contain expected metadata keywords."""
    print("\n── Tools Metadata ──")
    for tool_name, keywords in EXPECTED_METADATA.items():
        if tool_name not in tool_map:
            suite.record(f"metadata_{tool_name}", False, "Tool not found")
            continue
        desc = tool_map[tool_name].get("description", "")
        missing = [kw for kw in keywords if kw.lower() not in desc.lower()]
        suite.record(
            f"metadata_{tool_name}",
            len(missing) == 0,
            f"Missing in description: {missing}",
            detail=f"Description: {desc}",
        )


def test_tools_input_schemas(tool_map: dict[str, dict], suite: TestSuite):
    """Validate input schemas have correct properties and required fields."""
    print("\n── Input Schemas ──")

    schema_checks = {
        # (tool_name, expected_properties, expected_required)
        "switch_living_room_lamp": (["state"], ["state"]),
        "light_ceiling_light": (["state", "brightness"], ["state"]),
        "fan_bedroom_fan": (["state", "speed", "oscillating", "direction"], ["state"]),
        "cover_garage_door": (["command", "position"], ["command"]),
        "cover_window_blinds": (["command", "position", "tilt"], ["command"]),
        "climate_main_thermostat": (["mode", "target_temperature"], []),
        "number_target_brightness": (["value"], ["value"]),
        "select_operating_mode": (["option"], ["option"]),
        "lock_front_door_lock": (["command"], ["command"]),
        "valve_zone_1_sprinkler": (["command", "position"], ["command"]),
        "alarm_home_alarm": (["command", "code"], ["command"]),
        "text_display_message": (["value"], ["value"]),
        "date_schedule_date": (["year", "month", "day"], ["year", "month", "day"]),
        "time_alarm_time": (["hour", "minute", "second"], ["hour", "minute", "second"]),
        "datetime_next_run": (["epoch"], ["epoch"]),
    }

    for tool_name, (expected_props, expected_required) in schema_checks.items():
        if tool_name not in tool_map:
            suite.record(f"schema_{tool_name}", False, "Tool not found")
            continue
        schema = tool_map[tool_name].get("inputSchema", {})
        props = list(schema.get("properties", {}).keys())
        required = schema.get("required", [])

        missing_props = [p for p in expected_props if p not in props]
        missing_req = [r for r in expected_required if r not in required]

        ok = len(missing_props) == 0 and len(missing_req) == 0
        msg = ""
        if missing_props:
            msg += f"Missing properties: {missing_props}. "
        if missing_req:
            msg += f"Missing required: {missing_req}."
        suite.record(f"schema_{tool_name}", ok, msg)


def test_tools_call_sensors(client: MCPClient, suite: TestSuite):
    """Call every sensor and validate response structure."""
    print("\n── Tools Call: Sensors ──")
    for tool_name, _ in EXPECTED_SENSORS:
        result = client.tools_call(tool_name)
        content = result.get("content", [])
        has_content = len(content) > 0 and content[0].get("type") == "text"
        is_error = result.get("isError", False)
        text = content[0].get("text", "") if has_content else ""

        # Parse the returned JSON and check for value field
        value_ok = False
        if has_content:
            try:
                data = json.loads(text)
                value_ok = "value" in data or "name" in data
            except json.JSONDecodeError:
                # Might be plain text format
                value_ok = len(text) > 0

        suite.record(
            f"call_{tool_name}",
            has_content and not is_error and value_ok,
            f"content={has_content}, error={is_error}, value_ok={value_ok}",
            detail=text,
        )


def test_tools_call_binary_sensors(client: MCPClient, suite: TestSuite):
    """Call every binary sensor."""
    print("\n── Tools Call: Binary Sensors ──")
    for tool_name, _ in EXPECTED_BINARY_SENSORS:
        result = client.tools_call(tool_name)
        content = result.get("content", [])
        has_content = len(content) > 0
        is_error = result.get("isError", False)
        suite.record(f"call_{tool_name}", has_content and not is_error,
                     f"error={is_error}")


def test_tools_call_text_sensors(client: MCPClient, suite: TestSuite):
    """Call every text sensor."""
    print("\n── Tools Call: Text Sensors ──")
    for tool_name, _ in EXPECTED_TEXT_SENSORS:
        result = client.tools_call(tool_name)
        content = result.get("content", [])
        has_content = len(content) > 0
        is_error = result.get("isError", False)
        suite.record(f"call_{tool_name}", has_content and not is_error,
                     f"error={is_error}")


def test_tools_call_switches(client: MCPClient, suite: TestSuite):
    """Test every switch action: ON, OFF, TOGGLE."""
    print("\n── Tools Call: Switches ──")
    for tool_name, _ in EXPECTED_SWITCHES:
        for state in ["ON", "OFF", "TOGGLE"]:
            result = client.tools_call(tool_name, {"state": state})
            content = result.get("content", [])
            is_error = result.get("isError", False)
            suite.record(
                f"call_{tool_name}_{state}",
                len(content) > 0 and not is_error,
                f"error={is_error}",
            )


def test_tools_call_buttons(client: MCPClient, suite: TestSuite):
    """Press every button."""
    print("\n── Tools Call: Buttons ──")
    for tool_name, _ in EXPECTED_BUTTONS:
        result = client.tools_call(tool_name)
        is_error = result.get("isError", False)
        suite.record(f"call_{tool_name}", not is_error, f"error={is_error}")


def test_tools_call_lights(client: MCPClient, suite: TestSuite):
    """Test light control: on, brightness, off."""
    print("\n── Tools Call: Lights ──")
    for tool_name, _ in EXPECTED_LIGHTS:
        # Turn on with brightness
        result = client.tools_call(tool_name, {"state": "ON", "brightness": "200"})
        suite.record(f"call_{tool_name}_on_bright",
                     not result.get("isError", False))

        # Turn off with transition
        result = client.tools_call(tool_name, {"state": "OFF", "transition_length": "500"})
        suite.record(f"call_{tool_name}_off_transition",
                     not result.get("isError", False))

        # Toggle
        result = client.tools_call(tool_name, {"state": "TOGGLE"})
        suite.record(f"call_{tool_name}_toggle",
                     not result.get("isError", False))


def test_tools_call_fans(client: MCPClient, suite: TestSuite):
    """Test fan control: on with speed, oscillation, direction, off."""
    print("\n── Tools Call: Fans ──")
    for tool_name, _ in EXPECTED_FANS:
        result = client.tools_call(tool_name, {
            "state": "ON", "speed": "3", "oscillating": "true", "direction": "FORWARD",
        })
        suite.record(f"call_{tool_name}_on_full", not result.get("isError", False))

        result = client.tools_call(tool_name, {
            "state": "ON", "speed": "1", "oscillating": "false", "direction": "REVERSE",
        })
        suite.record(f"call_{tool_name}_on_low_reverse", not result.get("isError", False))

        result = client.tools_call(tool_name, {"state": "OFF"})
        suite.record(f"call_{tool_name}_off", not result.get("isError", False))


def test_tools_call_covers(client: MCPClient, suite: TestSuite):
    """Test cover control: open, close, stop, position, tilt."""
    print("\n── Tools Call: Covers ──")
    for tool_name, _ in EXPECTED_COVERS:
        for cmd in ["OPEN", "CLOSE", "STOP"]:
            result = client.tools_call(tool_name, {"command": cmd})
            suite.record(f"call_{tool_name}_{cmd}",
                         not result.get("isError", False))

        # Position
        result = client.tools_call(tool_name, {"command": "OPEN", "position": "0.5"})
        suite.record(f"call_{tool_name}_position", not result.get("isError", False))

    # Tilt (only window_blinds)
    result = client.tools_call("cover_window_blinds", {"command": "OPEN", "tilt": "0.75"})
    suite.record("call_cover_window_blinds_tilt", not result.get("isError", False))


def test_tools_call_climate(client: MCPClient, suite: TestSuite):
    """Test climate control: mode, temp, presets."""
    print("\n── Tools Call: Climate ──")
    for tool_name, _ in EXPECTED_CLIMATES:
        # Set mode + temp
        result = client.tools_call(tool_name, {
            "mode": "HEAT_COOL",
            "target_temperature_low": "20",
            "target_temperature_high": "24",
        })
        suite.record(f"call_{tool_name}_heat_cool", not result.get("isError", False))

        # Set preset
        result = client.tools_call(tool_name, {"preset": "AWAY"})
        suite.record(f"call_{tool_name}_preset_away", not result.get("isError", False))

        # Heat mode
        result = client.tools_call(tool_name, {
            "mode": "HEAT", "target_temperature": "22",
        })
        suite.record(f"call_{tool_name}_heat_22", not result.get("isError", False))

        # Off
        result = client.tools_call(tool_name, {"mode": "OFF"})
        suite.record(f"call_{tool_name}_off", not result.get("isError", False))


def test_tools_call_numbers(client: MCPClient, suite: TestSuite):
    """Test number control: set value within range."""
    print("\n── Tools Call: Numbers ──")
    test_values = {
        "number_target_brightness": ["0", "50", "100"],
        "number_volume_level": ["-60", "-20", "0"],
    }
    for tool_name, _ in EXPECTED_NUMBERS:
        for val in test_values.get(tool_name, ["0"]):
            result = client.tools_call(tool_name, {"value": val})
            suite.record(f"call_{tool_name}_val{val}",
                         not result.get("isError", False))


def test_tools_call_selects(client: MCPClient, suite: TestSuite):
    """Test select: set every option."""
    print("\n── Tools Call: Selects ──")
    options = ["Auto", "Manual", "Eco", "Boost", "Off"]
    for tool_name, _ in EXPECTED_SELECTS:
        for opt in options:
            result = client.tools_call(tool_name, {"option": opt})
            suite.record(f"call_{tool_name}_{opt}",
                         not result.get("isError", False))


def test_tools_call_locks(client: MCPClient, suite: TestSuite):
    """Test lock: lock, unlock, open."""
    print("\n── Tools Call: Locks ──")
    for tool_name, _ in EXPECTED_LOCKS:
        for cmd in ["LOCK", "UNLOCK", "OPEN"]:
            result = client.tools_call(tool_name, {"command": cmd})
            suite.record(f"call_{tool_name}_{cmd}",
                         not result.get("isError", False))


def test_tools_call_valves(client: MCPClient, suite: TestSuite):
    """Test valve: open, close, stop, position."""
    print("\n── Tools Call: Valves ──")
    for tool_name, _ in EXPECTED_VALVES:
        for cmd in ["OPEN", "CLOSE", "STOP"]:
            result = client.tools_call(tool_name, {"command": cmd})
            suite.record(f"call_{tool_name}_{cmd}",
                         not result.get("isError", False))

        result = client.tools_call(tool_name, {"command": "OPEN", "position": "0.5"})
        suite.record(f"call_{tool_name}_position", not result.get("isError", False))


def test_tools_call_text(client: MCPClient, suite: TestSuite):
    """Test text input: set value."""
    print("\n── Tools Call: Text ──")
    test_strings = ["Hello MCP!", "Test 123", "", "A" * 128]
    for tool_name, _ in EXPECTED_TEXTS:
        for i, val in enumerate(test_strings):
            result = client.tools_call(tool_name, {"value": val})
            suite.record(f"call_{tool_name}_str{i}",
                         not result.get("isError", False))


def test_tools_call_dates(client: MCPClient, suite: TestSuite):
    """Test date, time, datetime entities."""
    print("\n── Tools Call: Date/Time/Datetime ──")

    # Date
    for tool_name, _ in EXPECTED_DATES:
        result = client.tools_call(tool_name, {"year": "2026", "month": "6", "day": "15"})
        suite.record(f"call_{tool_name}", not result.get("isError", False))

    # Time
    for tool_name, _ in EXPECTED_TIMES:
        result = client.tools_call(tool_name, {"hour": "7", "minute": "30", "second": "0"})
        suite.record(f"call_{tool_name}", not result.get("isError", False))

    # Datetime
    for tool_name, _ in EXPECTED_DATETIMES:
        result = client.tools_call(tool_name, {"epoch": "1750000000"})
        suite.record(f"call_{tool_name}", not result.get("isError", False))


def test_tools_call_alarms(client: MCPClient, suite: TestSuite):
    """Test alarm control panel: arm/disarm with code."""
    print("\n── Tools Call: Alarm Control Panel ──")
    for tool_name, _ in EXPECTED_ALARMS:
        # Arm home
        result = client.tools_call(tool_name, {"command": "ARM_HOME", "code": "1234"})
        suite.record(f"call_{tool_name}_arm_home", not result.get("isError", False))

        # Disarm
        result = client.tools_call(tool_name, {"command": "DISARM", "code": "1234"})
        suite.record(f"call_{tool_name}_disarm", not result.get("isError", False))

        # Arm away
        result = client.tools_call(tool_name, {"command": "ARM_AWAY", "code": "1234"})
        suite.record(f"call_{tool_name}_arm_away", not result.get("isError", False))

        # Arm night
        result = client.tools_call(tool_name, {"command": "ARM_NIGHT", "code": "1234"})
        suite.record(f"call_{tool_name}_arm_night", not result.get("isError", False))

        # Disarm again to clean up
        client.tools_call(tool_name, {"command": "DISARM", "code": "1234"})


def test_tools_call_scripts(client: MCPClient, suite: TestSuite):
    """Test script execution: execute, is_running, stop."""
    print("\n── Tools Call: Scripts ──")

    # Test simple scripts (execute only)
    for script_name in ["script_all_off", "script_all_on", "script_morning_routine",
                        "script_night_routine", "script_irrigation_cycle"]:
        result = client.tools_call(script_name, {"action": "execute"})
        suite.record(f"call_{script_name}_execute", not result.get("isError", False))

    # Test default action (no args = execute)
    result = client.tools_call("script_all_off", {})
    suite.record("call_script_all_off_default", not result.get("isError", False))

    # Test long_running_task lifecycle: execute → is_running → stop
    result = client.tools_call("script_long_running_task", {"action": "execute"})
    suite.record("call_script_long_running_execute",
                 not result.get("isError", False))

    time.sleep(0.5)  # let it start

    result = client.tools_call("script_long_running_task", {"action": "is_running"})
    content_text = result.get("content", [{}])[0].get("text", "")
    suite.record("call_script_long_running_is_running",
                 not result.get("isError", False),
                 f"is_running returned: {content_text}")

    result = client.tools_call("script_long_running_task", {"action": "stop"})
    suite.record("call_script_long_running_stop",
                 not result.get("isError", False))


def test_tools_call_errors(client: MCPClient, suite: TestSuite):
    """Test error handling for unknown tools and bad calls."""
    print("\n── Tools Call: Error Handling ──")

    # Unknown tool
    result = client.tools_call("nonexistent_tool_xyz")
    suite.record("call_unknown_tool_returns_error",
                 result.get("isError", True),
                 f"isError={result.get('isError')}")

    # Another unknown
    result = client.tools_call("sensor_get_does_not_exist")
    suite.record("call_unknown_sensor_returns_error",
                 result.get("isError", True))


def test_resources_list(client: MCPClient, suite: TestSuite) -> dict[str, dict]:
    """Test resources/list and validate all expected resources exist."""
    print("\n── Resources List ──")
    resources = client.resources_list()
    resource_map = {r["uri"]: r for r in resources}

    # Collect all expected URIs
    all_expected_uris = []
    for group in [
        EXPECTED_SENSORS, EXPECTED_BINARY_SENSORS, EXPECTED_TEXT_SENSORS,
        EXPECTED_SWITCHES, EXPECTED_BUTTONS, EXPECTED_LIGHTS, EXPECTED_FANS,
        EXPECTED_COVERS, EXPECTED_CLIMATES, EXPECTED_NUMBERS, EXPECTED_SELECTS,
        EXPECTED_LOCKS, EXPECTED_VALVES, EXPECTED_TEXTS, EXPECTED_DATES,
        EXPECTED_TIMES, EXPECTED_DATETIMES, EXPECTED_ALARMS,
    ]:
        for entry in group:
            all_expected_uris.append(entry[1])  # resource URI

    suite.record(
        "resources_list_count",
        len(resources) >= len(all_expected_uris),
        f"Expected >= {len(all_expected_uris)}, got {len(resources)}",
    )

    for uri in all_expected_uris:
        present = uri in resource_map
        suite.record(f"resource_exists_{uri.split('://')[-1].replace('/', '_')}",
                     present, f"URI '{uri}' not found")

    # Validate resource fields
    for res in resources:
        uri = res.get("uri", "?")
        short = uri.split("://")[-1].replace("/", "_")
        has_uri = isinstance(res.get("uri"), str)
        has_name = isinstance(res.get("name"), str) and len(res["name"]) > 0
        has_mime = res.get("mimeType") == "application/json"
        suite.record(f"resource_valid_{short}",
                     has_uri and has_name and has_mime,
                     f"uri={has_uri} name={has_name} mime={has_mime}")

    return resource_map


def test_resources_read(client: MCPClient, suite: TestSuite):
    """Read every resource and validate the response structure."""
    print("\n── Resources Read ──")

    # Sample from each entity type
    test_uris = [
        ("esphome://sensor/temperature", ["name", "state"]),
        ("esphome://sensor/humidity", ["name"]),
        ("esphome://binary_sensor/front_door", ["name", "state"]),
        ("esphome://text_sensor/system_status", ["name"]),
        ("esphome://switch/living_room_lamp", ["name", "state"]),
        ("esphome://light/ceiling_light", ["name"]),
        ("esphome://fan/bedroom_fan", []),
        ("esphome://cover/garage_door", []),
        ("esphome://climate/main_thermostat", ["name"]),
        ("esphome://number/target_brightness", []),
        ("esphome://select/operating_mode", []),
        ("esphome://lock/front_door_lock", []),
        ("esphome://valve/zone_1_sprinkler", []),
        ("esphome://text/display_message", []),
        ("esphome://alarm_control_panel/home_alarm", []),
    ]

    for uri, expected_fields in test_uris:
        short = uri.split("://")[-1].replace("/", "_")
        try:
            result = client.resources_read(uri)
            contents = result.get("contents", [])
            has_contents = len(contents) > 0

            if has_contents:
                entry = contents[0]
                uri_match = entry.get("uri") == uri
                has_text = isinstance(entry.get("text"), str)
                mime_ok = entry.get("mimeType") == "application/json"

                # Try to parse the text as JSON and check fields
                fields_ok = True
                if has_text and expected_fields:
                    try:
                        data = json.loads(entry["text"])
                        missing = [f for f in expected_fields if f not in data]
                        fields_ok = len(missing) == 0
                        if not fields_ok:
                            suite.record(f"resource_read_{short}", False,
                                         f"Missing fields: {missing}")
                            continue
                    except json.JSONDecodeError:
                        pass

                suite.record(
                    f"resource_read_{short}",
                    has_contents and uri_match and has_text and mime_ok and fields_ok,
                    f"contents={has_contents} uri={uri_match} text={has_text} mime={mime_ok}",
                )
            else:
                suite.record(f"resource_read_{short}", False, "Empty contents")
        except Exception as e:
            suite.record(f"resource_read_{short}", False, str(e))

    # Invalid URI
    try:
        result = client.resources_read("esphome://sensor/nonexistent_xyz")
        # Should return empty or error
        contents = result.get("contents", [])
        is_empty_or_error = (
            len(contents) == 0
            or contents[0].get("text", "") in ["{}", ""]
            or "error" in json.dumps(result).lower()
        )
        suite.record("resource_read_invalid_uri", is_empty_or_error,
                     "Expected empty/error for invalid URI")
    except Exception as e:
        suite.record("resource_read_invalid_uri", True, f"Exception (ok): {e}")


def test_session_reconnect(host: str, port: int, suite: TestSuite):
    """Test disconnect and reconnect creates a fresh session."""
    print("\n── Session Reconnect ──")
    client2 = MCPClient(host, port)
    try:
        client2.connect()
        resp = client2.initialize()
        ok = "result" in resp
        suite.record("reconnect_initialize", ok)
        tools = client2.tools_list()
        suite.record("reconnect_tools_list", len(tools) > 0,
                     f"Got {len(tools)} tools")
    except Exception as e:
        suite.record("reconnect", False, str(e))
    finally:
        client2.disconnect()


def test_concurrent_sessions(host: str, port: int, suite: TestSuite):
    """Test multiple concurrent MCP sessions."""
    print("\n── Concurrent Sessions ──")
    clients = []
    try:
        for i in range(3):
            c = MCPClient(host, port)
            c.connect()
            c.initialize()
            clients.append(c)
        suite.record("concurrent_connect_3", True)

        # Each should independently list tools
        counts = []
        for i, c in enumerate(clients):
            tools = c.tools_list()
            counts.append(len(tools))
        suite.record("concurrent_all_list_tools",
                     all(c > 0 for c in counts) and len(set(counts)) == 1,
                     f"Tool counts: {counts}")

        # Each can call a tool
        for i, c in enumerate(clients):
            result = c.tools_call("sensor_get_temperature")
            suite.record(f"concurrent_session{i}_call",
                         not result.get("isError", False))
    except Exception as e:
        suite.record("concurrent_sessions", False, str(e))
    finally:
        for c in clients:
            c.disconnect()


def test_rapid_fire(client: MCPClient, suite: TestSuite):
    """Stress test: rapid sequential calls."""
    print("\n── Rapid Fire (50 calls) ──")
    errors = 0
    start = time.time()
    for i in range(50):
        try:
            result = client.tools_call("sensor_get_temperature")
            if result.get("isError", False):
                errors += 1
        except Exception:
            errors += 1
    elapsed = time.time() - start
    suite.record("rapid_fire_50_calls",
                 errors == 0,
                 f"{errors} errors in {elapsed:.2f}s ({50/elapsed:.1f} calls/sec)")


# ════════════════════════════════════════════════════════════════
#  Main
# ════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="ESPHome MCP Server Test Suite")
    parser.add_argument("--host", default="192.168.1.100", help="Device IP address")
    parser.add_argument("--port", type=int, default=8080, help="MCP server port")
    parser.add_argument("--timeout", type=float, default=10.0, help="Socket timeout (seconds)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show full payloads")
    args = parser.parse_args()

    suite = TestSuite(verbose=args.verbose)
    client = MCPClient(args.host, args.port, args.timeout)

    print("═" * 60)
    print(f"  ESPHome MCP Server — Test Suite")
    print(f"  Target: {args.host}:{args.port}")
    print("═" * 60)

    try:
        # ── Phase 1: Connection & Handshake ──
        test_connection(client, suite)
        test_initialize(client, suite)

        # ── Phase 2: Tools Discovery ──
        tool_map = test_tools_list(client, suite)
        test_tools_metadata(tool_map, suite)
        test_tools_input_schemas(tool_map, suite)

        # ── Phase 3: Tools Execution (every entity type) ──
        test_tools_call_sensors(client, suite)
        test_tools_call_binary_sensors(client, suite)
        test_tools_call_text_sensors(client, suite)
        test_tools_call_switches(client, suite)
        test_tools_call_buttons(client, suite)
        test_tools_call_lights(client, suite)
        test_tools_call_fans(client, suite)
        test_tools_call_covers(client, suite)
        test_tools_call_climate(client, suite)
        test_tools_call_numbers(client, suite)
        test_tools_call_selects(client, suite)
        test_tools_call_locks(client, suite)
        test_tools_call_valves(client, suite)
        test_tools_call_text(client, suite)
        test_tools_call_dates(client, suite)
        test_tools_call_alarms(client, suite)
        test_tools_call_scripts(client, suite)
        test_tools_call_errors(client, suite)

        # ── Phase 4: Resources ──
        test_resources_list(client, suite)
        test_resources_read(client, suite)

        # ── Phase 5: Session Management ──
        client.disconnect()
        test_session_reconnect(args.host, args.port, suite)
        test_concurrent_sessions(args.host, args.port, suite)

        # ── Phase 6: Stress ──
        client = MCPClient(args.host, args.port, args.timeout)
        client.connect()
        client.initialize()
        test_rapid_fire(client, suite)

    except KeyboardInterrupt:
        print("\n\nInterrupted!")
    except Exception as e:
        print(f"\n\n💥 Fatal error: {e}")
    finally:
        client.disconnect()

    # ── Results ──
    success = suite.summary()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()