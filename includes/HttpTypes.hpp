#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace webserv {

enum class HttpMethod : std::uint8_t {
  Get,
  Post,
  Delete,
  Unknown,
};

enum class RequestParseState : std::uint8_t {
  StartLine,
  Headers,
  Body,
  ChunkSize,
  ChunkData,
  ChunkTrailer,
  Complete,
  Error,
};

enum class BodyTransferMode : std::uint8_t {
  None,
  ContentLength,
  Chunked,
};

enum class ParseOutcome : std::uint8_t {
  NeedMoreData,
  Complete,
  Error,
};

enum class IoResult : std::uint8_t {
  Continue,
  Complete,
  Closed,
  Error,
};

enum class FdType : std::uint8_t {
  Listener,
  Client,
  CgiStdIn,
  CgiStdOut,
  Unknown,
};

enum class ConnectionState : std::uint8_t {
  ReadingRequest,
  ProcessingRequest,
  RunningCgi,
  WritingResponse,
  Closing,
  Closed,
};

enum class ConnectionPreference : std::uint8_t {
  KeepAlive,
  Close,
};

struct CaseInsensitiveLess {
  using is_transparent = void;

  [[nodiscard]] bool operator()(std::string_view lhs,
                                std::string_view rhs) const noexcept;
};

using HeaderMap = std::map<std::string, std::string, CaseInsensitiveLess>;
using EnvironmentMap = std::map<std::string, std::string>;
using MethodSet = std::set<HttpMethod>;
using ErrorPageMap = std::map<int, std::filesystem::path>;
using CgiHandlerMap = std::map<std::string, std::filesystem::path>;

struct ListenEndpoint {
  std::string host{"0.0.0.0"};
  std::uint16_t port{0};

  ListenEndpoint() = default;
  ListenEndpoint(std::string endpointHost, std::uint16_t endpointPort)
      : host(std::move(endpointHost)), port(endpointPort) {}
};

struct RedirectRule {
  int statusCode{302};
  std::string target;

  RedirectRule() = default;
  RedirectRule(int code, std::string url)
      : statusCode(code), target(std::move(url)) {}
};

[[nodiscard]] std::string_view toString(HttpMethod method) noexcept;
[[nodiscard]] HttpMethod parseHttpMethod(std::string_view method) noexcept;
[[nodiscard]] std::string reasonPhraseFor(int statusCode);
[[nodiscard]] std::string normalizeHeaderName(std::string_view name);

} // namespace webserv
