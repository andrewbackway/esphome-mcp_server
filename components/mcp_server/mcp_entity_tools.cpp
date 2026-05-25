#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "mcp_entity_tools.h"
#include "esphome/core/application.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/log.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_LIGHT
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_traits.h"
#endif
#ifdef USE_FAN
#include "esphome/components/fan/fan.h"
#endif
#ifdef USE_COVER
#include "esphome/components/cover/cover.h"
#endif
#ifdef USE_CLIMATE
#include "esphome/components/climate/climate.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_LOCK
#include "esphome/components/lock/lock.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_MEDIA_PLAYER
#include "esphome/components/media_player/media_player.h"
#endif
#ifdef USE_ALARM_CONTROL_PANEL
#include "esphome/components/alarm_control_panel/alarm_control_panel.h"
#endif
#ifdef USE_EVENT
#include "esphome/components/event/event.h"
#endif
#ifdef USE_VALVE
#include "esphome/components/valve/valve.h"
#endif
#ifdef USE_UPDATE
#include "esphome/components/update/update_entity.h"
#endif
#ifdef USE_DATETIME_DATE
#include "esphome/components/datetime/date_entity.h"
#endif
#ifdef USE_DATETIME_TIME
#include "esphome/components/datetime/time_entity.h"
#endif
#ifdef USE_DATETIME_DATETIME
#include "esphome/components/datetime/datetime_entity.h"
#endif
#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif
#ifdef USE_SCRIPT
#include "esphome/components/script/script.h"
#endif

namespace esphome {
namespace mcp_server {

static const char *TAG = "mcp_server.tools";

// ────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────

static bool should_include(const std::string &type, bool auto_discover,
                           const std::vector<std::string> &filters) {
  if (!auto_discover) return false;
  if (filters.empty()) return true;
  for (const auto &f : filters)
    if (f == type) return true;
  return false;
}

/// Extract common EntityBase metadata
static void add_base_meta(JsonBuilder &j, EntityBase *e, const std::string &entity_type) {
  j.key_str("entity_type", entity_type);
  j.key_str("object_id", e->get_object_id());
  j.key_str("name", e->get_name());
  if (!e->get_icon().empty())
    j.key_str("icon", e->get_icon());
  j.key_bool("is_internal", e->is_internal());
  j.key_bool("disabled_by_default", e->is_disabled_by_default());
  // entity_category: 0=None, 1=Config, 2=Diagnostic
  auto cat = e->get_entity_category();
  if (cat == ENTITY_CATEGORY_CONFIG)
    j.key_str("entity_category", "config");
  else if (cat == ENTITY_CATEGORY_DIAGNOSTIC)
    j.key_str("entity_category", "diagnostic");
}

/// Argument extractor — handles spaces around ':' and both string and numeric values.
/// Python's json.dumps() produces "key": "value" (space after colon), so we must skip
/// whitespace when locating the value.
static std::string get_arg(const std::string &json, const std::string &key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";

  pos += needle.size();
  // skip whitespace then expect ':'
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != ':') return "";
  pos++;
  // skip whitespace after ':'
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= json.size()) return "";

  if (json[pos] == '"') {
    // string value — return content between the quotes (no unescape needed for our args)
    pos++;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
  }
  // numeric / boolean value — return until next delimiter
  auto end = json.find_first_of(",}]", pos);
  if (end == std::string::npos) end = json.size();
  return json.substr(pos, end - pos);
}

/// Safe numeric conversions — never throw (ESP-IDF has exceptions disabled → abort()).
static float  safe_stof_(const std::string &s) { return s.empty() ? 0.0f : strtof(s.c_str(), nullptr); }
static int    safe_stoi_(const std::string &s) { return s.empty() ? 0    : (int)strtol(s.c_str(), nullptr, 10); }
static unsigned long safe_stoul_(const std::string &s) { return s.empty() ? 0UL : strtoul(s.c_str(), nullptr, 10); }

static std::string json_string_escape_(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

static std::string tool_result(const std::string &text) {
  return R"({"content":[{"type":"text","text":")" + json_string_escape_(text) + R"("}]})";
}
static std::string tool_error(const std::string &text) {
  return R"({"content":[{"type":"text","text":")" + json_string_escape_(text) + R"("}],"isError":true})";
}

// ════════════════════════════════════════════════════════════════
//  TOOLS LIST — every entity type with full metadata in description
// ════════════════════════════════════════════════════════════════

std::string build_tools_list(bool auto_discover, bool expose_scripts,
                             const std::vector<std::string> &type_filters) {
  JsonBuilder j;
  j.raw(R"({"tools":[)");
  bool first = true;

  auto comma = [&]() { if (!first) j.raw(","); first = false; };

  // ── Helper: build a rich description string with all metadata ──
  auto rich_desc = [](const std::string &base, const std::vector<std::pair<std::string,std::string>> &meta) {
    std::string d = base;
    for (auto &kv : meta) {
      if (!kv.second.empty()) d += " | " + kv.first + ": " + kv.second;
    }
    return d;
  };

  // ────────── SENSOR ──────────
#ifdef USE_SENSOR
  if (should_include("sensor", auto_discover, type_filters)) {
    for (auto *e : App.get_sensors()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Read sensor: " + e->get_name(), {
        {"unit", e->get_unit_of_measurement()},
        {"accuracy_decimals", std::to_string(e->get_accuracy_decimals())},
        {"device_class", e->get_device_class()},
        {"state_class", 
          e->get_state_class() == sensor::STATE_CLASS_MEASUREMENT ? "measurement" :
          e->get_state_class() == sensor::STATE_CLASS_TOTAL_INCREASING ? "total_increasing" :
          e->get_state_class() == sensor::STATE_CLASS_TOTAL ? "total" : ""},
        {"icon", e->get_icon()},
      });
      j.raw(R"({"name":"sensor_get_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{}}})");
    }
  }
#endif

  // ────────── BINARY SENSOR ──────────
#ifdef USE_BINARY_SENSOR
  if (should_include("binary_sensor", auto_discover, type_filters)) {
    for (auto *e : App.get_binary_sensors()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Read binary sensor: " + e->get_name(), {
        {"device_class", e->get_device_class()},
        {"icon", e->get_icon()},
      });
      j.raw(R"({"name":"binary_sensor_get_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{}}})");
    }
  }
