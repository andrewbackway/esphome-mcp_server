#pragma once

#include <string>
#include <vector>

namespace esphome {
namespace mcp_server {

// ── JSON Helpers ──
class JsonBuilder {
 public:
  JsonBuilder() { buf_.reserve(2048); }
  void start_object() { buf_ += '{'; }
  void end_object() { trim_comma_(); buf_ += "},"; }
  void start_array(const std::string &key) { key_(key); buf_ += '['; }
  void end_array() { trim_comma_(); buf_ += "],"; }
  void key_str(const std::string &k, const std::string &v) {
    key_(k); buf_ += '"'; buf_ += escape_(v); buf_ += "\",";
  }
  void key_float(const std::string &k, float v) {
    char tmp[32]; snprintf(tmp, sizeof(tmp), "%.4g", v);
    key_(k); buf_ += tmp; buf_ += ',';
  }
  void key_int(const std::string &k, int v) {
    key_(k); buf_ += std::to_string(v); buf_ += ',';
  }
  void key_bool(const std::string &k, bool v) {
    key_(k); buf_ += v ? "true," : "false,";
  }
  void key_raw(const std::string &k, const std::string &raw) {
    key_(k); buf_ += raw; buf_ += ',';
  }
  void raw(const std::string &s) { buf_ += s; }
  std::string finish() { trim_comma_(); return buf_; }
 private:
  std::string buf_;
  void key_(const std::string &k) { buf_ += '"'; buf_ += k; buf_ += "\":"; }
  void trim_comma_() { if (!buf_.empty() && buf_.back() == ',') buf_.pop_back(); }
  std::string escape_(const std::string &s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
      if (c == '"') out += "\\\"";
      else if (c == '\\') out += "\\\\";
      else if (c == '\n') out += "\\n";
      else out += c;
    }
    return out;
  }
};

// ── Entity metadata helper ──
struct EntityMeta {
  std::string object_id;
  std::string name;
  std::string icon;
  std::string entity_category;  // "", "config", "diagnostic"
  bool disabled_by_default;
};

// ── Public API ──
std::string build_tools_list(bool auto_discover, bool expose_scripts,
                             const std::vector<std::string> &type_filters);
std::string execute_tool(const std::string &tool_name,
                         const std::string &arguments_json);
std::string build_resources_list(bool auto_discover,
                                 const std::vector<std::string> &type_filters);
std::string read_resource(const std::string &uri);

}  // namespace mcp_server
}  // namespace esphome