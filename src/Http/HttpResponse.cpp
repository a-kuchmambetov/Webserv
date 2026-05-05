#include "HttpResponse.hpp"
#include "CgiResult.hpp"
#include "HttpTypes.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace webserv {

namespace {

std::string httpDate() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);

  char buffer[64];
  std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tm);
  return buffer;
}

std::string readFileBody(const std::filesystem::path &path, bool &ok) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    ok = false;
    return {};
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  ok = static_cast<bool>(file) || file.eof();
  return stream.str();
}

} // namespace

HttpResponse::FileBody::FileBody(std::filesystem::path filePath,
                                 std::uintmax_t fileSize,
                                 std::string mimeType)
    : path(std::move(filePath)), size(fileSize),
      contentType(std::move(mimeType)) {}

void HttpResponse::reset() {
  _statusCode = 200;
  _reasonPhrase = "OK";
  _httpVersion = "HTTP/1.1";
  _headers.clear();
  _payload = std::monostate{};
  _connectionPreference = ConnectionPreference::KeepAlive;
}

void HttpResponse::setStatus(int statusCode) {
  setStatus(statusCode, reasonPhraseFor(statusCode));
}

void HttpResponse::setStatus(int statusCode, std::string reasonPhrase) {
  _statusCode = statusCode;
  _reasonPhrase = std::move(reasonPhrase);
}

int HttpResponse::statusCode() const noexcept { return _statusCode; }

std::string_view HttpResponse::reasonPhrase() const noexcept {
  return _reasonPhrase;
}

void HttpResponse::setHeader(std::string name, std::string value) {
  _headers[normalizeHeaderName(name)] = std::move(value);
}

void HttpResponse::removeHeader(std::string_view name) {
  _headers.erase(std::string(name));
}

bool HttpResponse::hasHeader(std::string_view name) const noexcept {
  return _headers.find(name) != _headers.end();
}

std::optional<std::string_view>
HttpResponse::header(std::string_view name) const noexcept {
  auto it = _headers.find(name);
  if (it == _headers.end())
    return std::nullopt;
  return std::string_view(it->second);
}

const HeaderMap &HttpResponse::headers() const noexcept { return _headers; }

void HttpResponse::setBody(std::string bytes) {
  _payload = std::move(bytes);
}

void HttpResponse::setBody(std::string bytes, std::string contentType) {
  setBody(std::move(bytes));
  setHeader("Content-Type", std::move(contentType));
}

void HttpResponse::setFileBody(FileBody fileBody) {
  _payload = std::move(fileBody);
}

void HttpResponse::clearBody() noexcept { _payload = std::monostate{}; }

const HttpResponse::Payload &HttpResponse::payload() const noexcept {
  return _payload;
}

bool HttpResponse::hasMemoryBody() const noexcept {
  return std::holds_alternative<std::string>(_payload);
}

std::string_view HttpResponse::body() const noexcept {
  if (!hasMemoryBody())
    return {};
  return std::get<std::string>(_payload);
}

void HttpResponse::setConnectionPreference(
    ConnectionPreference preference) noexcept {
  _connectionPreference = preference;
}

ConnectionPreference HttpResponse::connectionPreference() const noexcept {
  return _connectionPreference;
}

bool HttpResponse::shouldCloseConnection() const noexcept {
  return _connectionPreference == ConnectionPreference::Close;
}

void HttpResponse::ensureRequiredHeaders() {
  if (!hasHeader("Date"))
    setHeader("Date", httpDate());
  if (!hasHeader("Server"))
    setHeader("Server", "webserv");
  if (!hasHeader("Connection")) {
    setHeader("Connection",
              shouldCloseConnection() ? "close" : "keep-alive");
  }

  if (!hasHeader("Content-Type")) {
    if (std::holds_alternative<std::string>(_payload) &&
        !std::get<std::string>(_payload).empty()) {
      setHeader("Content-Type", "text/plain; charset=utf-8");
    } else if (std::holds_alternative<FileBody>(_payload)) {
      const FileBody &fileBody = std::get<FileBody>(_payload);
      setHeader("Content-Type",
                fileBody.contentType.empty() ? "application/octet-stream"
                                             : fileBody.contentType);
    }
  }

  if (!hasHeader("Content-Length")) {
    std::uintmax_t length = 0;
    if (std::holds_alternative<std::string>(_payload))
      length = std::get<std::string>(_payload).size();
    else if (std::holds_alternative<FileBody>(_payload))
      length = std::get<FileBody>(_payload).size;
    setHeader("Content-Length", std::to_string(length));
  }
}

std::string HttpResponse::serializeHead() const {
  HttpResponse response = *this;
  response.ensureRequiredHeaders();

  std::ostringstream stream;
  stream << response._httpVersion << ' ' << response._statusCode << ' '
         << response._reasonPhrase << "\r\n";
  for (const auto &[name, value] : response._headers)
    stream << name << ": " << value << "\r\n";
  stream << "\r\n";
  return stream.str();
}

std::string HttpResponse::serialize() const {
  HttpResponse response = *this;

  if (std::holds_alternative<FileBody>(response._payload)) {
    const FileBody fileBody = std::get<FileBody>(response._payload);
    bool ok = false;
    std::string bytes = readFileBody(fileBody.path, ok);
    if (!ok) {
      response = HttpResponse::error(500);
    } else {
      response._payload = std::move(bytes);
      response.setHeader("Content-Length",
                         std::to_string(std::get<std::string>(
                                             response._payload)
                                            .size()));
      if (!fileBody.contentType.empty())
        response.setHeader("Content-Type", fileBody.contentType);
    }
  }

  std::string head = response.serializeHead();
  if (std::holds_alternative<std::string>(response._payload))
    head += std::get<std::string>(response._payload);
  return head;
}

HttpResponse HttpResponse::error(int statusCode, std::string body) {
  HttpResponse response;
  response.setStatus(statusCode);
  response.setConnectionPreference(ConnectionPreference::Close);
  if (body.empty()) {
    body = std::to_string(statusCode) + " " + reasonPhraseFor(statusCode) +
           "\n";
  }
  response.setBody(std::move(body), "text/plain; charset=utf-8");
  return response;
}

HttpResponse HttpResponse::redirect(int statusCode, std::string target) {
  HttpResponse response;
  response.setStatus(statusCode);
  response.setHeader("Location", target);
  response.setBody("Redirecting to " + std::move(target) + "\n",
                   "text/plain; charset=utf-8");
  return response;
}

HttpResponse HttpResponse::fromCgi(const CgiResult &cgiResult) {
  return cgiResult.toHttpResponse();
}

std::string HttpResponse::mimeTypeFor(const std::filesystem::path &filePath) {
  std::string ext = filePath.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (ext == ".html" || ext == ".htm")
    return "text/html; charset=utf-8";
  if (ext == ".css")
    return "text/css; charset=utf-8";
  if (ext == ".js" || ext == ".mjs")
    return "application/javascript";
  if (ext == ".json")
    return "application/json";
  if (ext == ".txt")
    return "text/plain; charset=utf-8";
  if (ext == ".png")
    return "image/png";
  if (ext == ".jpg" || ext == ".jpeg")
    return "image/jpeg";
  if (ext == ".gif")
    return "image/gif";
  if (ext == ".svg")
    return "image/svg+xml";
  if (ext == ".ico")
    return "image/x-icon";
  if (ext == ".pdf")
    return "application/pdf";
  return "application/octet-stream";
}

} // namespace webserv