#endif

  // ────────── TEXT SENSOR ──────────
#ifdef USE_TEXT_SENSOR
  if (should_include("text_sensor", auto_discover, type_filters)) {
    for (auto *e : App.get_text_sensors()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Read text sensor: " + e->get_name(), {
        {"device_class", e->get_device_class()},
        {"icon", e->get_icon()},
      });
      j.raw(R"({"name":"text_sensor_get_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{}}})");
    }
  }
#endif

  // ────────── SWITCH ──────────
#ifdef USE_SWITCH
  if (should_include("switch", auto_discover, type_filters)) {
    for (auto *e : App.get_switches()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Control switch: " + e->get_name(), {
        {"device_class", e->get_device_class()},
        {"icon", e->get_icon()},
        {"assumed_state", e->assumed_state() ? "true" : "false"},
      });
      j.raw(R"({"name":"switch_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("state":{"type":"string","enum":["ON","OFF","TOGGLE"],"description":"Desired switch state"})");
      j.raw(R"(},"required":["state"]}})");
    }
  }
#endif

  // ────────── BUTTON ──────────
#ifdef USE_BUTTON
  if (should_include("button", auto_discover, type_filters)) {
    for (auto *e : App.get_buttons()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Press button: " + e->get_name(), {
        {"device_class", e->get_device_class()},
        {"icon", e->get_icon()},
      });
      j.raw(R"({"name":"button_press_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{}}})");
    }
  }
#endif

  // ────────── LIGHT ──────────
#ifdef USE_LIGHT
  if (should_include("light", auto_discover, type_filters)) {
    for (auto *e : App.get_lights()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->get_traits();
      std::string caps;
      if (traits.supports_color_mode(light::ColorMode::RGB)) caps += "rgb ";
      if (traits.supports_color_mode(light::ColorMode::COLOR_TEMPERATURE)) caps += "color_temp ";
      if (traits.supports_color_mode(light::ColorMode::WHITE)) caps += "white ";
      if (traits.supports_color_mode(light::ColorMode::COLD_WARM_WHITE)) caps += "cwww ";
      std::string desc = rich_desc("Control light: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"color_modes", caps},
        {"min_mireds", traits.get_min_mireds() > 0 ? std::to_string((int)traits.get_min_mireds()) : ""},
        {"max_mireds", traits.get_max_mireds() > 0 ? std::to_string((int)traits.get_max_mireds()) : ""},
        {"effects_count", std::to_string(e->get_effects().size())},
      });
      j.raw(R"({"name":"light_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("state":{"type":"string","enum":["ON","OFF","TOGGLE"]},)");
      j.raw(R"("brightness":{"type":"number","minimum":0,"maximum":255,"description":"Brightness 0-255"},)");
      j.raw(R"("color_temp":{"type":"number","description":"Color temperature in mireds"},)");
      j.raw(R"("r":{"type":"number","minimum":0,"maximum":255},)");
      j.raw(R"("g":{"type":"number","minimum":0,"maximum":255},)");
      j.raw(R"("b":{"type":"number","minimum":0,"maximum":255},)");
      j.raw(R"("effect":{"type":"string","description":"Effect name to activate"},)");
      j.raw(R"("transition_length":{"type":"number","description":"Transition in ms"})");
      j.raw(R"(},"required":["state"]}})");
    }
  }
#endif

  // ────────── FAN ──────────
#ifdef USE_FAN
  if (should_include("fan", auto_discover, type_filters)) {
    for (auto *e : App.get_fans()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->get_traits();
      std::string desc = rich_desc("Control fan: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"supports_speed", traits.supports_speed() ? "true" : "false"},
        {"supports_oscillation", traits.supports_oscillation() ? "true" : "false"},
        {"supports_direction", traits.supports_direction() ? "true" : "false"},
        {"speed_count", std::to_string(traits.supported_speed_count())},
      });
      j.raw(R"({"name":"fan_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("state":{"type":"string","enum":["ON","OFF","TOGGLE"]},)");
      j.raw(R"("speed":{"type":"integer","minimum":1,"maximum":)" + std::to_string(traits.supported_speed_count()) + R"(},)");
      j.raw(R"("oscillating":{"type":"boolean"},)");
      j.raw(R"("direction":{"type":"string","enum":["FORWARD","REVERSE"]})");
      j.raw(R"(},"required":["state"]}})");
    }
  }
#endif

  // ────────── COVER ──────────
