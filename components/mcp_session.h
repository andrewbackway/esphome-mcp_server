#pragma once

#include <string>
#include <functional>

namespace esphome {
namespace mcp_server {

class MCPServerComponent;  // forward

class MCPSession {
 public:
  MCPSession(int client_fd, MCPServerComponent *server);
  void loop();
  bool is_disconnected() const { return this->disconnected_; }

 protected:
  int client_fd_;
  MCPServerComponent *server_;
  bool disconnected_{false};
  bool initialized_{false};
  std::string read_buffer_;

  void process_message_(const std::string &message);
  void send_response_(const std::string &id, const std::string &result);
  void send_error_(const std::string &id, int code, const std::string &msg);
};

}  // namespace mcp_server
}  // namespace esphome