#pragma once

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpTypes.hpp"
#include "UniqueFd.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace webserv {

class CgiRequest;

struct PeerAddress {
  std::string ip;
  std::uint16_t port{0};
};

struct ConnectionOptions {
  std::chrono::seconds idleTimeout{15};
  std::size_t readChunkSize{16 * 1024};
  std::size_t maxRequestBodySize{0};
  std::size_t maxRequestHeaderSize{8 * 1024};
};

class Connection {
public:
  explicit Connection(UniqueFd clientFd, ConnectionOptions options = {});
  ~Connection();

  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  Connection(Connection &&other) noexcept;
  Connection &operator=(Connection &&other) noexcept;

  [[nodiscard]] int getFd() const noexcept;
  [[nodiscard]] ConnectionState getState() const noexcept;
  [[nodiscard]] const ConnectionOptions &getOptions() const noexcept;

  void setPeerAddress(std::string ip, std::uint16_t port);
  [[nodiscard]] const PeerAddress &getPeerAddress() const noexcept;

  void markActivity(std::chrono::steady_clock::time_point now =
                        std::chrono::steady_clock::now()) noexcept;
  [[nodiscard]] std::chrono::steady_clock::time_point
  lastActivity() const noexcept;
  [[nodiscard]] bool
  isTimedOut(std::chrono::steady_clock::time_point now) const noexcept;

  [[nodiscard]] bool wantsRead() const noexcept;
  [[nodiscard]] bool wantsWrite() const noexcept;
  [[nodiscard]] bool shouldClose() const noexcept;

  [[nodiscard]] IoResult onReadable();
  [[nodiscard]] IoResult onWritable();

  [[nodiscard]] HttpRequest &getRequest() noexcept;
  [[nodiscard]] const HttpRequest &getRequest() const noexcept;
  [[nodiscard]] bool hasCompleteRequest() const noexcept;

  void queueResponse(HttpResponse response);
  void appendResponseBytes(std::string_view bytes);

  void attachCgi(std::unique_ptr<CgiRequest> cgiRequest) noexcept;
  [[nodiscard]] bool hasActiveCgi() const noexcept;
  [[nodiscard]] CgiRequest *getActiveCgi() noexcept;
  [[nodiscard]] const CgiRequest *getActiveCgi() const noexcept;
  [[nodiscard]] std::unique_ptr<CgiRequest> detachCgi() noexcept;

  void prepareForNextRequest();
  void markForClose() noexcept;
  void close() noexcept;

private:
  [[nodiscard]] IoResult readFromSocket();
  [[nodiscard]] IoResult writeToSocket();

  UniqueFd _fd;
  ConnectionOptions _options;
  ConnectionState _state{ConnectionState::ReadingRequest};
  bool _closeAfterWrite{false};

  PeerAddress _peerAddress;
  std::chrono::steady_clock::time_point _lastActivity{};

  std::string _readBuffer;
  std::string _writeBuffer;

  HttpRequest _request;
  bool _requestReady{false};

  // std::unique_ptr<CgiRequest> _activeCgi;
};

} // namespace webserv