#ifdef USE_COVER
  if (should_include("cover", auto_discover, type_filters)) {
    for (auto *e : App.get_covers()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->get_traits();
      std::string desc = rich_desc("Control cover: " + e->get_name(), {
        {"device_class", e->get_device_class()},
        {"icon", e->get_icon()},
        {"supports_position", traits.get_supports_position() ? "true" : "false"},
        {"supports_tilt", traits.get_supports_tilt() ? "true" : "false"},
        {"supports_stop", traits.get_supports_stop() ? "true" : "false"},
      });
      j.raw(R"({"name":"cover_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("command":{"type":"string","enum":["OPEN","CLOSE","STOP"]},)");
      j.raw(R"("position":{"type":"number","minimum":0,"maximum":1,"description":"0=closed, 1=open"},)");
      j.raw(R"("tilt":{"type":"number","minimum":0,"maximum":1})");
      j.raw(R"(},"required":["command"]}})");
    }
  }
#endif

  // ────────── CLIMATE ──────────
#ifdef USE_CLIMATE
  if (should_include("climate", auto_discover, type_filters)) {
    for (auto *e : App.get_climates()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->get_traits();

      // Build supported modes list
      std::string modes;
      for (auto m : traits.get_supported_modes()) {
        switch (m) {
          case climate::CLIMATE_MODE_OFF: modes += "OFF,"; break;
          case climate::CLIMATE_MODE_HEAT_COOL: modes += "HEAT_COOL,"; break;
          case climate::CLIMATE_MODE_COOL: modes += "COOL,"; break;
          case climate::CLIMATE_MODE_HEAT: modes += "HEAT,"; break;
          case climate::CLIMATE_MODE_DRY: modes += "DRY,"; break;
          case climate::CLIMATE_MODE_FAN_ONLY: modes += "FAN_ONLY,"; break;
          case climate::CLIMATE_MODE_AUTO: modes += "AUTO,"; break;
          default: break;
        }
      }
      std::string fan_modes;
      for (auto fm : traits.get_supported_fan_modes()) {
        switch (fm) {
          case climate::CLIMATE_FAN_LOW: fan_modes += "LOW,"; break;
          case climate::CLIMATE_FAN_MEDIUM: fan_modes += "MEDIUM,"; break;
          case climate::CLIMATE_FAN_HIGH: fan_modes += "HIGH,"; break;
          case climate::CLIMATE_FAN_AUTO: fan_modes += "AUTO,"; break;
          case climate::CLIMATE_FAN_QUIET: fan_modes += "QUIET,"; break;
          default: break;
        }
      }
      std::string presets;
      for (auto p : traits.get_supported_presets()) {
        switch (p) {
          case climate::CLIMATE_PRESET_HOME: presets += "HOME,"; break;
          case climate::CLIMATE_PRESET_AWAY: presets += "AWAY,"; break;
          case climate::CLIMATE_PRESET_BOOST: presets += "BOOST,"; break;
          case climate::CLIMATE_PRESET_COMFORT: presets += "COMFORT,"; break;
          case climate::CLIMATE_PRESET_ECO: presets += "ECO,"; break;
          case climate::CLIMATE_PRESET_SLEEP: presets += "SLEEP,"; break;
          case climate::CLIMATE_PRESET_ACTIVITY: presets += "ACTIVITY,"; break;
          default: break;
        }
      }

      std::string desc = rich_desc("Control climate: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"modes", modes},
        {"fan_modes", fan_modes},
        {"presets", presets},
        {"visual_min_temp", std::to_string((int)traits.get_visual_min_temperature())},
        {"visual_max_temp", std::to_string((int)traits.get_visual_max_temperature())},
        {"visual_temp_step", std::to_string(traits.get_visual_target_temperature_step())},
        {"supports_swing", !traits.get_supported_swing_modes().empty() ? "true" : "false"},
      });
      j.raw(R"({"name":"climate_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("mode":{"type":"string","description":"Climate mode"},)");
      j.raw(R"("target_temperature":{"type":"number"},)");
      j.raw(R"("target_temperature_low":{"type":"number"},)");
      j.raw(R"("target_temperature_high":{"type":"number"},)");
      j.raw(R"("fan_mode":{"type":"string"},)");
      j.raw(R"("swing_mode":{"type":"string"},)");
      j.raw(R"("preset":{"type":"string"})");
      j.raw(R"(}}})");
    }
  }
#endif

  // ────────── NUMBER ──────────
#ifdef USE_NUMBER
  if (should_include("number", auto_discover, type_filters)) {
    for (auto *e : App.get_numbers()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->traits;
      std::string desc = rich_desc("Set number: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"device_class", e->get_device_class()},
        {"unit", e->get_unit_of_measurement()},
        {"min", std::to_string(traits.get_min_value())},
        {"max", std::to_string(traits.get_max_value())},
        {"step", std::to_string(traits.get_step())},
        {"mode", e->traits.get_mode() == number::NUMBER_MODE_SLIDER ? "slider" :
                 e->traits.get_mode() == number::NUMBER_MODE_BOX ? "box" : "auto"},
      });
      j.raw(R"({"name":"number_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("value":{"type":"number","minimum":)" + std::to_string(traits.get_min_value()));
      j.raw(R"(,"maximum":)" + std::to_string(traits.get_max_value()));
      j.raw(R"(,"description":"Step: )" + std::to_string(traits.get_step()) + R"("})");
      j.raw(R"(},"required":["value"]}})");
    }
  }
#endif

  // ────────── SELECT ──────────
#ifdef USE_SELECT
  if (should_include("select", auto_discover, type_filters)) {
    for (auto *e : App.get_selects()) {
      if (e->is_internal()) continue;
      comma();
      const auto& options = e->traits.get_options();
      std::string opts_csv;
      std::string opts_enum = "[";
      for (size_t i = 0; i < options.size(); i++) {
        if (i > 0) { opts_csv += ","; opts_enum += ","; }
        opts_csv += std::string(options[i]);
        opts_enum += "\"" + std::string(options[i]) + "\"";
      }
      opts_enum += "]";
      std::string desc = rich_desc("Set select: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"options", opts_csv},
      });
      j.raw(R"({"name":"select_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("option":{"type":"string","enum":)" + opts_enum + R"(})");
      j.raw(R"(},"required":["option"]}})");
    }
  }
#endif

  // ────────── LOCK ──────────
#ifdef USE_LOCK
  if (should_include("lock", auto_discover, type_filters)) {
    for (auto *e : App.get_locks()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Control lock: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"commands", e->traits.get_supports_open() ? "LOCK,UNLOCK,OPEN" : "LOCK,UNLOCK"},
      });
      j.raw(R"({"name":"lock_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("command":{"type":"string","enum":["LOCK","UNLOCK","OPEN"]})");
      j.raw(R"(},"required":["command"]}})");
    }
  }
#endif

  // ────────── MEDIA PLAYER ──────────
#ifdef USE_MEDIA_PLAYER
  if (should_include("media_player", auto_discover, type_filters)) {
    for (auto *e : App.get_media_players()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->get_traits();
      std::string desc = rich_desc("Control media player: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"supports_pause", traits.get_supports_pause() ? "true" : "false"},
      });
      j.raw(R"({"name":"media_player_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("command":{"type":"string","enum":["PLAY","PAUSE","STOP","MUTE","UNMUTE"]},)");
      j.raw(R"("volume":{"type":"number","minimum":0,"maximum":1},)");
      j.raw(R"("media_url":{"type":"string","description":"URL to play"})");
      j.raw(R"(},"required":["command"]}})");
    }
  }
#endif

  // ────────── ALARM CONTROL PANEL ──────────
