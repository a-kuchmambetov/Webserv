#include "Connection.hpp"
#include "CgiRequest.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpTypes.hpp"

#include <cerrno>
#include <cstddef>
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

void Connection::setPeerAddress(std::string ip, std::uint16_t port) {
  _peerAddress = {ip, port};
}

const PeerAddress &Connection::getPeerAddress() const noexcept {
  return _peerAddress;
}

// IoResult Connection::onReadable() {
//   if (_state != ConnectionState::ReadingRequest)
//     return IoResult::Continue;

//   IoResult readResult = readFromSocket();
//   if (readResult != IoResult::Continue)
//     return readResult;

//   if (_options.maxRequestBodySize > 0)
//     _request.setMaxBodySize(_options.maxRequestBodySize);

//   ParseOutcome outcome = _request.append(_readBuffer);
//   _readBuffer.clear();

//   switch (outcome) {
//   case ParseOutcome::NeedMoreData:
//     return IoResult::Continue;
//   case ParseOutcome::Complete:
//     _requestReady = true;
//     _state = ConnectionState::ProcessingRequest;
//     return IoResult::Complete;
//   case ParseOutcome::Error:
//     return IoResult::Error;
//   }
//   return IoResult::Error;
// }

// IoResult Connection::readFromSocket() {
//   std::vector<char> buffer(_options.readChunkSize);
//   ssize_t bytesRead = recv(_fd.get(), buffer.data(), buffer.size(), 0);

//   if (bytesRead > 0) {
//     _readBuffer.append(buffer.data(), static_cast<std::size_t>(bytesRead));
//     return IoResult::Continue;
//   }
//   if (bytesRead == 0)
//     return IoResult::Closed;
//   if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
//     return IoResult::Continue;
//   return IoResult::Error;
// }

} // namespace webserv
