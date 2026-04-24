#include "HttpRequest.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace webserv {

// appends data into _rawBuffer
// state machine loop situation
// stops when needmoredata, error, complete
ParseOutcome HttpRequest::append(std::string_view bytes) {
  _rawBuffer.append(bytes);

  while (69) {
    switch (_state) {
    case RequestParseState::StartLine:
      if (!parseStartLine()) {
        if (_state == RequestParseState::Error)
          return ParseOutcome::Error;
        return ParseOutcome::NeedMoreData;
      }
      break;
    case RequestParseState::Headers:
      if (!parseHeaders()) {
        if (_state == RequestParseState::Error)
          return ParseOutcome::Error;
        return ParseOutcome::NeedMoreData;
      }
      break;
    case RequestParseState::Body:
      if (!parseBody())
        return ParseOutcome::NeedMoreData;
      break;
    case RequestParseState::ChunkSize:
      if (!parseChunkedBody()) {
        if (_state == RequestParseState::Error)
          return ParseOutcome::Error;
        return ParseOutcome::NeedMoreData;
      }
      break;
    case RequestParseState::Complete:
      return ParseOutcome::Complete;
    case webserv::RequestParseState::Error:
      return ParseOutcome::Error;
    default:
      _state = RequestParseState::Error;
      return ParseOutcome::Error;
    }
  }
}

// METHOD SP TARGET SP VERSION
// GET /smth HTTP/1.1

/*
malformed start line	400
unknown method syntax	400
invalid headers	400
invalid body format	400
wrong HTTP version	505
body too large	413
headers too large (optional)	431
*/
bool HttpRequest::parseStartLine() {
  std::string::size_type pos = _rawBuffer.find("\r\n");
  if (pos == std::string::npos)
    return false; // still need to wait for more data from T

  // extract first line
  std::string line = _rawBuffer.substr(0, pos);
  _rawBuffer.erase(0, pos + 2);

  // space checks
  if (line.empty() || std::count(line.begin(), line.end(), ' ') != 2 ||
      line.front() == ' ' || line.back() == ' ')
    return (setError(400), false);

  // space pos
  std::string::size_type sp1 = line.find(' ');
  std::string::size_type sp2 = line.find(' ', sp1 + 1);

  // extract parts
  _methodText = line.substr(0, sp1);
  _target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  _httpVersion = line.substr(sp2 + 1);

  // validation
  _method = parseHttpMethod(_methodText);
  if (_method == HttpMethod::Unknown)
    return (setError(400), false);
  if (_httpVersion != "HTTP/1.1") // only 1.1 ?
    return (setError(505), false);
  if (_target.empty() || _target[0] != '/')
    return (setError(400), false);
  parseTarget();
  if (_state == RequestParseState::Error)
    return false;

  _state = RequestParseState::Headers;
  return true;
}

HttpMethod parseHttpMethod(std::string_view method) noexcept {
  if (method == "GET")
    return HttpMethod::Get;
  if (method == "POST")
    return HttpMethod::Post;
  if (method == "DELETE")
    return HttpMethod::Delete;
  return HttpMethod::Unknown;
}

void HttpRequest::parseTarget() {
  std::string::size_type qmark = _target.find('?');
  std::string rawPath;
  std::string rawQuery;

  if (qmark == std::string::npos) {
    rawPath = _target;
    rawQuery = "";
  } else {
    rawPath = _target.substr(0, qmark);
    rawQuery = _target.substr(qmark + 1);
  };

  if (!isValidPercent(rawPath) || !isValidPercent(rawQuery)) {
    setError(400);
    return;
  }

  std::string decodedPath = decodeUriComponent(rawPath);
  std::string decodedQuery = decodeUriComponent(rawQuery);

  std::string normalized = normalizePath(decodedPath);
  if (normalized.empty() || normalized[0] != '/') {
    setError(400);
    return;
  }

  _path = normalized;
  _queryString = decodedQuery;
}

std::string HttpRequest::decodeUriComponent(std::string_view value) {
  std::string res;
  res.reserve(value.size());

  for (std::size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if (c == '%') {
      char hex[3] = {value[i + 1], value[i + 2], 0};
      char decoded = static_cast<char>(std::strtol(hex, NULL, 16));
      res += decoded;
      i += 2;
    } else if (c == '+')
      res += ' ';
    else
      res += c;
  }
  return res;
}

std::string HttpRequest::normalizePath(std::string_view path) {
  std::filesystem::path p(path);
  p = p.lexically_normal();

  return p.string();
}

bool HttpRequest::isHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

