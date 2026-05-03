#include "WebServer.hpp"
#include "CgiRequest.hpp"
#include "Connection.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpTypes.hpp"
#include "LocationConfig.hpp"
#include "ServerConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/epoll.h>

namespace webserv {

namespace {

std::string lowerAscii(std::string value) {
  for (char &c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

std::string normalizeHost(std::string_view host) {
  std::string value(host);
  const std::size_t colon = value.find(':');
  if (colon != std::string::npos &&
      value.find(':', colon + 1) == std::string::npos)
    value.erase(colon);
  return lowerAscii(value);
}

bool endpointMatches(const ListenEndpoint &endpoint, const Listener &listener) {
  return endpoint.port == listener.port &&
         (endpoint.host == listener.host || endpoint.host == "0.0.0.0" ||
          listener.host == "0.0.0.0");
}

bool serverListensOn(const ServerConfig &server, const Listener &listener) {
  for (const ListenEndpoint &endpoint : server.listenEndpoints()) {
    if (endpointMatches(endpoint, listener))
      return true;
  }
  return false;
}

bool serverNameMatches(const ServerConfig &server, std::string_view host) {
  const std::string normalizedHost = normalizeHost(host);
  for (const std::string &name : server.serverNames()) {
    if (lowerAscii(name) == normalizedHost)
      return true;
  }
  return false;
}

std::filesystem::path effectiveRoot(const ServerConfig &server,
                                    const LocationConfig &location) {
  if (!location.root().empty())
    return location.root();
  if (!server.root().empty())
    return server.root();
  return ".";
}

std::vector<std::string> effectiveIndexFiles(const ServerConfig &server,
                                             const LocationConfig &location) {
  if (!location.indexFiles().empty())
    return location.indexFiles();
  return server.indexFiles();
}

std::filesystem::path canonicalOrNormalized(const std::filesystem::path &path) {
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
  if (!ec)
    return resolved;
  return std::filesystem::absolute(path, ec).lexically_normal();
}

bool pathStartsWith(const std::filesystem::path &child,
                    const std::filesystem::path &base) {
  auto childIt = child.begin();
  auto baseIt = base.begin();
  for (; baseIt != base.end(); ++baseIt, ++childIt) {
    if (childIt == child.end() || *childIt != *baseIt)
      return false;
  }
  return true;
}

std::filesystem::path relativeRequestPath(std::string_view requestPath,
                                          std::string_view locationPrefix) {
  std::string relative(requestPath);
  if (locationPrefix != "/" && relative.starts_with(locationPrefix))
    relative.erase(0, locationPrefix.size());
  if (!relative.empty() && relative.front() == '/')
    relative.erase(0, 1);
  if (relative.empty())
    return ".";
  return std::filesystem::path(relative).lexically_normal();
}

bool resolveRequestPath(const ServerConfig &server,
                        const LocationConfig &location,
                        std::string_view requestPath,
                        std::filesystem::path &resolvedPath) {
  const std::filesystem::path root =
      canonicalOrNormalized(effectiveRoot(server, location));
  const std::filesystem::path relative =
      relativeRequestPath(requestPath, location.pathPrefix());
  const std::filesystem::path candidate =
      canonicalOrNormalized(root / relative);

  if (!pathStartsWith(candidate, root))
    return false;
  resolvedPath = candidate;
  return true;
}

std::string escapeHtml(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '"':
      escaped += "&quot;";
      break;
    default:
      escaped += c;
      break;
    }
  }
  return escaped;
}

std::string directoryListing(const std::filesystem::path &directory,
                             std::string_view requestPath) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    std::string name = entry.path().filename().string();
    if (entry.is_directory(ec))
      name += "/";
    names.push_back(std::move(name));
  }
  std::sort(names.begin(), names.end());

  std::string base(requestPath);
  if (!base.ends_with('/'))
    base += '/';

  std::ostringstream html;
  html << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of "
       << escapeHtml(requestPath) << "</title></head><body><h1>Index of "
       << escapeHtml(requestPath) << "</h1><ul>";
  for (const std::string &name : names) {
    html << "<li><a href=\"" << escapeHtml(base + name) << "\">"
         << escapeHtml(name) << "</a></li>";
  }
  html << "</ul></body></html>\n";
  return html.str();
}

std::string allowHeaderValue(const LocationConfig &location) {
  std::string value;
  for (HttpMethod method : location.allowedMethods()) {
    if (!value.empty())
      value += ", ";
    value += toString(method);
  }
  return value;
}

std::string generatedUploadName() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return "upload-" + std::to_string(millis) + ".bin";
}

std::string filenameFromContentDisposition(std::string_view header) {
  static constexpr std::string_view key = "filename=";
  const std::size_t pos = header.find(key);
  if (pos == std::string_view::npos)
    return {};

  std::string value(header.substr(pos + key.size()));
  const std::size_t semicolon = value.find(';');
  if (semicolon != std::string::npos)
    value.erase(semicolon);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  return std::filesystem::path(value).filename().string();
}