#ifdef USE_ALARM_CONTROL_PANEL
  if (should_include("alarm_control_panel", auto_discover, type_filters)) {
    for (auto *e : App.get_alarm_control_panels()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Control alarm: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"commands", "ARM_HOME,ARM_AWAY,ARM_NIGHT,ARM_VACATION,DISARM"},
        {"requires_code", e->get_requires_code() ? "true" : "false"},
        {"code", e->get_requires_code() ? "required" : "optional"},
      });
      j.raw(R"({"name":"alarm_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("command":{"type":"string","enum":["ARM_HOME","ARM_AWAY","ARM_NIGHT","ARM_VACATION","DISARM","PENDING","TRIGGERED"]},)");
      j.raw(R"("code":{"type":"string","description":"Alarm code if required"})");
      j.raw(R"(},"required":["command"]}})");
    }
  }
#endif

  // ────────── VALVE ──────────
#ifdef USE_VALVE
  if (should_include("valve", auto_discover, type_filters)) {
    for (auto *e : App.get_valves()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->get_traits();
      std::string desc = rich_desc("Control valve: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"device_class", e->get_device_class()},
        {"commands", "OPEN,CLOSE,STOP"},
        {"supports_position", traits.get_supports_position() ? "true" : "false"},
      });
      j.raw(R"({"name":"valve_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("command":{"type":"string","enum":["OPEN","CLOSE","STOP"]},)");
      j.raw(R"("position":{"type":"number","minimum":0,"maximum":1})");
      j.raw(R"(},"required":["command"]}})");
    }
  }
#endif

  // ────────── TEXT ──────────
#ifdef USE_TEXT
  if (should_include("text", auto_discover, type_filters)) {
    for (auto *e : App.get_texts()) {
      if (e->is_internal()) continue;
      comma();
      auto traits = e->traits;
      std::string desc = rich_desc("Set text: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"min_length", std::to_string(traits.get_min_length())},
        {"max_length", std::to_string(traits.get_max_length())},
        {"mode", traits.get_mode() == text::TEXT_MODE_TEXT ? "text" : "password"},
        {"pattern", traits.get_pattern()},
      });
      j.raw(R"({"name":"text_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("value":{"type":"string","minLength":)" + std::to_string(traits.get_min_length()));
      j.raw(R"(,"maxLength":)" + std::to_string(traits.get_max_length()) + R"(})");
      j.raw(R"(},"required":["value"]}})");
    }
  }
#endif

  // ────────── DATE / TIME / DATETIME ──────────
#ifdef USE_DATETIME_DATE
  if (should_include("date", auto_discover, type_filters)) {
    for (auto *e : App.get_dates()) {
      if (e->is_internal()) continue;
      comma();
      j.raw(R"({"name":"date_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":"Set date: )" + e->get_name() + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("year":{"type":"integer"},"month":{"type":"integer","minimum":1,"maximum":12},"day":{"type":"integer","minimum":1,"maximum":31})");
      j.raw(R"(},"required":["year","month","day"]}})");
    }
  }
#endif
#ifdef USE_DATETIME_TIME
  if (should_include("time", auto_discover, type_filters)) {
    for (auto *e : App.get_times()) {
      if (e->is_internal()) continue;
      comma();
      j.raw(R"({"name":"time_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":"Set time: )" + e->get_name() + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("hour":{"type":"integer","minimum":0,"maximum":23},"minute":{"type":"integer","minimum":0,"maximum":59},"second":{"type":"integer","minimum":0,"maximum":59})");
      j.raw(R"(},"required":["hour","minute","second"]}})");
    }
  }
#endif
#ifdef USE_DATETIME_DATETIME
  if (should_include("datetime", auto_discover, type_filters)) {
    for (auto *e : App.get_datetimes()) {
      if (e->is_internal()) continue;
      comma();
      j.raw(R"({"name":"datetime_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":"Set datetime: )" + e->get_name() + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("epoch":{"type":"integer","description":"Unix timestamp"})");
      j.raw(R"(},"required":["epoch"]}})");
    }
  }
#endif

  // ────────── EVENT (read-only, last event) ──────────
#ifdef USE_EVENT
  if (should_include("event", auto_discover, type_filters)) {
    for (auto *e : App.get_events()) {
      if (e->is_internal()) continue;
      comma();
      std::string desc = rich_desc("Read last event: " + e->get_name(), {
        {"icon", e->get_icon()},
        {"device_class", e->get_device_class()},
      });
      j.raw(R"({"name":"event_get_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":")" + desc + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{}}})");
    }
  }
#endif

  // ────────── UPDATE ──────────
#ifdef USE_UPDATE
  if (should_include("update", auto_discover, type_filters)) {
    for (auto *e : App.get_updates()) {
      if (e->is_internal()) continue;
      comma();
      j.raw(R"({"name":"update_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":"Check/perform update: )" + e->get_name() + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("command":{"type":"string","enum":["CHECK","INSTALL"]})");
      j.raw(R"(},"required":["command"]}})");
    }
  }
#endif

  // ────────── SCRIPTS ──────────
#ifdef USE_SCRIPT
  if (expose_scripts) {
    for (auto *e : App.get_scripts()) {
      comma();
      j.raw(R"({"name":"script_)" + e->get_object_id() + R"(",)");
      j.raw(R"("description":"Execute script: )" + e->get_object_id() + R"(",)");
      j.raw(R"("inputSchema":{"type":"object","properties":{)");
      j.raw(R"("action":{"type":"string","enum":["execute","stop","is_running"],"description":"Action to perform on script"})");
      j.raw(R"(}}})" );
    }
  }
#endif

  j.raw("]}");
  return j.finish();
}


// ════════════════════════════════════════════════════════════════
//  TOOL EXECUTION — handle all entity types
// ════════════════════════════════════════════════════════════════