bool HttpRequest::isValidPercent(std::string_view value) {
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%') {
      if (i + 2 >= value.size())
        return false;
      if (!isHex(value[i + 1]) || !isHex(value[i + 2]))
        return false;
      char hex[3] = {value[i + 1], value[i + 2], 0};
      char decoded = static_cast<char>(std::strtol(hex, NULL, 16));
      if (decoded < 32 || decoded == 127)
        return false;
      i += 2;
    }
  }
  return true;
}

bool HttpRequest::parseHeaders() {
  std::size_t headerEnd = _rawBuffer.find("\r\n\r\n");
  if (headerEnd == std::string::npos)
    return false; // when we do not receive full header
  std::string headersSection = _rawBuffer.substr(0, headerEnd);
  _rawBuffer.erase(0, headerEnd + 4);

  std::size_t pos = 0;

  while (pos < headersSection.length()) {
    std::size_t lineEnd = headersSection.find("\r\n", pos);
    if (lineEnd == std::string::npos)
      lineEnd = headersSection.length();
    std::string line = headersSection.substr(pos, lineEnd - pos);
    pos = lineEnd + 2;
    if (line.empty())
      continue;
    std::size_t colonPos = line.find(':');
    if (colonPos == std::string::npos || colonPos == 0)
      return (setError(400), false);
    std::string key = line.substr(0, colonPos);
    std::string value = line.substr(colonPos + 1);

    std::size_t start = value.find_first_not_of(" \t");
    std::size_t end = value.find_last_not_of(" \t");
    if (start == std::string::npos)
      value.clear();
    else
      value = value.substr(start, end - start + 1);

    storeHeader(key, value);
  }
  return processHeaders();
}

// transfer-encoding -> chunked content length must me ignored

bool HttpRequest::processHeaders() {
  if (!hasHeader("Host") || header("Host")->empty())
    return (setError(400), false);
  _keepAlive = true;

  if (hasHeader("Connection")) {
    std::string val = std::string(header("Connection").value());
    for (std::size_t i = 0; i < val.size(); ++i)
      val[i] = static_cast<char>(std::tolower(val[i]));
    if (val == "close")
      _keepAlive = false;
  }

  if (hasHeader("Transfer-Encoding")) {
    std::string val = std::string(header("Transfer-Encoding").value());
    for (std::size_t i = 0; i < val.size(); ++i)
      val[i] = static_cast<char>(std::tolower(val[i]));
    if (val == "chunked") {
      _bodyMode = BodyTransferMode::Chunked;
      _state = RequestParseState::ChunkSize;
      return true;
    }
    return (setError(400), false);
  }

  if (hasHeader("Content-Length")) {
    char *endptr = nullptr;
    unsigned long length = std::strtoul(
        std::string(header("Content-Length").value()).c_str(), &endptr, 10);

    if (*endptr != '\0')
      return (setError(400), false);
    if (_maxBodySize > 0 && length > _maxBodySize)
      return (setError(413), false);
    _bodyBytesExpected = static_cast<std::size_t>(length);
    _bodyMode = BodyTransferMode::ContentLength;
    if (_bodyBytesExpected == 0)
      _state = RequestParseState::Complete;
    else
      _state = RequestParseState::Body;
    return true;
  }
  _bodyMode = BodyTransferMode::None;
  _state = RequestParseState::Complete;
  return true;
}

// optional a value that may or may not exist
std::optional<std::string_view>
HttpRequest::header(std::string_view name) const noexcept {
  auto it = _headers.find(name);
  if (it == _headers.end())
    return std::nullopt;
  return std::string_view(it->second);
}

bool HttpRequest::parseBody() {
  if (_bodyMode == BodyTransferMode::ContentLength)
    return parseContentLengthBody();
  if (_bodyMode == BodyTransferMode::Chunked)
    return parseChunkedBody();
  return true;
}

// reads the request body when we know its size
bool HttpRequest::parseContentLengthBody() {
  std::size_t available = _rawBuffer.size();
  std::size_t remainig = _bodyBytesExpected - _bodyBytesReceived;

  std::size_t toRead = std::min(available, remainig);
  _body.append(_rawBuffer.substr(0, toRead));
  _rawBuffer.erase(0, toRead);

  _bodyBytesReceived += toRead;
  if (_bodyBytesReceived == _bodyBytesExpected) {
    _state = RequestParseState::Complete;
    return true;
  }
  return false;
}