std::string sanitizeFilename(std::string value) {
  if (value.empty())
    value = generatedUploadName();
  for (char &c : value) {
    const bool safe = std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                      c == '-' || c == '_';
    if (!safe)
      c = '_';
  }
  if (value == "." || value == "..")
    return generatedUploadName();
  return value;
}

std::string locationForUpload(std::string_view requestPath,
                              const std::string &filename) {
  std::string location(requestPath);
  if (!location.ends_with('/'))
    location += '/';
  location += filename;
  return location;
}

} // namespace

const ServerConfig &
WebServer::selectVirtualHost(const ClientSession &session,
                             const HttpRequest &request) const {
  const Listener &listener = _listeningFds.at(session.listenerFd);
  const ServerConfig *fallback = session.defaultServer;

  for (const ServerConfig &server : _servers) {
    if (!serverListensOn(server, listener))
      continue;
    if (!fallback)
      fallback = &server;
    if (serverNameMatches(server, request.host()))
      return server;
  }

  return *fallback;
}

void WebServer::processRequest(int fd) {
  ClientSession &session = _connectionFds.at(fd);
  Connection &conn = session.connection;
  const HttpRequest &request = conn.getRequest();
  const ServerConfig &server = selectVirtualHost(session, request);
  const LocationConfig *location = server.findBestLocation(request.path());

  if (!location)
    return queueError(fd, &server, 404);
  if (!location->isMethodAllowed(request.method()))
    return queueMethodNotAllowed(fd, *location);
  if (request.body().size() > server.effectiveClientMaxBodySize(location))
    return queueError(fd, &server, 413);
  if (location->hasRedirect())
    return queueRedirect(fd, *location);
  if (isCgiRequest(request, *location))
    return startCgi(fd, server, *location);

  switch (request.method()) {
  case HttpMethod::Get:
    return handleStaticGet(fd, server, *location);
  case HttpMethod::Post:
    return handleUpload(fd, server, *location);
  case HttpMethod::Delete:
    return handleDelete(fd, server, *location);
  case HttpMethod::Unknown:
    break;
  }

  queueError(fd, &server, 501);
}

void WebServer::queueResponse(int fd, HttpResponse response) {
  Connection &conn = _connectionFds.at(fd).connection;
  conn.queueResponse(std::move(response));
  _poller.modFd(fd, EPOLLOUT | EPOLLRDHUP);
}

void WebServer::queueError(int fd, const ServerConfig *server, int statusCode) {
  HttpResponse response = HttpResponse::error(statusCode);

  if (server) {
    std::optional<std::filesystem::path> path =
        server->errorPageFor(statusCode);
    std::error_code ec;
    if (path && std::filesystem::is_regular_file(*path, ec)) {
      response.setFileBody(
          HttpResponse::FileBody{*path, std::filesystem::file_size(*path, ec),
                                 HttpResponse::mimeTypeFor(*path)});
    }
  }

  queueResponse(fd, std::move(response));
}

void WebServer::queueMethodNotAllowed(int fd, const LocationConfig &location) {
  HttpResponse response = HttpResponse::error(405);
  response.setHeader("Allow", allowHeaderValue(location));
  queueResponse(fd, std::move(response));
}

void WebServer::queueRedirect(int fd, const LocationConfig &location) {
  const RedirectRule &rule = *location.redirect();
  queueResponse(fd, HttpResponse::redirect(rule.statusCode, rule.target));
}

bool WebServer::isCgiRequest(const HttpRequest &request,
                             const LocationConfig &location) const {
  const std::string extension =
      std::filesystem::path(std::string(request.path())).extension().string();
  return location.cgiHandlerFor(extension).has_value();
}

void WebServer::handleStaticGet(int fd, const ServerConfig &server,
                                const LocationConfig &location) {
  const HttpRequest &request = _connectionFds.at(fd).connection.getRequest();
  std::filesystem::path path;
  std::error_code ec;

  if (!resolveRequestPath(server, location, request.path(), path))
    return queueError(fd, &server, 403);

  if (std::filesystem::is_directory(path, ec)) {
    for (const std::string &indexFile : effectiveIndexFiles(server, location)) {
      const std::filesystem::path indexPath = path / indexFile;
      if (std::filesystem::is_regular_file(indexPath, ec)) {
        HttpResponse response;
        response.setFileBody(HttpResponse::FileBody{
            indexPath, std::filesystem::file_size(indexPath, ec),
            HttpResponse::mimeTypeFor(indexPath)});
        return queueResponse(fd, std::move(response));
      }
    }

    if (location.autoindexEnabled()) {
      HttpResponse response;
      response.setBody(directoryListing(path, request.path()),
                       "text/html; charset=utf-8");
      return queueResponse(fd, std::move(response));
    }
    return queueError(fd, &server, 404);
  }

  if (!std::filesystem::is_regular_file(path, ec))
    return queueError(fd, &server, 404);

  HttpResponse response;
  response.setFileBody(
      HttpResponse::FileBody{path, std::filesystem::file_size(path, ec),
                             HttpResponse::mimeTypeFor(path)});
  queueResponse(fd, std::move(response));
}