std::string execute_tool(const std::string &tool_name,
                         const std::string &args) {

  // ── SENSOR ──
#ifdef USE_SENSOR
  if (tool_name.rfind("sensor_get_", 0) == 0) {
    std::string id = tool_name.substr(11);
    for (auto *e : App.get_sensors()) {
      if (e->get_object_id() == id) {
        JsonBuilder j;
        j.start_object();
        j.key_str("name", e->get_name());
        j.key_float("value", e->get_state());
        j.key_str("unit", e->get_unit_of_measurement());
        if (!e->get_device_class().empty()) j.key_str("device_class", e->get_device_class());
        j.key_int("accuracy_decimals", e->get_accuracy_decimals());
        j.key_bool("has_state", e->has_state());
        j.end_object();
        return tool_result(j.finish());
      }
    }
  }
#endif

  // ── BINARY SENSOR ──
#ifdef USE_BINARY_SENSOR
  if (tool_name.rfind("binary_sensor_get_", 0) == 0) {
    std::string id = tool_name.substr(18);
    for (auto *e : App.get_binary_sensors()) {
      if (e->get_object_id() == id) {
        JsonBuilder j;
        j.start_object();
        j.key_str("name", e->get_name());
        j.key_bool("state", e->state);
        if (!e->get_device_class().empty()) j.key_str("device_class", e->get_device_class());
        j.key_bool("has_state", e->has_state());
        j.end_object();
        return tool_result(j.finish());
      }
    }
  }
#endif

  // ── TEXT SENSOR ──
#ifdef USE_TEXT_SENSOR
  if (tool_name.rfind("text_sensor_get_", 0) == 0) {
    std::string id = tool_name.substr(16);
    for (auto *e : App.get_text_sensors()) {
      if (e->get_object_id() == id) {
        JsonBuilder j;
        j.start_object();
        j.key_str("name", e->get_name());
        j.key_str("value", e->get_state());
        if (!e->get_device_class().empty()) j.key_str("device_class", e->get_device_class());
        j.key_bool("has_state", e->has_state());
        j.end_object();
        return tool_result(j.finish());
      }
    }
  }
#endif

  // ── SWITCH ──
#ifdef USE_SWITCH
  if (tool_name.rfind("switch_", 0) == 0) {
    std::string id = tool_name.substr(7);
    for (auto *e : App.get_switches()) {
      if (e->get_object_id() == id) {
        std::string state = get_arg(args, "state");
        if (state == "ON") e->turn_on();
        else if (state == "OFF") e->turn_off();
        else e->toggle();
        return tool_result("Switch " + id + " -> " + state);
      }
    }
  }
#endif

  // ── BUTTON ──
#ifdef USE_BUTTON
  if (tool_name.rfind("button_press_", 0) == 0) {
    std::string id = tool_name.substr(13);
    for (auto *e : App.get_buttons()) {
      if (e->get_object_id() == id) {
        e->press();
        return tool_result("Button " + id + " pressed");
      }
    }
  }
#endif

  // ── LIGHT ──
#ifdef USE_LIGHT
  if (tool_name.rfind("light_", 0) == 0) {
    std::string id = tool_name.substr(6);
    for (auto *e : App.get_lights()) {
      if (e->get_object_id() == id) {
        std::string state = get_arg(args, "state");
        if (state == "OFF") {
          auto call = e->turn_off();
          std::string tl = get_arg(args, "transition_length");
          if (!tl.empty()) call.set_transition_length(safe_stoi_(tl));
          call.perform();
        } else {
          auto call = e->turn_on();
          std::string b = get_arg(args, "brightness");
          if (!b.empty()) call.set_brightness(safe_stof_(b) / 255.0f);
          std::string ct = get_arg(args, "color_temp");
          if (!ct.empty()) call.set_color_temperature(safe_stof_(ct));
          std::string r = get_arg(args, "r"), g = get_arg(args, "g"), bv = get_arg(args, "b");
          if (!r.empty()) call.set_red(safe_stof_(r) / 255.0f);
          if (!g.empty()) call.set_green(safe_stof_(g) / 255.0f);
          if (!bv.empty()) call.set_blue(safe_stof_(bv) / 255.0f);
          std::string effect = get_arg(args, "effect");
          if (!effect.empty()) call.set_effect(effect);
          std::string tl = get_arg(args, "transition_length");
          if (!tl.empty()) call.set_transition_length(safe_stoi_(tl));
          call.perform();
        }
        return tool_result("Light " + id + " -> " + state);
      }
    }
  }
#endif

  // ── FAN ──
#ifdef USE_FAN
  if (tool_name.rfind("fan_", 0) == 0) {
    std::string id = tool_name.substr(4);
    for (auto *e : App.get_fans()) {
      if (e->get_object_id() == id) {
        std::string state = get_arg(args, "state");
        if (state == "OFF") {
          e->turn_off().perform();
        } else {
          auto call = e->turn_on();
          std::string speed = get_arg(args, "speed");
          if (!speed.empty()) call.set_speed(safe_stoi_(speed));
          std::string osc = get_arg(args, "oscillating");
          if (osc == "true") call.set_oscillating(true);
          else if (osc == "false") call.set_oscillating(false);
          std::string dir = get_arg(args, "direction");
          if (dir == "REVERSE") call.set_direction(fan::FanDirection::REVERSE);
          else if (dir == "FORWARD") call.set_direction(fan::FanDirection::FORWARD);
          call.perform();
        }
        return tool_result("Fan " + id + " -> " + state);
      }
    }
  }
#endif

  // ── COVER ──
#ifdef USE_COVER
  if (tool_name.rfind("cover_", 0) == 0) {
    std::string id = tool_name.substr(6);
    for (auto *e : App.get_covers()) {
      if (e->get_object_id() == id) {
        std::string cmd = get_arg(args, "command");
        auto cover_call = e->make_call();
        if (cmd == "OPEN") cover_call.set_command_open();
        else if (cmd == "CLOSE") cover_call.set_command_close();
        else if (cmd == "STOP") cover_call.set_command_stop();
        std::string pos = get_arg(args, "position");
        if (!pos.empty()) cover_call.set_position(safe_stof_(pos));
        std::string tilt = get_arg(args, "tilt");
        if (!tilt.empty()) cover_call.set_tilt(safe_stof_(tilt));
        cover_call.perform();
        return tool_result("Cover " + id + " -> " + cmd);
      }
    }
  }
#endif

  // ── CLIMATE ──
#ifdef USE_CLIMATE
  if (tool_name.rfind("climate_", 0) == 0) {
    std::string id = tool_name.substr(8);
    for (auto *e : App.get_climates()) {
      if (e->get_object_id() == id) {
        auto call = e->make_call();
        std::string mode = get_arg(args, "mode");
        if (mode == "OFF") call.set_mode(climate::CLIMATE_MODE_OFF);
        else if (mode == "HEAT") call.set_mode(climate::CLIMATE_MODE_HEAT);
        else if (mode == "COOL") call.set_mode(climate::CLIMATE_MODE_COOL);
        else if (mode == "HEAT_COOL") call.set_mode(climate::CLIMATE_MODE_HEAT_COOL);
        else if (mode == "DRY") call.set_mode(climate::CLIMATE_MODE_DRY);
        else if (mode == "FAN_ONLY") call.set_mode(climate::CLIMATE_MODE_FAN_ONLY);
        else if (mode == "AUTO") call.set_mode(climate::CLIMATE_MODE_AUTO);
        std::string tt = get_arg(args, "target_temperature");
        if (!tt.empty()) call.set_target_temperature(safe_stof_(tt));
        std::string ttl = get_arg(args, "target_temperature_low");
        if (!ttl.empty()) call.set_target_temperature_low(safe_stof_(ttl));
        std::string tth = get_arg(args, "target_temperature_high");
        if (!tth.empty()) call.set_target_temperature_high(safe_stof_(tth));
        std::string fm = get_arg(args, "fan_mode");
        if (!fm.empty()) call.set_fan_mode(fm);
        std::string sm = get_arg(args, "swing_mode");
        if (!sm.empty()) call.set_swing_mode(sm);
        std::string preset = get_arg(args, "preset");
        if (!preset.empty()) call.set_preset(preset);
        call.perform();
        return tool_result("Climate " + id + " configured");
      }
    }
  }
#endif

  // ── NUMBER ──
#ifdef USE_NUMBER
  if (tool_name.rfind("number_", 0) == 0) {
    std::string id = tool_name.substr(7);
    for (auto *e : App.get_numbers()) {
      if (e->get_object_id() == id) {
        std::string val = get_arg(args, "value");
        if (!val.empty()) {
          auto call = e->make_call();
          call.set_value(safe_stof_(val));
          call.perform();
        }
        return tool_result("Number " + id + " -> " + val);
      }
    }
  }
#endif

  // ── SELECT ──
#ifdef USE_SELECT
  if (tool_name.rfind("select_", 0) == 0) {
    std::string id = tool_name.substr(7);
    for (auto *e : App.get_selects()) {
      if (e->get_object_id() == id) {
        std::string opt = get_arg(args, "option");
        auto call = e->make_call();
        call.set_option(opt);
        call.perform();
        return tool_result("Select " + id + " -> " + opt);
      }
    }
  }
#endif

  // ── LOCK ──
#ifdef USE_LOCK
  if (tool_name.rfind("lock_", 0) == 0) {
    std::string id = tool_name.substr(5);
    for (auto *e : App.get_locks()) {
      if (e->get_object_id() == id) {
        std::string cmd = get_arg(args, "command");
        if (cmd == "LOCK") e->lock();
        else if (cmd == "UNLOCK") e->unlock();
        else if (cmd == "OPEN") e->open();
        return tool_result("Lock " + id + " -> " + cmd);
      }
    }
  }
#endif

  // ── MEDIA PLAYER ──
#ifdef USE_MEDIA_PLAYER
  if (tool_name.rfind("media_player_", 0) == 0) {
    std::string id = tool_name.substr(13);
    for (auto *e : App.get_media_players()) {
      if (e->get_object_id() == id) {
        auto call = e->make_call();
        std::string cmd = get_arg(args, "command");
        if (cmd == "PLAY") call.set_command(media_player::MEDIA_PLAYER_COMMAND_PLAY);
        else if (cmd == "PAUSE") call.set_command(media_player::MEDIA_PLAYER_COMMAND_PAUSE);
        else if (cmd == "STOP") call.set_command(media_player::MEDIA_PLAYER_COMMAND_STOP);
        else if (cmd == "MUTE") call.set_command(media_player::MEDIA_PLAYER_COMMAND_MUTE);
        else if (cmd == "UNMUTE") call.set_command(media_player::MEDIA_PLAYER_COMMAND_UNMUTE);
        std::string vol = get_arg(args, "volume");
        if (!vol.empty()) call.set_volume(safe_stof_(vol));
        std::string url = get_arg(args, "media_url");
        if (!url.empty()) call.set_media_url(url);
        call.perform();
        return tool_result("Media player " + id + " -> " + cmd);
      }
    }
  }
#endif

  // ── ALARM CONTROL PANEL ──
#ifdef USE_ALARM_CONTROL_PANEL
  if (tool_name.rfind("alarm_", 0) == 0) {
    std::string id = tool_name.substr(6);
    for (auto *e : App.get_alarm_control_panels()) {
      if (e->get_object_id() == id) {
        std::string cmd = get_arg(args, "command");
        std::string code = get_arg(args, "code");
        if (cmd == "ARM_HOME") e->arm_home(code);
        else if (cmd == "ARM_AWAY") e->arm_away(code);
        else if (cmd == "ARM_NIGHT") e->arm_night(code);
        else if (cmd == "ARM_VACATION") e->arm_vacation(code);
        else if (cmd == "DISARM") e->disarm(code);
        return tool_result("Alarm " + id + " -> " + cmd);
      }
    }
  }
#endif

  // ── VALVE ──
#ifdef USE_VALVE
  if (tool_name.rfind("valve_", 0) == 0) {
    std::string id = tool_name.substr(6);
    for (auto *e : App.get_valves()) {
      if (e->get_object_id() == id) {
        std::string cmd = get_arg(args, "command");
        auto valve_call = e->make_call();
        if (cmd == "OPEN") valve_call.set_command_open();
        else if (cmd == "CLOSE") valve_call.set_command_close();
        else if (cmd == "STOP") valve_call.set_command_stop();
        std::string pos = get_arg(args, "position");
        if (!pos.empty()) valve_call.set_position(safe_stof_(pos));
        valve_call.perform();
        return tool_result("Valve " + id + " -> " + cmd);
      }
    }
  }
#endif

  // ── TEXT ──
#ifdef USE_TEXT
  if (tool_name.rfind("text_", 0) == 0 && tool_name.rfind("text_sensor_", 0) != 0) {
    std::string id = tool_name.substr(5);
    for (auto *e : App.get_texts()) {
      if (e->get_object_id() == id) {
        std::string val = get_arg(args, "value");
        auto call = e->make_call();
        call.set_value(val);
        call.perform();
        return tool_result("Text " + id + " -> " + val);
      }
    }
  }
#endif

  // ── DATE ──
#ifdef USE_DATETIME_DATE
  if (tool_name.rfind("date_", 0) == 0) {
    std::string id = tool_name.substr(5);
    for (auto *e : App.get_dates()) {
      if (e->get_object_id() == id) {
        auto call = e->make_call();
        call.set_date(safe_stoi_(get_arg(args, "year")),
                      safe_stoi_(get_arg(args, "month")),
                      safe_stoi_(get_arg(args, "day")));
        call.perform();
        return tool_result("Date " + id + " set");
      }
    }
  }
#endif

  // ── TIME ──
#ifdef USE_DATETIME_TIME
  if (tool_name.rfind("time_", 0) == 0) {
    std::string id = tool_name.substr(5);
    for (auto *e : App.get_times()) {
      if (e->get_object_id() == id) {
        auto call = e->make_call();
        call.set_time(safe_stoi_(get_arg(args, "hour")),
                      safe_stoi_(get_arg(args, "minute")),
                      safe_stoi_(get_arg(args, "second")));
        call.perform();
        return tool_result("Time " + id + " set");
      }
    }
  }
#endif

  // ── DATETIME ──
#ifdef USE_DATETIME_DATETIME
  if (tool_name.rfind("datetime_", 0) == 0) {
    std::string id = tool_name.substr(9);
    for (auto *e : App.get_datetimes()) {
      if (e->get_object_id() == id) {
        auto call = e->make_call();
        call.set_datetime(safe_stoul_(get_arg(args, "epoch")));
        call.perform();
        return tool_result("Datetime " + id + " set");
      }
    }
  }
#endif

  // ── UPDATE ──
#ifdef USE_UPDATE
  if (tool_name.rfind("update_", 0) == 0) {
    std::string id = tool_name.substr(7);
    for (auto *e : App.get_updates()) {
      if (e->get_object_id() == id) {
        std::string cmd = get_arg(args, "command");
        if (cmd == "CHECK") e->check();
        else if (cmd == "INSTALL") e->install();
        return tool_result("Update " + id + " -> " + cmd);
      }
    }
  }
#endif

  // ── EVENT (read-only) ──
#ifdef USE_EVENT
  if (tool_name.rfind("event_get_", 0) == 0) {
    std::string id = tool_name.substr(10);
    for (auto *e : App.get_events()) {
      if (e->get_object_id() == id) {
        JsonBuilder j;
        j.start_object();
        j.key_str("name", e->get_name());
        j.key_str("event_type", e->get_event_type());
        j.end_object();
        return tool_result(j.finish());
      }
    }
  }
#endif

  // ── SCRIPT ──
#ifdef USE_SCRIPT
  if (tool_name.rfind("script_", 0) == 0) {
    std::string id = tool_name.substr(7);
    for (auto *s : App.get_scripts()) {
      if (s->get_object_id() == id) {
        std::string action = get_arg(args, "action");
        if (action == "stop") {
          s->stop();
          return tool_result("Script " + id + " stopped");
        } else if (action == "is_running") {
          return tool_result(s->is_running() ? "true" : "false");
        } else {
          s->execute();
          return tool_result("Script " + id + " executed");
        }
      }
    }
  }
#endif

  return tool_error("Unknown tool: " + tool_name);
}


