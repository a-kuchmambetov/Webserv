#pragma once

#include "CgiRequest.hpp"
#include "Connection.hpp"
#include "Poller.hpp"
#include "ServerConfig.hpp"
#include "UniqueFd.hpp"

#include <filesystem>
#include <map>
#include <sys/types.h>
#include <vector>

namespace webserv {

struct Listener {
  UniqueFd fd;
  std::string host;
  std::uint16_t port;
  const ServerConfig *serverConfig;
};

struct ClientSession {
  Connection connection;
  const ServerConfig *defaultServer;
  int listenerFd{-1};
};

class WebServer {
public:
  WebServer() = default;
  explicit WebServer(std::vector<ServerConfig> servers);
  explicit WebServer(const std::filesystem::path &configPath);
  ~WebServer();

  WebServer(const WebServer &) = delete;
  WebServer &operator=(const WebServer &) = delete;
  WebServer(WebServer &&other) = delete;
  WebServer &operator=(WebServer &&other) = delete;

  void run();

private:
  void setNonblockingFlag(UniqueFd &fd) const;
  [[nodiscard]] int setupSocket(const ListenEndpoint &endpoint) const;
  void createServer(const ServerConfig &server);
  void setupSignals();

  [[nodiscard]] bool acceptConnection(const Listener &listener);
  void removeConnection(ClientSession &ClientSession) noexcept;
  void readRequest(int fd);
  void processRequest(int fd);
  void processCgi(int fd);
  void writeResponse(int fd);
  void closeIdleConnections();

  std::string getStaticFile(const std::filesystem::path path);

  Poller _poller;
  std::vector<ServerConfig> _servers;
  std::map<int, Listener> _listeningFds;
  std::map<int, ClientSession> _connectionFds;
  UniqueFd _signalFd;
  bool _shouldStop{false};
};

} // namespace webserv
