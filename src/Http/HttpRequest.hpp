#pragma once

#include "HttpTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace webserv {

class HttpRequest {
public:
  HttpRequest() = default;

  void reset() noexcept;
  ParseOutcome append(std::string_view bytes);

  [[nodiscard]] RequestParseState state() const noexcept;
  [[nodiscard]] bool isComplete() const noexcept;
  [[nodiscard]] bool hasError() const noexcept;
  [[nodiscard]] int errorStatus() const noexcept;

  [[nodiscard]] HttpMethod method() const noexcept;
  [[nodiscard]] std::string_view methodText() const noexcept;
  [[nodiscard]] std::string_view target() const noexcept;
  [[nodiscard]] std::string_view path() const noexcept;
  [[nodiscard]] std::string_view queryString() const noexcept;
  [[nodiscard]] std::string_view httpVersion() const noexcept;

  [[nodiscard]] const HeaderMap &headers() const noexcept;
  [[nodiscard]] std::optional<std::string_view>
  header(std::string_view name) const noexcept;
  [[nodiscard]] bool hasHeader(std::string_view name) const noexcept;

  [[nodiscard]] std::string_view body() const noexcept;
  [[nodiscard]] BodyTransferMode bodyTransferMode() const noexcept;
  [[nodiscard]] bool hasContentLength() const noexcept;
  [[nodiscard]] std::size_t contentLength() const noexcept;
  [[nodiscard]] bool isChunked() const noexcept;
  [[nodiscard]] bool keepAliveRequested() const noexcept;
  [[nodiscard]] std::size_t bufferedByteCount() const noexcept;

  static bool isHex(char c);
  static bool decodeHex(char high, char low, char& out);
  bool isValidPercent(std::string_view value);
  bool isValidHeaderName(std::string_view name) const;
  bool isValidHeaderValue(std::string_view value) const;

  void setMaxBodySize(std::size_t bytes) noexcept;
  void setMaxHeaderSize(std::size_t bytes) noexcept; // addded

private:
  bool parseStartLine();
  bool extractLine(std::string& line);
  bool splitStartLine(std::string& line, std::string& method, std::string& target, std::string& version);
  bool validateHttpVersion(std::string& version);
  bool validateTarget(std::string& target);
  bool applyMethod(std::string& method);
  bool parseHeaders();
  bool processHeaders();
  bool validateMandotaryHeader();
  void parseConnectionHeader();
  bool resolveBodyMode();
  bool handleTransferEncoding();
  bool handleContentLength();
  bool finalizeBodyModeFallback();

  bool parseBody();
  bool parseContentLengthBody();
  bool parseChunkedBody();
  bool parseChunkSize();
  bool parseChunkData();
  bool parseChunkTrailer();
  void parseTarget();
  void setError(int statusCode) noexcept;
  void storeHeader(std::string key, std::string value);

  [[nodiscard]] static std::string
  decodeUriComponent(std::string_view value,
                     DecodeMode mode); // changed, notify guys
  [[nodiscard]] static std::string normalizePath(std::string_view path);

  RequestParseState _state{RequestParseState::StartLine};
  int _errorStatus{0};

  std::string _rawBuffer;
  std::size_t _headerEndPos{std::string::npos};
  std::size_t _bodyBytesExpected{0};
  std::size_t _bodyBytesReceived{0};
  std::size_t _currentChunkSize{0};
  std::size_t _maxBodySize{0};
  std::size_t _maxHeaderSize{0}; // added

  std::string _methodText;
  HttpMethod _method{HttpMethod::Unknown};
  std::string _target;
  std::string _path;
  std::string _queryString;
  std::string _httpVersion;

  HeaderMap _headers;
  std::string _body;

  BodyTransferMode _bodyMode{BodyTransferMode::None};
  bool _keepAlive{false};
};

} // namespace webserv
