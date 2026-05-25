#include "mcp_session.h"
#include "mcp_server.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace esphome {
namespace mcp_server {

static const char *const TAG = "mcp_server.session";

MCPSession::MCPSession(int client_fd, MCPServerComponent *server)
    : client_fd_(client_fd), server_(server) {}

static std::string json_escape_(const std::string &in) {
  std::string out;
  out.reserve(in.size() + 16);
  for (char c : in) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

static bool send_all_(int fd, const std::string &data) {
  const char *ptr = data.c_str();
  size_t remaining = data.size();

  while (remaining > 0) {
    int written = ::send(fd, ptr, remaining, 0);
    if (written <= 0) {
      return false;
    }
    ptr += written;
    remaining -= written;
  }
  return true;
}

static std::string extract_json_string_field_(const std::string &json, const std::string &key) {
  std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return "";

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";

  pos++;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
  if (pos >= json.size() || json[pos] != '"') return "";

  pos++;
  std::string out;
  while (pos < json.size()) {
    char c = json[pos++];
    if (c == '\\' && pos < json.size()) {
      char esc = json[pos++];
      switch (esc) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += esc; break;
      }
      continue;
    }
    if (c == '"') break;
    out += c;
  }
  return out;
}

static std::string extract_json_value_field_(const std::string &json, const std::string &key) {
  std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return "";

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";

  pos++;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
  if (pos >= json.size()) return "";

  if (json[pos] == '"') {
    size_t end = pos + 1;
    bool escaped = false;
    while (end < json.size()) {
      char c = json[end];
      if (c == '"' && !escaped) break;
      escaped = (c == '\\' && !escaped);
      if (c != '\\') escaped = false;
      end++;
    }
    if (end < json.size()) return json.substr(pos, end - pos + 1);
  }

  if (json[pos] == '{' || json[pos] == '[') {
    char open = json[pos];
    char close = (open == '{') ? '}' : ']';
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    size_t end = pos;
    for (; end < json.size(); end++) {
      char c = json[end];
      if (in_string) {
        if (c == '"' && !escaped) in_string = false;
        escaped = (c == '\\' && !escaped);
        if (c != '\\') escaped = false;
        continue;
      }
      if (c == '"') {
        in_string = true;
        continue;
      }
      if (c == open) depth++;
      if (c == close) {
        depth--;
        if (depth == 0) break;
      }
    }
    if (end < json.size()) return json.substr(pos, end - pos + 1);
  }

  size_t end = pos;
  while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n' && json[end] != '\r') end++;
  return json.substr(pos, end - pos);
}

void MCPSession::loop() {
  if (this->disconnected_) return;

  char buffer[1024];
  int len = ::recv(this->client_fd_, buffer, sizeof(buffer), MSG_DONTWAIT);
  if (len == 0) {
    ESP_LOGD(TAG, "Client disconnected");
    this->disconnected_ = true;
    return;
  }
  if (len < 0) {
    return;
  }

  this->read_buffer_.append(buffer, len);

  size_t newline_pos;
  while ((newline_pos = this->read_buffer_.find('\n')) != std::string::npos) {
    std::string message = this->read_buffer_.substr(0, newline_pos);
    this->read_buffer_.erase(0, newline_pos + 1);

    if (!message.empty() && message.back() == '\r')
      message.pop_back();
    if (message.empty())
      continue;

    ESP_LOGV(TAG, "RX: %s", message.c_str());
    this->process_message_(message);
    if (this->disconnected_) return;
  }

  if (this->read_buffer_.size() > 16384) {
    ESP_LOGW(TAG, "Read buffer exceeded limit; disconnecting client");
    this->send_error_("null", -32700, "Message too large");
    this->disconnected_ = true;
  }
}

void MCPSession::process_message_(const std::string &message) {
  const std::string method = extract_json_string_field_(message, "method");
  const std::string id = extract_json_value_field_(message, "id");

  if (method.empty()) {
    this->send_error_(id.empty() ? "null" : id, -32600, "Invalid Request: missing method");
    return;
  }

  if (method == "initialize") {
    std::string result = this->server_->handle_initialize();
    this->initialized_ = true;
    this->send_response_(id.empty() ? "null" : id, result);
    return;
  }

  if (method == "notifications/initialized") {
    return;
  }

  if (!this->initialized_) {
    this->send_error_(id.empty() ? "null" : id, -32002, "Server not initialized");
    return;
  }

  if (method == "tools/list") {
    std::string result = this->server_->handle_tools_list();
    this->send_response_(id.empty() ? "null" : id, result);
    return;
  }

  if (method == "resources/list") {
    std::string result = this->server_->handle_resources_list();
    this->send_response_(id.empty() ? "null" : id, result);
    return;
  }

  if (method == "tools/call") {
    std::string params = extract_json_value_field_(message, "params");
    std::string tool_name = extract_json_string_field_(params, "name");
    std::string arguments = extract_json_value_field_(params, "arguments");
    if (arguments.empty()) arguments = "{}";

    if (tool_name.empty()) {
      this->send_error_(id.empty() ? "null" : id, -32602, "Invalid params: missing tool name");
      return;
    }

    std::string result = this->server_->handle_tool_call(tool_name, arguments);
    this->send_response_(id.empty() ? "null" : id, result);
    return;
  }

  if (method == "resources/read") {
    std::string params = extract_json_value_field_(message, "params");
    std::string uri = extract_json_string_field_(params, "uri");
    if (uri.empty()) {
      this->send_error_(id.empty() ? "null" : id, -32602, "Invalid params: missing uri");
      return;
    }

    std::string result = this->server_->handle_resource_read(uri);
    this->send_response_(id.empty() ? "null" : id, result);
    return;
  }

  this->send_error_(id.empty() ? "null" : id, -32601, "Method not found");
}

void MCPSession::send_response_(const std::string &id, const std::string &result) {
  std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}\n";
  ESP_LOGV(TAG, "TX: %s", response.c_str());
  if (!send_all_(this->client_fd_, response)) {
    ESP_LOGW(TAG, "Failed to send response; disconnecting client");
    this->disconnected_ = true;
  }
}

void MCPSession::send_error_(const std::string &id, int code, const std::string &msg) {
  std::string response =
      "{\"jsonrpc\":\"2.0\",\"id\":" + id +
      ",\"error\":{\"code\":" + std::to_string(code) +
      ",\"message\":\"" + json_escape_(msg) + "\"}}\n";
  ESP_LOGV(TAG, "TXERR: %s", response.c_str());
  if (!send_all_(this->client_fd_, response)) {
    this->disconnected_ = true;
  }
}

} // namespace mcp_server
} // namespace esphome