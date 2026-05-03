#include "CgiRequest.hpp"
#include "Connection.hpp"
#include "HttpRequest.hpp"
#include "HttpTypes.hpp"
#include "ServerConfig.hpp"
#include "UniqueFd.hpp"
#include "WebServer.hpp"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace webserv {

void WebServer::run() {
  setupSignals();

  for (ServerConfig &serverConfig : _servers)
    createServer(serverConfig);

  while (!_shouldStop) {
    EventsData events = _poller.getEventsReady();

    for (const epoll_event &event : events) {
      const int fd = event.data.fd;

      switch (_poller.getFdType(fd)) {
      case FdType::Signal:
        handleSignal(fd);
        break;
      case FdType::Listener:
        while (acceptConnection(_listeningFds.at(fd))) {
        }
        break;
      case FdType::Client:
        handleClientEvent(fd, event.events);
        break;
      case FdType::CgiStdIn:
        processCgiInput(fd);
        break;
      case FdType::CgiStdOut:
        processCgiOutput(fd, event.events);
        break;
      case FdType::Unknown:
        _poller.removeFd(fd);
        break;
      }

      if (_shouldStop)
        break;
    }

    closeIdleConnections();
  }
}

void WebServer::handleSignal(int fd) {
  signalfd_siginfo si;
  while (read(fd, &si, sizeof(si)) == sizeof(si)) {
  }
  std::cout << "\n[Server] : shutdown signal received" << std::endl;
  _shouldStop = true;
}

void WebServer::handleClientEvent(int fd, uint32_t events) {
  auto it = _connectionFds.find(fd);
  if (it == _connectionFds.end()) {
    _poller.removeFd(fd);
    return;
  }

  if (events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
    removeConnection(it->second);
    return;
  }
  if (events & EPOLLIN) {
    readRequest(fd);
    return;
  }
  if (events & EPOLLOUT)
    writeResponse(fd);
}

void WebServer::closeIdleConnections() {
  const auto now = std::chrono::steady_clock::now();

  std::vector<int> timedOut;
  for (const auto &[fd, session] : _connectionFds) {
    if (session.connection.isTimedOut(now))
      timedOut.push_back(fd);
  }

  for (int fd : timedOut) {
    auto it = _connectionFds.find(fd);
    if (it == _connectionFds.end())
      continue;

    Connection &conn = it->second.connection;
    const PeerAddress &peer = conn.getPeerAddress();
    std::cout << "[Server] : client timed out - " << peer.ip << ":" << peer.port
              << std::endl;

    if (conn.getState() == ConnectionState::ReadingRequest)
      queueError(fd, it->second.defaultServer, 408);
    else
      removeConnection(it->second);
  }
}

bool WebServer::acceptConnection(const Listener &listener) {
  const int listenerFd = listener.fd.get();

  sockaddr_in clientAddr{};
  socklen_t clientLen = sizeof(clientAddr);

  UniqueFd clientFd(accept(
      listenerFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen));

  if (clientFd.get() == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return false;
    throw std::runtime_error("accept failed");
  }

  const int rawClientFd = clientFd.get();
  try {
    setNonblockingFlag(clientFd);
    _poller.addFd(rawClientFd, EPOLLIN | EPOLLRDHUP, FdType::Client);
  } catch (const std::exception &e) {
    std::cerr << "[Server] : dropping client after accept setup failure: "
              << e.what() << std::endl;
    return true;
  }

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

  const PeerAddress &clientPeerAddress = clientRef.connection.getPeerAddress();

  std::cout << "[Server] : client connected - " << clientPeerAddress.ip << ":"
            << clientPeerAddress.port << std::endl;

  return true;
}

void WebServer::removeConnection(ClientSession &clientSession) noexcept {
  const int clientFd = clientSession.connection.getFd();

  if (CgiRequest *cgi = clientSession.connection.getActiveCgi()) {
    const int stdinFd = cgi->stdinFd();
    const int stdoutFd = cgi->stdoutFd();
    if (stdinFd != -1) {
      _poller.removeFd(stdinFd);
      _cgiFdToClient.erase(stdinFd);
    }
    if (stdoutFd != -1) {
      _poller.removeFd(stdoutFd);
      _cgiFdToClient.erase(stdoutFd);
    }
  }

  clientSession.connection.close();
  _poller.removeFd(clientFd);
  _connectionFds.erase(clientFd);
}

void WebServer::readRequest(int fd) {
  ClientSession &session = _connectionFds.at(fd);
  Connection &conn = session.connection;

  switch (conn.onReadable()) {
  case IoResult::Continue:
    conn.markActivity();
    break;
  case IoResult::Complete:
    conn.markActivity();
    processRequest(fd);
    break;
  case IoResult::Closed:
    removeConnection(session);
    break;
  case IoResult::Error:
    if (conn.getRequest().hasError())
      queueError(fd, session.defaultServer, conn.getRequest().errorStatus());
    else
      removeConnection(session);
    break;
  }
}

void WebServer::writeResponse(int fd) {
  auto it = _connectionFds.find(fd);
  if (it == _connectionFds.end()) {
    _poller.removeFd(fd);
    return;
  }

  switch (it->second.connection.onWritable()) {
  case IoResult::Continue:
    break;
  case IoResult::Complete:
    _poller.modFd(fd, EPOLLIN | EPOLLRDHUP);
    break;
  case IoResult::Closed:
  case IoResult::Error:
    removeConnection(it->second);
    break;
  }
}

} // namespace webserv
