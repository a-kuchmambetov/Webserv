#include "CgiResult.hpp"
#include "HttpResponse.hpp"

#include <sstream>
#include <string>

namespace webserv {

void CgiResult::reset() noexcept {
  _headersParsed = false;
  _complete = false;
  _errorStatus = 0;
  _statusCode = 200;
  _rawBuffer.clear();
  _headers.clear();
  _body.clear();
}

ParseOutcome CgiResult::append(std::string_view bytes) {
  if (_errorStatus != 0)
    return ParseOutcome::Error;

  _rawBuffer.append(bytes);

  if (!_headersParsed) {
    if (!parseHeadersIfReady())
      return (_errorStatus != 0) ? ParseOutcome::Error
                                 : ParseOutcome::NeedMoreData;
  }

  return ParseOutcome::NeedMoreData;
}

void CgiResult::finalizeAtEof() {
  if (!_headersParsed) {
    if (!parseHeadersIfReady()) {
      setError(502);
      return;
    }
  }
  _body = std::move(_rawBuffer);
  _rawBuffer.clear();
  _complete = true;
}

bool CgiResult::parseHeadersIfReady() {
  // CGI output uses either CRLF or bare LF line endings
  std::size_t pos = _rawBuffer.find("\r\n\r\n");
  std::size_t sepLen = 4;
  if (pos == std::string::npos) {
    pos = _rawBuffer.find("\n\n");
    sepLen = 2;
  }
  if (pos == std::string::npos)
    return false;

  std::string headerSection = _rawBuffer.substr(0, pos);
  _rawBuffer = _rawBuffer.substr(pos + sepLen);

  std::istringstream stream(headerSection);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;

    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
      setError(502);
      return false;
    }

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    std::size_t vstart = value.find_first_not_of(" \t");
    if (vstart != std::string::npos)
      value = value.substr(vstart);

    _headers[std::move(name)] = std::move(value);
  }

  _headersParsed = true;
  applySpecialCgiHeaders();
  return true;
}

void CgiResult::applySpecialCgiHeaders() {
  // "Status: 404 Not Found" → status code for the HTTP response
  auto it = _headers.find("Status");
  if (it != _headers.end()) {
    try {
      _statusCode = std::stoi(it->second);
    } catch (...) {
      _statusCode = 200;
    }
    _headers.erase(it);
  }

  // "Location" without an explicit Status implies 302
  if (_headers.count("Location") > 0 && _statusCode == 200)
    _statusCode = 302;
}

void CgiResult::setError(int statusCode) noexcept { _errorStatus = statusCode; }

bool CgiResult::headersParsed() const noexcept { return _headersParsed; }
bool CgiResult::isComplete() const noexcept { return _complete; }
bool CgiResult::hasError() const noexcept { return _errorStatus != 0; }
int CgiResult::errorStatus() const noexcept { return _errorStatus; }
int CgiResult::statusCode() const noexcept { return _statusCode; }
const HeaderMap &CgiResult::headers() const noexcept { return _headers; }

std::optional<std::string_view>
CgiResult::header(std::string_view name) const noexcept {
  auto it = _headers.find(std::string(name));
  if (it == _headers.end())
    return std::nullopt;
  return std::string_view(it->second);
}

std::string_view CgiResult::body() const noexcept { return _body; }

HttpResponse CgiResult::toHttpResponse() const {
  HttpResponse response;
  response.setStatus(_statusCode);
  for (const auto &[name, value] : _headers)
    response.setHeader(name, value);
  if (!_body.empty())
    response.setBody(_body);
  return response;
}

} // namespace webserv
