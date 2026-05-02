#pragma once

#include "CgiRequest.hpp"
#include "Connection.hpp"
#include "Poller.hpp"
#include "ServerConfig.hpp"
#include "UniqueFd.hpp"

#include <filesystem>
#include <cstdint>
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
  void handleSignal(int fd);
  void handleClientEvent(int fd, uint32_t events);
  void removeConnection(ClientSession &clientSession) noexcept;
  void readRequest(int fd);
  void processRequest(int fd);
  void processCgiInput(int fd);
  void processCgiOutput(int fd, uint32_t events);
  void finishCgi(int clientFd, int outputFd);
  void writeResponse(int fd);
  void closeIdleConnections();

  [[nodiscard]] const ServerConfig &
  selectVirtualHost(const ClientSession &session,
                    const HttpRequest &request) const;
  void queueResponse(int fd, HttpResponse response);
  void queueError(int fd, const ServerConfig *server, int statusCode);
  void queueMethodNotAllowed(int fd, const LocationConfig &location);
  void queueRedirect(int fd, const LocationConfig &location);
  [[nodiscard]] bool isCgiRequest(const HttpRequest &request,
                                  const LocationConfig &location) const;
  void startCgi(int fd, const ServerConfig &server,
                const LocationConfig &location);
  void handleStaticGet(int fd, const ServerConfig &server,
                       const LocationConfig &location);
  void handleUpload(int fd, const ServerConfig &server,
                    const LocationConfig &location);
  void handleDelete(int fd, const ServerConfig &server,
                    const LocationConfig &location);

  Poller _poller;
  std::vector<ServerConfig> _servers;
  std::map<int, Listener> _listeningFds;
  std::map<int, ClientSession> _connectionFds;
  std::map<int, int> _cgiFdToClient;
  UniqueFd _signalFd;
  bool _shouldStop{false};
};

} // namespace webserv