void WebServer::handleUpload(int fd, const ServerConfig &server,
                             const LocationConfig &location) {
  const HttpRequest &request = _connectionFds.at(fd).connection.getRequest();
  if (!location.uploadsEnabled())
    return queueMethodNotAllowed(fd, location);

  std::error_code ec;
  const std::filesystem::path uploadRoot =
      canonicalOrNormalized(*location.uploadDirectory());
  std::filesystem::create_directories(uploadRoot, ec);
  if (ec)
    return queueError(fd, &server, 500);

  std::string filename;
  if (auto contentDisposition = request.header("Content-Disposition"))
    filename = filenameFromContentDisposition(*contentDisposition);
  filename = sanitizeFilename(std::move(filename));

  std::filesystem::path target;
  bool foundName = false;
  for (int i = 0; i < 1000; ++i) {
    std::string candidate = filename;
    if (i > 0) {
      const std::filesystem::path p(filename);
      candidate =
          p.stem().string() + "-" + std::to_string(i) + p.extension().string();
    }
    target = canonicalOrNormalized(uploadRoot / candidate);
    if (!pathStartsWith(target, uploadRoot))
      return queueError(fd, &server, 403);
    if (!std::filesystem::exists(target, ec)) {
      filename = candidate;
      foundName = true;
      break;
    }
  }
  if (!foundName)
    return queueError(fd, &server, 500);

  std::ofstream file(target, std::ios::binary);
  if (!file)
    return queueError(fd, &server, 500);
  std::string_view body = request.body();
  file.write(body.data(), static_cast<std::streamsize>(body.size()));
  if (!file)
    return queueError(fd, &server, 500);

  HttpResponse response;
  response.setStatus(201);
  response.setHeader("Location", locationForUpload(request.path(), filename));
  response.setBody("Created\n", "text/plain; charset=utf-8");
  queueResponse(fd, std::move(response));
}

void WebServer::handleDelete(int fd, const ServerConfig &server,
                             const LocationConfig &location) {
  const HttpRequest &request = _connectionFds.at(fd).connection.getRequest();
  std::filesystem::path path;
  std::error_code ec;

  if (!resolveRequestPath(server, location, request.path(), path))
    return queueError(fd, &server, 403);
  if (!std::filesystem::exists(path, ec))
    return queueError(fd, &server, 404);
  if (!std::filesystem::is_regular_file(path, ec))
    return queueError(fd, &server, 403);

  std::filesystem::remove(path, ec);
  if (ec) {
    if (ec.value() == EACCES)
      return queueError(fd, &server, 403);
    return queueError(fd, &server, 500);
  }

  HttpResponse response;
  response.setStatus(204);
  queueResponse(fd, std::move(response));
}

void WebServer::startCgi(int fd, const ServerConfig &server,
                         const LocationConfig &location) {
  ClientSession &session = _connectionFds.at(fd);
  Connection &conn = session.connection;
  const HttpRequest &request = conn.getRequest();
  std::filesystem::path scriptPath;
  std::error_code ec;

  if (!resolveRequestPath(server, location, request.path(), scriptPath))
    return queueError(fd, &server, 403);
  if (!std::filesystem::is_regular_file(scriptPath, ec))
    return queueError(fd, &server, 404);

  const std::string extension = scriptPath.extension().string();
  std::optional<std::filesystem::path> handler =
      location.cgiHandlerFor(extension);
  if (!handler)
    return queueError(fd, &server, 500);

  CgiExecutionConfig config;
  config.scriptPath = scriptPath;
  config.executablePath = *handler;
  config.workingDirectory = scriptPath.parent_path();

  std::unique_ptr<CgiRequest> cgi = std::make_unique<CgiRequest>();
  cgi->configure(request, server, location, std::move(config));
  if (!cgi->launch())
    return queueError(fd, &server, 502);

  const int stdinFd = cgi->stdinFd();
  const int stdoutFd = cgi->stdoutFd();
  if (stdoutFd == -1)
    return queueError(fd, &server, 502);

  conn.attachCgi(std::move(cgi));

  try {
    if (stdinFd != -1) {
      _poller.addFd(stdinFd, EPOLLOUT, FdType::CgiStdIn);
      _cgiFdToClient[stdinFd] = fd;
    }
    _poller.addFd(stdoutFd, EPOLLIN | EPOLLHUP | EPOLLERR, FdType::CgiStdOut);
    _cgiFdToClient[stdoutFd] = fd;
    _poller.modFd(fd, EPOLLRDHUP);
  } catch (const std::exception &) {
    if (stdinFd != -1) {
      _poller.removeFd(stdinFd);
      _cgiFdToClient.erase(stdinFd);
    }
    _poller.removeFd(stdoutFd);
    _cgiFdToClient.erase(stdoutFd);
    std::unique_ptr<CgiRequest> dropped = conn.detachCgi();
    queueError(fd, &server, 502);
  }
}

} // namespace webserv