// ════════════════════════════════════════════════════════════════
//  RESOURCES — every entity state readable as MCP resource
// ════════════════════════════════════════════════════════════════

std::string build_resources_list(bool auto_discover,
                                 const std::vector<std::string> &type_filters) {
  JsonBuilder j;
  j.raw(R"({"resources":[)");
  bool first = true;
  auto comma = [&]() { if (!first) j.raw(","); first = false; };

  auto add_resource = [&](const std::string &type, EntityBase *e, const std::string &extra_desc = "") {
    if (e->is_internal()) return;
    comma();
    j.raw(R"({"uri":"esphome://)" + type + "/" + e->get_object_id() + R"(",)");
    j.raw(R"("name":")" + e->get_name() + R"(",)");
    std::string desc = type + " entity";
    if (!e->get_icon().empty()) desc += " | icon: " + e->get_icon();
    if (!extra_desc.empty()) desc += " | " + extra_desc;
    j.raw(R"("description":")" + desc + R"(",)");
    j.raw(R"("mimeType":"application/json"})");
  };

#ifdef USE_SENSOR
  if (should_include("sensor", auto_discover, type_filters))
    for (auto *e : App.get_sensors())
      add_resource("sensor", e, "unit: " + e->get_unit_of_measurement() +
                   " | device_class: " + e->get_device_class());
#endif
#ifdef USE_BINARY_SENSOR
  if (should_include("binary_sensor", auto_discover, type_filters))
    for (auto *e : App.get_binary_sensors())
      add_resource("binary_sensor", e, "device_class: " + e->get_device_class());
#endif
#ifdef USE_TEXT_SENSOR
  if (should_include("text_sensor", auto_discover, type_filters))
    for (auto *e : App.get_text_sensors())
      add_resource("text_sensor", e);
#endif
#ifdef USE_SWITCH
  if (should_include("switch", auto_discover, type_filters))
    for (auto *e : App.get_switches())
      add_resource("switch", e, "device_class: " + e->get_device_class());
#endif
#ifdef USE_LIGHT
  if (should_include("light", auto_discover, type_filters))
    for (auto *e : App.get_lights()) add_resource("light", e);
#endif
#ifdef USE_FAN
  if (should_include("fan", auto_discover, type_filters))
    for (auto *e : App.get_fans()) add_resource("fan", e);
#endif
#ifdef USE_COVER
  if (should_include("cover", auto_discover, type_filters))
    for (auto *e : App.get_covers())
      add_resource("cover", e, "device_class: " + e->get_device_class());
#endif
#ifdef USE_CLIMATE
  if (should_include("climate", auto_discover, type_filters))
    for (auto *e : App.get_climates()) add_resource("climate", e);
#endif
#ifdef USE_NUMBER
  if (should_include("number", auto_discover, type_filters))
    for (auto *e : App.get_numbers()) add_resource("number", e);
#endif
#ifdef USE_SELECT
  if (should_include("select", auto_discover, type_filters))
    for (auto *e : App.get_selects()) add_resource("select", e);
#endif
#ifdef USE_LOCK
  if (should_include("lock", auto_discover, type_filters))
    for (auto *e : App.get_locks()) add_resource("lock", e);
#endif
#ifdef USE_BUTTON
  if (should_include("button", auto_discover, type_filters))
    for (auto *e : App.get_buttons()) add_resource("button", e);
#endif
#ifdef USE_MEDIA_PLAYER
  if (should_include("media_player", auto_discover, type_filters))
    for (auto *e : App.get_media_players()) add_resource("media_player", e);
#endif
#ifdef USE_ALARM_CONTROL_PANEL
  if (should_include("alarm_control_panel", auto_discover, type_filters))
    for (auto *e : App.get_alarm_control_panels()) add_resource("alarm_control_panel", e);
#endif
#ifdef USE_EVENT
  if (should_include("event", auto_discover, type_filters))
    for (auto *e : App.get_events()) add_resource("event", e);
#endif
#ifdef USE_VALVE
  if (should_include("valve", auto_discover, type_filters))
    for (auto *e : App.get_valves()) add_resource("valve", e);
#endif
#ifdef USE_UPDATE
  if (should_include("update", auto_discover, type_filters))
    for (auto *e : App.get_updates()) add_resource("update", e);
#endif
#ifdef USE_DATETIME_DATE
  if (should_include("date", auto_discover, type_filters))
    for (auto *e : App.get_dates()) add_resource("date", e);
#endif
#ifdef USE_DATETIME_TIME
  if (should_include("time", auto_discover, type_filters))
    for (auto *e : App.get_times()) add_resource("time", e);
#endif
#ifdef USE_DATETIME_DATETIME
  if (should_include("datetime", auto_discover, type_filters))
    for (auto *e : App.get_datetimes()) add_resource("datetime", e);
#endif
#ifdef USE_TEXT
  if (should_include("text", auto_discover, type_filters))
    for (auto *e : App.get_texts()) add_resource("text", e);
#endif

  j.raw("]}");
  return j.finish();
}


// ════════════════════════════════════════════════════════════════
//  RESOURCE READ — full state snapshot for any entity
// ════════════════════════════════════════════════════════════════

std::string read_resource(const std::string &uri) {
  // Parse "esphome://type/object_id"
  auto prefix = std::string("esphome://");
  if (uri.rfind(prefix, 0) != 0) return tool_error("Invalid URI: " + uri);
  std::string rest = uri.substr(prefix.size());
  auto slash = rest.find('/');
  if (slash == std::string::npos) return tool_error("Invalid URI: " + uri);
  std::string type = rest.substr(0, slash);
  std::string id = rest.substr(slash + 1);

  // Wrap a JSON body as a proper resources/read response
  auto make_response = [&](const std::string &body) -> std::string {
    return R"({"contents":[{"uri":")" + json_string_escape_(uri) +
           R"(","mimeType":"application/json","text":")" + json_string_escape_(body) +
           R"("}]})";
  };

  JsonBuilder j;
  j.start_object();

