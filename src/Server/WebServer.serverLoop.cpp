#include "WebServer.hpp"
#include "Connection.hpp"
#include "HttpTypes.hpp"
#include "Poller.hpp"
#include "ServerConfig.hpp"
#include "UniqueFd.hpp"

#include <cerrno>
#include <chrono>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace webserv {

void WebServer::run() {
  setupSignals();

  // Create listening sockets for each server config
  for (ServerConfig &serverConfig : _servers)
    createServer(serverConfig);

  while (!_shouldStop) {
    EventsData eventData = _poller.getEventsReady();
    int n = eventData.eventsReadyN;
    std::vector<epoll_event> &events = *eventData.events;

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
        std::cout << "\n[Server] : shutdown signal received" << std::endl;
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
        readRequest(fd);
      }
      // Client response from server
      else if (events[i].events & EPOLLOUT) {
        writeResponse(fd);
      }
    }

    closeIdleConnections();
  }
}

void WebServer::closeIdleConnections() {
  auto now = std::chrono::steady_clock::now();

  std::vector<int> timedOut;
  for (const auto &[fd, session] : _connectionFds) {
    if (session.connection.isTimedOut(now))
      timedOut.push_back(fd);
  }

  for (int fd : timedOut) {
    ClientSession &client = _connectionFds.at(fd);

    removeConnection(client);
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

  ConnectionOptions options;
  if (listener.serverConfig) {
    options.maxRequestBodySize = listener.serverConfig->clientMaxBodySize();
    options.maxRequestHeaderSize = listener.serverConfig->clientMaxHeaderSize();
  }

  _connectionFds.emplace(rawClientFd,
                         ClientSession{Connection(std::move(clientFd), options),
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
            << clientPeerAdress.port << std::endl;

  return true;
}

void WebServer::removeConnection(ClientSession &clientSession) noexcept {
  int clientFd = clientSession.connection.getFd();

  clientSession.connection.close();
  _poller.removeFd(clientFd);
  _connectionFds.erase(clientFd);
}

void WebServer::readRequest(int fd) {
  Connection &conn = _connectionFds.at(fd).connection;

  switch (conn.onReadable()) {
  case IoResult::Continue:
    conn.markActivity();
    break;
  case IoResult::Complete:
    conn.markActivity();
    {
      const auto body = conn.getRequest().body();
      std::cout << "[Server] : request body (" << body.size() << " bytes):\n"
                << body << std::endl;
    }
    _poller.modFd(fd, EPOLLOUT | EPOLLRDHUP);
    break;
  case IoResult::Closed:
  case IoResult::Error:
    _poller.removeFd(fd);
    _connectionFds.erase(fd); // UniqueFd dtor closes fd
    break;
  }
}

void WebServer::buildResponse(int fd) {
  Connection &conn = _connectionFds.at(fd).connection;
  (void)conn;
}

void WebServer::writeResponse(int fd) {
  Connection &conn = _connectionFds.at(fd).connection;
  (void)conn;
}

}; // namespace webserv
