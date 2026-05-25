#pragma once

#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/components/network/util.h"

#include <vector>
#include <string>
#include <memory>

#ifdef USE_ESP_IDF
#include "lwip/sockets.h"
#else
#include <WiFiServer.h>
#include <WiFiClient.h>
#endif

namespace esphome {
namespace mcp_server {

class MCPSession;  // forward decl

class MCPServerComponent : public Component {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  void set_auto_discover(bool val) { this->auto_discover_ = val; }
  void set_expose_scripts(bool val) { this->expose_scripts_ = val; }
  void add_entity_type_filter(const std::string &type) {
    this->entity_type_filters_.push_back(type);
  }

  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

  // --- MCP Protocol Handlers ---
  std::string handle_initialize();
  std::string handle_tools_list();
  std::string handle_tool_call(const std::string &tool_name,
                               const std::string &arguments_json);
  std::string handle_resources_list();
  std::string handle_resource_read(const std::string &uri);

 protected:
  uint16_t port_{8080};
  bool auto_discover_{true};
  bool expose_scripts_{true};
  std::vector<std::string> entity_type_filters_;
  std::vector<std::shared_ptr<MCPSession>> sessions_;

#ifdef USE_ESP_IDF
  int server_fd_{-1};
#else
  std::unique_ptr<WiFiServer> server_;
#endif

  void accept_clients_();
  void cleanup_sessions_();
};

}  // namespace mcp_server
}  // namespace esphome