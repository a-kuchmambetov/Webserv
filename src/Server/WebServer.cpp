#include "WebServer.hpp"
#include "Connection.hpp"
#include "HttpTypes.hpp"
#include "Poller.hpp"
#include "ServerConfig.hpp"
#include "UniqueFd.hpp"

#include <cerrno>
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

WebServer::WebServer(std::vector<ServerConfig> servers)
    : _servers(std::move(servers)) {}

WebServer::~WebServer() = default;

void WebServer::run() {
  setupSignals();

  // Create listening sockets for each server config
  for (ServerConfig &serverConfig : _servers)
    createServer(serverConfig);

  while (!_shouldStop) {
    EventsData eventData = _poller.getEventsReady();
    int n = eventData.eventsReadyN;
    std::vector<epoll_event> &events = *eventData.events;

    if (n == 0)
      continue;

    if (n == -1) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("epoll_wait failed");
    }

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;

      // SIGINT/SIGTERM delivered via signalfd
      if (fd == _signalFd.get()) {
        signalfd_siginfo si;
        while (read(fd, &si, sizeof(si)) == sizeof(si)) {
        }
        std::cout << "\n[Server] : shutdown signal received\n";
        _shouldStop = true;
        break;
      }
      // New incoming connection
      else if (_listeningFds.contains(fd)) {

        while (acceptConnection(_listeningFds.at(fd))) {
        }
      }
      // Client disconnected
      else if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
        removeConnection(_connectionFds.at(fd));

      }
      // Client sent data
      else if (events[i].events & EPOLLIN) {
        char buf[4096];

        while (true) {
          ssize_t bytes = recv(fd, buf, sizeof(buf), 0);

          if (bytes > 0) {
            std::cout << "Received " << bytes << " bytes from client " << fd
                      << ":\n";
            std::cout.write(buf, bytes);
            std::cout << '\n';
            std::cout.flush();
            continue;
          }

          if (bytes == 0) {
            removeConnection(_connectionFds.at(fd));
            break;
          }

          if (errno == EINTR)
            continue;
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

          removeConnection(_connectionFds.at(fd));
          break;
        }
      }
    }
  }
}

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

  _poller.addFd(_signalFd, EPOLLIN, FdType::Signal);
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
    UniqueFd fd(setupSocket(endpoint));
    int rawFd = fd.get();

    _poller.addFd(fd, EPOLLIN);

    Listener temp = {std::move(fd), endpoint.host, endpoint.port,
                     &serverConfig};
    _listeningFds.emplace(rawFd, std::move(temp));

    std::cout << "[Server] : socket listening on " << endpoint.host << ':'
              << endpoint.port << "\n";
  }
}

bool WebServer::acceptConnection(const Listener &listener) {
  int listenerFd = listener.fd.get();

  sockaddr_in clientAddr{};
  socklen_t clientLen = sizeof(clientAddr);

  UniqueFd clientFd(accept(
      listenerFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen));

  if (clientFd.get() == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return false;

    throw std::runtime_error("accept failed");
  }

  setNonblockingFlag(clientFd);

  _poller.addFd(clientFd, EPOLLIN | EPOLLRDHUP, FdType::Client);

  int rawClientFd = clientFd.get();
  _connectionFds.emplace(rawClientFd,
                         ClientSession{Connection(std::move(clientFd)),
                                       listener.serverConfig, listenerFd});

  ClientSession &clientRef = _connectionFds.at(rawClientFd);

  uint32_t ipv4 = ntohl(clientAddr.sin_addr.s_addr);
  uint16_t port = ntohs(clientAddr.sin_port);

  std::string ipv4Str = std::to_string((ipv4 >> 24) & 0xFF) + '.' +
                        std::to_string((ipv4 >> 16) & 0xFF) + '.' +
                        std::to_string((ipv4 >> 8) & 0xFF) + '.' +
                        std::to_string(ipv4 & 0xFF);

  clientRef.connection.setPeerAddress(ipv4Str, port);

  const PeerAddress &clientPeerAdress = clientRef.connection.getPeerAddress();

  std::cout << "[Server] : client connected - " << clientPeerAdress.ip << ":"
            << clientPeerAdress.port << "\n";

  return true;
}

void WebServer::removeConnection(const ClientSession &clientSession) {
  int clientFd = clientSession.connection.getFd();
  const PeerAddress &clientPeerAdress =
      clientSession.connection.getPeerAddress();

  std::cout << "[Server] : client disconnected - " << clientPeerAdress.ip << ":"
            << clientPeerAdress.port << "\n";

  _poller.removeFd(clientFd);
  _connectionFds.erase(clientFd);
}

// void WebServer::readFromFd(int fd) {
//   Connection &conn = _connectionFds.at(fd).connection;

//   switch (conn.onReadable()) {
//   case IoResult::Continue:
//     conn.markActivity();
//     break;
//   case IoResult::Complete:
//     conn.markActivity();
//     _poller.modFd(fd, EPOLLOUT | EPOLLRDHUP); // flip to write
//     break;
//   case IoResult::Closed:
//   case IoResult::Error:
//     _poller.removeFd(fd);
//     _connectionFds.erase(fd); // UniqueFd dtor closes fd
//     break;
//   }
// }

}; // namespace webserv
