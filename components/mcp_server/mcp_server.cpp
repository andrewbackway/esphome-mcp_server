#include "mcp_server.h"
#include "mcp_session.h"
#include "mcp_entity_tools.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mcp_server {

static const char *TAG = "mcp_server";

void MCPServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MCP Server on port %d...", this->port_);

#ifdef USE_ESP_IDF
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(this->port_);
  addr.sin_addr.s_addr = INADDR_ANY;

  this->server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(this->server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Non-blocking
  int flags = fcntl(this->server_fd_, F_GETFL, 0);
  fcntl(this->server_fd_, F_SETFL, flags | O_NONBLOCK);

  bind(this->server_fd_, (struct sockaddr *)&addr, sizeof(addr));
  listen(this->server_fd_, 4);
#else
  this->server_ = std::make_unique<WiFiServer>(this->port_);
  this->server_->begin();
#endif

  ESP_LOGI(TAG, "MCP Server listening on port %d", this->port_);
}

void MCPServerComponent::loop() {
  this->accept_clients_();
  for (auto &session : this->sessions_) {
    session->loop();
  }
  this->cleanup_sessions_();
}

void MCPServerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MCP Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  Auto Discover: %s", YESNO(this->auto_discover_));
  ESP_LOGCONFIG(TAG, "  Expose Scripts: %s", YESNO(this->expose_scripts_));
}

// --- MCP Protocol Implementation ---

std::string MCPServerComponent::handle_initialize() {
  // MCP initialize response with capabilities
  return R"({
    "protocolVersion": "2025-03-26",
    "capabilities": {
      "tools": { "listChanged": false },
      "resources": { "subscribe": false, "listChanged": false }
    },
    "serverInfo": {
      "name": "esphome-mcp",
      "version": "2026.6.0"
    }
  })";
}

std::string MCPServerComponent::handle_tools_list() {
  return build_tools_list(this->auto_discover_,
                          this->expose_scripts_,
                          this->entity_type_filters_);
}

std::string MCPServerComponent::handle_tool_call(
    const std::string &tool_name, const std::string &arguments_json) {
  return execute_tool(tool_name, arguments_json);
}

std::string MCPServerComponent::handle_resources_list() {
  return build_resources_list(this->auto_discover_,
                              this->entity_type_filters_);
}

std::string MCPServerComponent::handle_resource_read(const std::string &uri) {
  return read_resource(uri);
}

void MCPServerComponent::accept_clients_() {
#ifdef USE_ESP_IDF
  struct sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);
  int client_fd = ::accept(this->server_fd_, (struct sockaddr *) &client_addr, &addr_len);
  if (client_fd < 0) {
    return;  // EAGAIN / EWOULDBLOCK — no pending connection
  }
  int flags = fcntl(client_fd, F_GETFL, 0);
  fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  ESP_LOGI(TAG, "New MCP client connected (fd=%d)", client_fd);
  this->sessions_.push_back(std::make_shared<MCPSession>(client_fd, this));
#else
  WiFiClient client = this->server_->available();
  if (!client) {
    return;
  }
  ESP_LOGI(TAG, "New MCP client connected");
  this->sessions_.push_back(std::make_shared<MCPSession>(client.fd(), this));
#endif
}

void MCPServerComponent::cleanup_sessions_() {
  this->sessions_.erase(
      std::remove_if(this->sessions_.begin(), this->sessions_.end(),
                     [](const auto &s) { return s->is_disconnected(); }),
      this->sessions_.end());
}

}  // namespace mcp_server
}  // namespace esphome