#include "WebServer.hpp"
#include "HttpTypes.hpp"
#include "Parser.hpp"
#include "Poller.hpp"
#include "ServerConfig.hpp"
#include "Tokenizer.hpp"
#include "UniqueFd.hpp"
#include "Validator.hpp"

#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace webserv {

namespace {

bool sameEndpoint(const Listener &listener, const ListenEndpoint &endpoint) {
  return listener.host == endpoint.host && listener.port == endpoint.port;
}

} // namespace

WebServer::WebServer(std::vector<ServerConfig> servers)
    : _servers(std::move(servers)) {}

WebServer::WebServer(const std::filesystem::path &configPath) {
  Tokenizer tokenizer;
  tokenizer.readFile(configPath.string());
  Parser parser(tokenizer.getTokens());
  _servers = parser.parse();

  Validator validator(_servers);
  validator.validate();
}

WebServer::~WebServer() = default;

void WebServer::setupSignals() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  // Block default delivery so the signal is only surfaced via signalfd.
  // Must happen before any thread/fork inherits an unblocked mask.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1)
    throw std::runtime_error("sigprocmask failed");

  _signalFd = UniqueFd(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
  if (_signalFd.get() == -1)
    throw std::runtime_error("signalfd failed");

  _poller.addFd(_signalFd.get(), EPOLLIN, FdType::Signal);
}

void WebServer::setNonblockingFlag(UniqueFd &fd) const {
  // Add non-blocking flag for the fd
  int flags = fcntl(fd.get(), F_GETFL, 0);
  if (flags == -1)
    throw std::runtime_error("fcntl(F_GETFL) failed");
  if (fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK) == -1)
    throw std::runtime_error("fcntl(F_SETFL) failed");
}

int WebServer::setupSocket(const ListenEndpoint &endpoint) const {
  addrinfo hints; // Create structure to store socket settings
  std::memset(&hints, 0, sizeof(hints)); // Set all with zero for safety
  hints.ai_family = AF_INET;             // or AF_UNSPEC later; IPv4 socket type
  hints.ai_socktype = SOCK_STREAM;       // Listen for TCP protocol
  hints.ai_flags = AI_PASSIVE; // set 0.0.0.0 on null endpoint.host value

  addrinfo *addrInfo = 0;
  int rc = getaddrinfo(
      endpoint.host.c_str(), std::to_string(endpoint.port).c_str(), &hints,
      &addrInfo); // Setup addrInfo with endpoint.host and .port values
  if (rc != 0)
    throw std::runtime_error("getaddrinfo failed");

  UniqueFd serverFd(
      socket(addrInfo->ai_family, addrInfo->ai_socktype,
             addrInfo->ai_protocol)); // Create socket and store in UniqueFd to
                                      // prevent leaks with close() on error
  if (serverFd.get() < 0) {
    freeaddrinfo(addrInfo);
    throw std::runtime_error("socket failed");
  }

  constexpr int yes = 1;
  if (setsockopt(serverFd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) <
      0) {
    freeaddrinfo(addrInfo);
    throw std::runtime_error("setsockopt failed");
  }

  try {
    setNonblockingFlag(serverFd);
  } catch (...) {
    freeaddrinfo(addrInfo);
    throw;
  }

  std::cout << "Server conf: " << endpoint.host << ":" << endpoint.port
            << std::endl;
  if (bind(serverFd.get(), addrInfo->ai_addr, addrInfo->ai_addrlen) < 0) {
    freeaddrinfo(addrInfo);
    throw std::runtime_error("bind failed");
  }

  if (listen(serverFd.get(), SOMAXCONN) < 0) {
    freeaddrinfo(addrInfo);
    throw std::runtime_error("listen failed");
  }

  freeaddrinfo(addrInfo);
  return serverFd.release();
}

void WebServer::createServer(const ServerConfig &serverConfig) {
  const std::vector<ListenEndpoint> &endpoints = serverConfig.listenEndpoints();

  for (const ListenEndpoint &endpoint : endpoints) {
    bool alreadyListening = false;
    for (const auto &[fd, listener] : _listeningFds) {
      (void)fd;
      if (sameEndpoint(listener, endpoint)) {
        alreadyListening = true;
        break;
      }
    }
    if (alreadyListening)
      continue;

    UniqueFd fd(setupSocket(endpoint));
    int rawFd = fd.get();

    _poller.addFd(rawFd, EPOLLIN, FdType::Listener);

    Listener temp = {std::move(fd), endpoint.host, endpoint.port,
                     &serverConfig};
    _listeningFds.emplace(rawFd, std::move(temp));

    std::cout << "[Server] : socket listening on " << endpoint.host << ':'
              << endpoint.port << std::endl;
  }
}

} // namespace webserv
