#include "Connection.hpp"
#include "CgiRequest.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpTypes.hpp"
#include "UniqueFd.hpp"

#include <cerrno>
#include <cstddef>
#include <iostream>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <sys/types.h>

namespace webserv {

Connection::Connection(UniqueFd clientFd, ConnectionOptions options)
    : _fd(std::move(clientFd)), _options(options),
      _lastActivity(std::chrono::steady_clock::now()) {}

Connection::~Connection() = default;

Connection::Connection(Connection &&other) noexcept = default;

Connection &Connection::operator=(Connection &&other) noexcept = default;

int Connection::getFd() const noexcept { return _fd.get(); }

ConnectionState Connection::getState() const noexcept { return _state; }

const ConnectionOptions &Connection::getOptions() const noexcept {
  return _options;
}

void Connection::setPeerAddress(std::string ip, std::uint16_t port) {
  _peerAddress = {ip, port};
}

const PeerAddress &Connection::getPeerAddress() const noexcept {
  return _peerAddress;
}

void Connection::markActivity(
    std::chrono::steady_clock::time_point now) noexcept {
  _lastActivity = now;
}

std::chrono::steady_clock::time_point
Connection::lastActivity() const noexcept {
  return _lastActivity;
}

bool Connection::isTimedOut(
    std::chrono::steady_clock::time_point now) const noexcept {
  return now - _lastActivity >= _options.idleTimeout;
}

bool Connection::wantsRead() const noexcept {
  return _state == ConnectionState::ReadingRequest;
}

bool Connection::wantsWrite() const noexcept {
  return _state == ConnectionState::WritingResponse && !_writeBuffer.empty();
}

bool Connection::shouldClose() const noexcept { return _closeAfterWrite; }

IoResult Connection::onReadable() {
  if (_state != ConnectionState::ReadingRequest)
    return IoResult::Continue;

  IoResult readResult = readFromSocket();
  if (readResult != IoResult::Continue)
    return readResult;

  if (_options.maxRequestBodySize > 0)
    _request.setMaxBodySize(_options.maxRequestBodySize);
  if (_options.maxRequestHeaderSize > 0)
    _request.setMaxHeaderSize(
        _options.maxRequestHeaderSize); // IMPORTANT: waiting for implementation

  ParseOutcome outcome = _request.append(_readBuffer);
  _readBuffer.clear();

  switch (outcome) {
  case ParseOutcome::NeedMoreData:
    return IoResult::Continue;
  case ParseOutcome::Complete:
    _requestReady = true;
    _state = ConnectionState::ProcessingRequest;
    return IoResult::Complete;
  case ParseOutcome::Error:
    return IoResult::Error;
  }
  return IoResult::Error;
}

IoResult Connection::onWritable() { return writeToSocket(); }

HttpRequest &Connection::getRequest() noexcept { return _request; }

const HttpRequest &Connection::getRequest() const noexcept { return _request; }

bool Connection::hasCompleteRequest() const noexcept { return _requestReady; }

void Connection::appendResponseBytes(std::string_view bytes) {
  _writeBuffer.append(bytes);
}

void Connection::prepareForNextRequest() {
  _request.reset();
  _requestReady = false;
  _readBuffer.clear();
  _writeBuffer.clear();
  _closeAfterWrite = false;
  _state = ConnectionState::ReadingRequest;
}

void Connection::markForClose() noexcept { _closeAfterWrite = true; }

IoResult Connection::readFromSocket() {
  std::vector<char> buffer(_options.readChunkSize);
  ssize_t bytesRead = recv(_fd.get(), buffer.data(), buffer.size(), 0);

  if (bytesRead > 0) {
    _readBuffer.append(buffer.data(), static_cast<std::size_t>(bytesRead));
    return IoResult::Continue;
  }
  if (bytesRead == 0)
    return IoResult::Closed;
  if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
    return IoResult::Continue;
  return IoResult::Error;
}

IoResult Connection::writeToSocket() {
  while (!_writeBuffer.empty()) {
    ssize_t bytesSent =
        send(_fd.get(), _writeBuffer.data(), _writeBuffer.size(), MSG_NOSIGNAL);

    if (bytesSent > 0) {
      _writeBuffer.erase(0, static_cast<std::size_t>(bytesSent));
      continue;
    }
    if (bytesSent == 0)
      return IoResult::Continue;
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return IoResult::Continue;
    return IoResult::Error;
  }

  if (_closeAfterWrite) {
    _state = ConnectionState::Closed;
    return IoResult::Closed;
  }

  prepareForNextRequest();
  return IoResult::Complete;
}

void Connection::close() noexcept {

  const ConnectionState state = getState();
  const PeerAddress &peer = getPeerAddress();

  if (state == ConnectionState::ReadingRequest) {
    std::cout << "[Server] : client timed out - " << peer.ip << ":" << peer.port
              << std::endl;
    std::string resMsg =
        "HTTP/1.1 408 Request Timeout\r\n"
        "Connection: close\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 48\r\n"
        "\r\b"
        "<html><body><h1>408 Request Timeout</h1></body></html>\n";
    send(_fd.get(), resMsg.data(), resMsg.size(), MSG_NOSIGNAL);
  } else {
    std::cout << "[Server] : client disconnected by server - " << peer.ip << ":"
              << peer.port << std::endl;
  }

  _state = ConnectionState::Closed;
}

} // namespace webserv