bool HttpRequest::parseChunkedBody() {
  while (69) {

    // read chunk size 4\r\n
    if (_state == RequestParseState::ChunkSize) {
      std::size_t pos = _rawBuffer.find("\r\n");
      if (pos == std::string::npos)
        return false; // more data needed

      std::string sizeStr = _rawBuffer.substr(0, pos);
      _rawBuffer.erase(0, pos + 2);

      char *endptr = nullptr;
      unsigned long size = std::strtoul(sizeStr.c_str(), &endptr, 16);
      if (*endptr != '\0')
        return (setError(400), false);

      _currentChunkSize = size;

      if (_maxBodySize > 0 && _currentChunkSize > _maxBodySize)
        return (setError(413), false);

      if (_currentChunkSize == 0) {
        _state = RequestParseState::ChunkTrailer;
      } else {
        _state = RequestParseState::ChunkData;
      }
    }
    // read chunk data yolo\r\n
    if (_state == RequestParseState::ChunkData) {
      if (_rawBuffer.size() < _currentChunkSize + 2)
        return false; // wait for full chunk + crlf
      if (_maxBodySize > 0 && _body.size() + _currentChunkSize > _maxBodySize)
        return (setError(413), false);
      // appedn chunk data
      _body.append(_rawBuffer.substr(0, _currentChunkSize));
      _rawBuffer.erase(0, _currentChunkSize);
      // check trailing crlf
      if (_rawBuffer.substr(0, 2) != "\r\n")
        return (setError(400), false);
      _rawBuffer.erase(0, 2);
      // to the next chunk
      _state = RequestParseState::ChunkSize;
    }
    // 0\r\n\r\n
    if (_state == RequestParseState::ChunkTrailer) {
      // expecting final crlf
      if (_rawBuffer.size() < 2)
        return false;
      if (_rawBuffer.substr(0, 2) != "\r\n")
        return (setError(400), false);
      _rawBuffer.erase(0, 2);
      _state = RequestParseState::Complete;
      return true;
    }
  }
}

bool HttpRequest::hasContentLength() const noexcept {
  return _bodyMode == BodyTransferMode::ContentLength;
}

std::size_t HttpRequest::contentLength() const noexcept {
  return _bodyBytesExpected;
}

bool HttpRequest::isChunked() const noexcept {
  return _bodyMode == BodyTransferMode::Chunked;
}

bool HttpRequest::keepAliveRequested() const noexcept { return _keepAlive; }

std::size_t HttpRequest::bufferedByteCount() const noexcept {
  return _rawBuffer.size();
}

void HttpRequest::storeHeader(std::string key, std::string value) {
  if (key == "Content-Length" && hasHeader(key)) {
    setError(400);
    return;
  }
  _headers[std::move(key)] = std::move(value);
}

bool HttpRequest::hasHeader(std::string_view name) const noexcept {
  return _headers.find(name) != _headers.end();
}

void HttpRequest::setError(int statusCode) noexcept {
  _errorStatus = statusCode;
  _state = RequestParseState::Error;
}

RequestParseState HttpRequest::state() const noexcept { return _state; }

bool HttpRequest::isComplete() const noexcept {
  return _state == RequestParseState::Complete;
}

bool HttpRequest::hasError() const noexcept {
  return _state == RequestParseState::Error;
}
int HttpRequest::errorStatus() const noexcept { return _errorStatus; }

HttpMethod HttpRequest::method() const noexcept { return _method; }
std::string_view HttpRequest::methodText() const noexcept {
  return _methodText;
}

std::string_view HttpRequest::target() const noexcept { return _target; }

std::string_view HttpRequest::path() const noexcept { return _path; }

std::string_view HttpRequest::queryString() const noexcept {
  return _queryString;
}

std::string_view HttpRequest::httpVersion() const noexcept {
  return _httpVersion;
}

const HeaderMap &HttpRequest::headers() const noexcept { return _headers; }

std::string_view HttpRequest::body() const noexcept { return _body; }

BodyTransferMode HttpRequest::bodyTransferMode() const noexcept {
  return _bodyMode;
}

void HttpRequest::setMaxBodySize(std::size_t bytes) noexcept {
  _maxBodySize = bytes;
}

void HttpRequest::reset() noexcept {
  _state = RequestParseState::StartLine;
  _errorStatus = 0;

  _rawBuffer.clear();
  _headerEndPos = std::string::npos;
  _bodyBytesExpected = 0;
  _bodyBytesReceived = 0;
  _currentChunkSize = 0;
  _maxBodySize = 0;

  _methodText.clear();
  _method = HttpMethod::Unknown;
  _target.clear();
  _path.clear();
  _queryString.clear();
  _httpVersion.clear();

  _headers.clear();
  _body.clear();

  _bodyMode = BodyTransferMode::None;
  _keepAlive = false;
}

} // namespace webserv