#ifdef USE_SENSOR
  if (type == "sensor") {
    for (auto *e : App.get_sensors()) {
      if (e->get_object_id() != id) continue;
      j.key_str("name", e->get_name());
      j.key_str("object_id", e->get_object_id());
      j.key_float("state", e->get_state());
      j.key_str("unit_of_measurement", e->get_unit_of_measurement());
      j.key_int("accuracy_decimals", e->get_accuracy_decimals());
      j.key_str("device_class", e->get_device_class());
      j.key_str("icon", e->get_icon());
      j.key_bool("has_state", e->has_state());
      j.key_bool("is_internal", e->is_internal());
      j.key_bool("disabled_by_default", e->is_disabled_by_default());
      j.end_object();
      return make_response(j.finish());
    }
  }
#endif

#ifdef USE_BINARY_SENSOR
  if (type == "binary_sensor") {
    for (auto *e : App.get_binary_sensors()) {
      if (e->get_object_id() != id) continue;
      j.key_str("name", e->get_name());
      j.key_bool("state", e->state);
      j.key_str("device_class", e->get_device_class());
      j.key_str("icon", e->get_icon());
      j.key_bool("has_state", e->has_state());
      j.end_object();
      return make_response(j.finish());
    }
  }
#endif

#ifdef USE_SWITCH
  if (type == "switch") {
    for (auto *e : App.get_switches()) {
      if (e->get_object_id() != id) continue;
      j.key_str("name", e->get_name());
      j.key_bool("state", e->state);
      j.key_str("device_class", e->get_device_class());
      j.key_str("icon", e->get_icon());
      j.key_bool("assumed_state", e->assumed_state());
      j.end_object();
      return make_response(j.finish());
    }
  }
