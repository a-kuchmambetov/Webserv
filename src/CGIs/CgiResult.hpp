#pragma once

#include "HttpTypes.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace webserv {

class HttpResponse;

class CgiResult {
public:
  CgiResult() = default;

  void reset() noexcept;
  ParseOutcome append(std::string_view bytes);
  void finalizeAtEof();

  [[nodiscard]] bool headersParsed() const noexcept;
  [[nodiscard]] bool isComplete() const noexcept;
  [[nodiscard]] bool hasError() const noexcept;
  [[nodiscard]] int errorStatus() const noexcept;

  [[nodiscard]] int statusCode() const noexcept;
  [[nodiscard]] const HeaderMap &headers() const noexcept;
  [[nodiscard]] std::optional<std::string_view>
  header(std::string_view name) const noexcept;
  [[nodiscard]] std::string_view body() const noexcept;

  [[nodiscard]] HttpResponse toHttpResponse() const;

private:
  bool parseHeadersIfReady();
  void applySpecialCgiHeaders();
  void setError(int statusCode) noexcept;

  bool _headersParsed{false};
  bool _complete{false};
  int _errorStatus{0};
  int _statusCode{200};

  std::string _rawBuffer;
  HeaderMap _headers;
  std::string _body;
};

} // namespace webserv
