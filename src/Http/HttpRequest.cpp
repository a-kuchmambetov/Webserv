#include "HttpRequest.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace webserv {

namespace {

HttpMethod parseRequestMethod(std::string_view method) noexcept {
  if (method == "GET")
    return HttpMethod::Get;
  if (method == "POST")
    return HttpMethod::Post;
  if (method == "DELETE")
    return HttpMethod::Delete;
  return HttpMethod::Unknown;
}

} // namespace

// appends data into _rawBuffer
// state machine loop situation
// stops when needmoredata, error, complete
ParseOutcome HttpRequest::append(std::string_view bytes) {
  if (_state == RequestParseState::Error ||
      _state == RequestParseState::Complete)
    return _state == RequestParseState::Complete ? ParseOutcome::Complete
                                                 : ParseOutcome::Error;
  _rawBuffer.append(bytes);

  // not sure if header size needs to be here 
  if (_maxHeaderSize > 0 && _state != RequestParseState::Complete) {
    if (_rawBuffer.size() > _maxHeaderSize) {
      if (_rawBuffer.find("\r\n\r\n") == std::string::npos) {
        setError(431);
        return ParseOutcome::Error;
      }
    }
  }
  while (true) {
    switch (_state) {
    case RequestParseState::StartLine:
      if (!parseStartLine()) {
        return (_state == RequestParseState::Error)
                   ? ParseOutcome::Error
                   : ParseOutcome::NeedMoreData;
      }
      break;
    case RequestParseState::Headers:
      if (!parseHeaders()) {
        return (_state == RequestParseState::Error)
                   ? ParseOutcome::Error
                   : ParseOutcome::NeedMoreData;
      }
      break;
    case RequestParseState::Body:
      if (!parseBody())
        return ParseOutcome::NeedMoreData;
      break;
    case RequestParseState::ChunkSize:
    case RequestParseState::ChunkData:
    case RequestParseState::ChunkTrailer:
      if (!parseChunkedBody()) {
        if (_state == RequestParseState::Error)
          return ParseOutcome::Error;
        // if (_bodyMode == BodyTransferMode::Chunked &&
        //     (_state == RequestParseState::ChunkSize ||
        //      _state == RequestParseState::ChunkData) &&
        //     _rawBuffer.empty()) {
        //   setError(400);
        //   return ParseOutcome::Error;
        // }
        return ParseOutcome::NeedMoreData;
      }
      break;
    case RequestParseState::Complete:
      return ParseOutcome::Complete;
    case RequestParseState::Error:
      return ParseOutcome::Error;
    default:
      setError(500);
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

// extracting and validating request line


bool HttpRequest::parseStartLine() {
  std::string line;
  if (!extractLine(line))
    return false;

  std::string method, target, version;
  if (!splitStartLine(line, method, target, version))
    return false;
  if (!validateHttpVersion(version))
    return false;
  if (!validateTarget(target))
    return false;
  if (!applyMethod(method))
    return false;

  _methodText = method;
  _target = target;
  _httpVersion = version;

  parseTarget();
  if (_state == RequestParseState::Error)
    return false;
  _state = RequestParseState::Headers;
  return true;
}

bool HttpRequest::extractLine(std::string &line) {
  std::size_t pos = _rawBuffer.find("\r\n");
  if (pos == std::string::npos)
    return false;

  line = _rawBuffer.substr(0, pos);
  _rawBuffer.erase(0, pos + 2);

  if (line.empty())
    return (setError(400), false);

  return true;
}

bool HttpRequest::splitStartLine(std::string &line, std::string &method,
                                 std::string &target, std::string &version) {
  int spaceCount = std::count(line.begin(), line.end(), ' ');

  if (spaceCount != 2 || line.front() == ' ' || line.back() == ' ')
    return (setError(400), false);
  std::string::size_type sp1 = line.find(' ');
  std::string::size_type sp2 = line.find(' ', sp1 + 1);

  method = line.substr(0, sp1);
  target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  version = line.substr(sp2 + 1);

  if (method.empty() || target.empty() || version.empty())
    return (setError(400), false);

  return true;
}

bool HttpRequest::validateHttpVersion(std::string &version) {
  if (version.rfind("HTTP/", 0) != 0)
    return (setError(400), false);

  std::string ver = version.substr(5);
  std::size_t dot = ver.find('.');
  if (dot == std::string::npos)
    return (setError(400), false);

  std::string left = ver.substr(0, dot);
  std::string right = ver.substr(dot + 1);
  if (left.empty() || right.empty())
    return (setError(400), false);

  for (char c : left)
    if (!std::isdigit(static_cast<unsigned char>(c)))
      return (setError(400), false);
  for (char c : right)
    if (!std::isdigit(static_cast<unsigned char>(c)))
      return (setError(400), false);

  if (version != "HTTP/1.1")
    return (setError(505), false);
  return true;
}

bool HttpRequest::validateTarget(std::string &target) {
  if (target.empty() || target[0] != '/')
    return (setError(400), false);
  return true;
}

bool HttpRequest::applyMethod(std::string &method) {
  _method = parseRequestMethod(method);
  if (_method == HttpMethod::Unknown)
    return (setError(501), false);
  return true;
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

  std::string decodedPath = decodeUriComponent(rawPath, DecodeMode::Path);
  std::string decodedQuery = decodeUriComponent(rawQuery, DecodeMode::Query);

  std::string normalized = normalizePath(decodedPath);
  if (normalized.empty() || normalized[0] != '/') {
    setError(400);
    return;
  }

  _path = normalized;
  _queryString = decodedQuery;
}

std::string HttpRequest::decodeUriComponent(std::string_view value,
                                            DecodeMode mode) {
  std::string res;
  res.reserve(value.size());

  for (std::size_t i = 0; i < value.size(); ++i) {
    char c = value[i];

    if (mode == DecodeMode::Query && c == '+') {
      res += ' ';
    } else if (c == '%') {
      char hex[3] = {value[i + 1], value[i + 2], 0};
      char decoded = static_cast<char>(std::strtol(hex, nullptr, 16));
      res += decoded;
      i += 2;
    } else {
      res += c;
    }
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

// extracting raw header lines from _rawBuffer
/*
Host: example.com\r\n
Content-Length: 5\r\n
\r\n
HELLO
*/

bool HttpRequest::parseHeaders() {
  std::size_t headerEnd = _rawBuffer.find("\r\n\r\n");
  if (headerEnd == std::string::npos)
    return false;

  if (_maxHeaderSize > 0 && headerEnd > _maxHeaderSize) {
    setError(431);
    return false;
  }

  std::string headersSection = _rawBuffer.substr(0, headerEnd);
  _rawBuffer.erase(0, headerEnd + 4);

  std::size_t pos = 0;
  while (pos < headersSection.size()) {
    std::size_t lineEnd = headersSection.find("\r\n", pos);
    if (lineEnd == std::string::npos)
      lineEnd = headersSection.size();

    std::string line = headersSection.substr(pos, lineEnd - pos);
    pos = lineEnd + 2;

    if (line.empty())
      continue;

    if (line[0] == ' ' || line[0] == '\t')
      return (setError(400), false);

    if (std::count(line.begin(), line.end(), ':') != 1)
      return (setError(400), false);

    std::size_t colonPos = line.find(':');
    if (colonPos == std::string::npos || colonPos == 0)
      return (setError(400), false);

    std::string key = line.substr(0, colonPos);
    std::string value = line.substr(colonPos + 1);

    std::size_t start = value.find_first_not_of(" \t");
    std::size_t end = value.find_last_not_of(" \t");
    value = (start == std::string::npos) ? ""
                                         : value.substr(start, end - start + 1);

    if (!isValidHeaderName(key) || !isValidHeaderValue(value))
      return (setError(400), false);
    storeHeader(std::move(key), std::move(value));
    if (_state == RequestParseState::Error)
      return false;
  }
  return processHeaders();
}
bool HttpRequest::isValidHeaderName(std::string_view name) const {
  if (name.empty())
    return false;
  for (char c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc <= 32 || uc == 127 || uc == ':' || uc == '(' || uc == ')' ||
        uc == '<' || uc == '>' || uc == '@' || uc == ',' || uc == ';' ||
        uc == '\\' || uc == '"' || uc == '/' || uc == '[' || uc == ']' ||
        uc == '?' || uc == '=' || uc == '{' || uc == '}')
      return false;
  }
  return true;
}

bool HttpRequest::isValidHeaderValue(std::string_view value) const {
  for (char c : value) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc == 127)
      return false;
  }
  return true;
}

// case insensitive
// stores headers
// rejects CL H TE if they appear more than once

void HttpRequest::storeHeader(std::string key, std::string value) {
  std::string normalized = normalizeHeaderName(key);

  if (_headers.find(normalized) != _headers.end()) {
    if (normalized == "Content-Length" || normalized == "Host" ||
        normalized == "Transfer-Encoding") {
      setError(400);
      return;
    }
  }

  _headers[std::move(normalized)] = std::move(value);
}

// transfer-encoding -> chunked content length must me ignored

bool HttpRequest::processHeaders() {
  if (!validateMandotaryHeader())
    return false;

  parseConnectionHeader();

  if (!resolveBodyMode())
    return false;

  return true;
}

bool HttpRequest::validateMandotaryHeader() {
  auto host = header("Host");
  if (!host || host->empty())
    return (setError(400), false);
  return true;
}

void HttpRequest::parseConnectionHeader() {
  _keepAlive = true;

  if (auto conn = header("Connection")) {
    std::string val(conn->begin(), conn->end());
    for (char &c : val)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (val == "close")
      _keepAlive = false;
  }
}

bool HttpRequest::resolveBodyMode() {
  if (auto te = header("Transfer-Encoding")) {
    // reject if both TE and CL
    if (header("Content-Length"))
      return (setError(400), false);
    std::string val(te->begin(), te->end());

    val.erase(std::remove_if(val.begin(), val.end(),
                             [](unsigned char ch) { return std::isspace(ch); }),
              val.end());

    std::string::size_type start = 0;
    bool sawChunked = false;

    while (start <= val.size()) {
      std::string::size_type comma = val.find(',', start);
      std::string token =
          val.substr(start, comma == std::string::npos ? std::string::npos
                                                       : comma - start);

      if (token.empty())
        return (setError(400), false);

      for (char &c : token)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      if (token == "chunked") {
        sawChunked = true;
      } else {
        return (setError(501), false);
      }

      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
    if (sawChunked) {
      _bodyMode = BodyTransferMode::Chunked;
      _state = RequestParseState::ChunkSize;
      return true;
    }

    return (setError(501), false);
  }
  if (auto cl = header("Content-Length")) {
    std::string clStr(cl->begin(), cl->end());

    if (clStr.empty()) {
      return (setError(400), false);
    }
    for (char c : clStr) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        return (setError(400), false);
      }
    }

    char *endptr = nullptr;
    errno = 0;
    unsigned long length = std::strtoul(clStr.c_str(), &endptr, 10);

    if (errno == ERANGE || *endptr != '\0')
      return (setError(400), false);

    if (_maxBodySize > 0 && length > _maxBodySize)
      return (setError(413), false);

    _bodyBytesExpected = static_cast<std::size_t>(length);
    _bodyMode = BodyTransferMode::ContentLength;

    _state = (_bodyBytesExpected == 0) ? RequestParseState::Complete
                                       : RequestParseState::Body;

    return true;
  }

  if (_method == HttpMethod::Post)
    return (setError(411), false);

  _bodyMode = BodyTransferMode::None;
  _state = RequestParseState::Complete;
  return true;
}

// optional a value that may or may not exist
std::optional<std::string_view>
HttpRequest::header(std::string_view name) const noexcept {
  auto it = _headers.find(std::string(name));
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

  if (_bodyBytesReceived < _bodyBytesExpected)
    return false;
  _state = RequestParseState::Complete;
  return true;
}

bool HttpRequest::parseChunkedBody() {
  while (true) {
    if (_state == RequestParseState::ChunkSize) {
      if (!parseChunkSize())
        return false;
    } else if (_state == RequestParseState::ChunkData) {
      if (!parseChunkData())
        return false;
      _state = RequestParseState::ChunkSize;
    } else if (_state == RequestParseState::ChunkTrailer) {
      if (!parseChunkTrailer())
        return false;
      _state = RequestParseState::Complete;
      return true;
    } else {
      setError(500);
      return false;
    }
  }
}

bool HttpRequest::parseChunkSize() {
  std::size_t pos = _rawBuffer.find("\r\n");
  if (pos == std::string::npos)
    return false; // more data needed

  std::string sizeStr = _rawBuffer.substr(0, pos);
  _rawBuffer.erase(0, pos + 2);

  if (sizeStr.empty())
    return (setError(400), false);

  char *endptr = nullptr;
  errno = 0;
  unsigned long size = std::strtoul(sizeStr.c_str(), &endptr, 16);
  if (*endptr != '\0' || errno == ERANGE)
    return (setError(400), false);

  _currentChunkSize = size;

  if (_maxBodySize > 0 && size + _body.size() > _maxBodySize) // not sure
    return (setError(413), false);
  _state = (_currentChunkSize == 0) ? RequestParseState::ChunkTrailer
                                    : RequestParseState::ChunkData;
  return true;
}

bool HttpRequest::parseChunkData() {
  if (_rawBuffer.size() < _currentChunkSize + 2)
    return false;
  _body.append(_rawBuffer.substr(0, _currentChunkSize));
  _rawBuffer.erase(0, _currentChunkSize);
  if (_rawBuffer.substr(0, 2) != "\r\n")
    return (setError(400), false);
  _rawBuffer.erase(0, 2);
  return true;
}

bool HttpRequest::parseChunkTrailer() {
  if (_rawBuffer.size() < 2)
    return false;
  if (_rawBuffer.substr(0, 2) == "\r\n") {
    _rawBuffer.erase(0, 2);
    return true;
  }
  std::size_t end = _rawBuffer.find("\r\n\r\n");
  if (end == std::string::npos) {
    if (_rawBuffer.size() > 4096)
      return (setError(400), false);
    return false;
  }
  _rawBuffer.erase(0, end + 4);
  return true;
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

bool HttpRequest::hasHeader(std::string_view name) const noexcept {
  return _headers.find(std::string(name)) != _headers.end();
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

void HttpRequest::setMaxHeaderSize(std::size_t bytes) noexcept {
  _maxHeaderSize = bytes;
}

void HttpRequest::reset() noexcept {
  _state = RequestParseState::StartLine;
  _errorStatus = 0;

  _rawBuffer.clear();
  _headerEndPos = std::string::npos; // not using it
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