#endif

#ifdef USE_LIGHT
  if (type == "light") {
    for (auto *e : App.get_lights()) {
      if (e->get_object_id() != id) continue;
      j.key_str("name", e->get_name());
      auto values = e->remote_values;
      j.key_bool("is_on", values.is_on());
      j.key_float("brightness", values.get_brightness());
      j.key_float("color_temp", values.get_color_temperature());
      j.key_float("red", values.get_red());
      j.key_float("green", values.get_green());
      j.key_float("blue", values.get_blue());
      j.key_float("white", values.get_white());
      j.key_str("icon", e->get_icon());
      // List effects
      std::string fx_list;
      for (auto *fx : e->get_effects()) {
        if (!fx_list.empty()) fx_list += ", ";
        fx_list += fx->get_name();
      }
      j.key_str("effects", fx_list);
      j.end_object();
      return make_response(j.finish());
    }
  }
#endif

#ifdef USE_CLIMATE
  if (type == "climate") {
    for (auto *e : App.get_climates()) {
      if (e->get_object_id() != id) continue;
      j.key_str("name", e->get_name());
      j.key_float("current_temperature", e->current_temperature);
      j.key_float("target_temperature", e->target_temperature);
      j.key_float("target_temperature_low", e->target_temperature_low);
      j.key_float("target_temperature_high", e->target_temperature_high);
      j.key_str("icon", e->get_icon());
      j.end_object();
      return make_response(j.finish());
    }
  }
#endif

  // Fallback for types not fully implemented in read_resource
  return make_response("{}");
}

}  // namespace mcp_server
}  // namespace esphome

#pragma GCC diagnostic pop