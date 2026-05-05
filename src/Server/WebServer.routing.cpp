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

std::optional<std::string> extractBoundary(std::string_view contentType) {
  static constexpr std::string_view key = "boundary=";
  const std::size_t pos = contentType.find(key);
  if (pos == std::string_view::npos)
    return std::nullopt;

  std::string value(contentType.substr(pos + key.size()));
  std::size_t end = value.size();
  while (end > 0 && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                     value[end - 1] == ';'))
    --end;
  value.resize(end);

  std::size_t start = 0;
  while (start < value.size() &&
         (value[start] == ' ' || value[start] == '\t'))
    ++start;
  value = value.substr(start);

  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  return value;
}

std::optional<std::string_view> headerValueInBlock(std::string_view block,
                                                    std::string_view name) {
  std::size_t pos = 0;
  while (pos < block.size()) {
    std::size_t lineEnd = block.find("\r\n", pos);
    if (lineEnd == std::string_view::npos)
      lineEnd = block.size();
    std::string_view line = block.substr(pos, lineEnd - pos);
    std::size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
      std::string_view key = line.substr(0, colon);
      bool match = key.size() == name.size();
      if (match) {
        for (std::size_t i = 0; i < key.size(); ++i) {
          if (std::tolower(static_cast<unsigned char>(key[i])) !=
              std::tolower(static_cast<unsigned char>(name[i]))) {
            match = false;
            break;
          }
        }
      }
      if (match) {
        std::string_view value = line.substr(colon + 1);
        std::size_t vstart = value.find_first_not_of(" \t");
        if (vstart != std::string_view::npos)
          value = value.substr(vstart);
        return value;
      }
    }
    if (lineEnd == block.size())
      break;
    pos = lineEnd + 2;
  }
  return std::nullopt;
}

std::string partFilename(std::string_view partHeaders) {
  auto cd = headerValueInBlock(partHeaders, "Content-Disposition");
  if (!cd)
    return {};
  return filenameFromContentDisposition(*cd);
}

std::size_t findBoundary(std::string_view body, std::string_view boundary,
                          std::size_t start) {
  std::string marker = "--" + std::string(boundary);
  std::size_t pos = start;
  while (pos < body.size()) {
    pos = body.find(marker, pos);
    if (pos == std::string_view::npos)
      return std::string_view::npos;
    if (pos == 0 ||
        (pos >= 2 && body[pos - 2] == '\r' && body[pos - 1] == '\n'))
      return pos;
    ++pos;
  }
  return std::string_view::npos;
}

std::vector<std::pair<std::string_view, std::string_view>>
parseMultipartParts(std::string_view body, std::string_view boundary) {
  std::vector<std::pair<std::string_view, std::string_view>> parts;
  std::string marker = "--" + std::string(boundary);
  std::size_t pos = findBoundary(body, boundary, 0);
  if (pos == std::string_view::npos)
    return parts;
  while (true) {
    std::size_t afterMarker = pos + marker.size();
    if (afterMarker + 2 <= body.size() && body[afterMarker] == '-' &&
        body[afterMarker + 1] == '-')
      break;
    if (afterMarker + 2 > body.size() || body[afterMarker] != '\r' ||
        body[afterMarker + 1] != '\n')
      break;
    std::size_t partStart = afterMarker + 2;
    std::size_t headersEnd = body.find("\r\n\r\n", partStart);
    if (headersEnd == std::string_view::npos)
      break;
    std::string_view partHeaders = body.substr(partStart, headersEnd - partStart);
    std::size_t bodyStart = headersEnd + 4;
    std::size_t nextPos = findBoundary(body, boundary, bodyStart);
    if (nextPos == std::string_view::npos)
      break;
    std::size_t partBodyEnd = nextPos;
    if (partBodyEnd >= 2 && body[partBodyEnd - 2] == '\r' &&
        body[partBodyEnd - 1] == '\n')
      partBodyEnd -= 2;
    parts.emplace_back(partHeaders,
                        body.substr(bodyStart, partBodyEnd - bodyStart));
    pos = nextPos;
  }
  return parts;
}

std::optional<std::filesystem::path>
resolveUniqueTarget(const std::filesystem::path &uploadRoot,
                    const std::string &filename,
                    std::string &outFilename) {
  std::error_code ec;
  std::filesystem::path target;
  for (int i = 0; i < 1000; ++i) {
    std::string candidate = filename;
    if (i > 0) {
      const std::filesystem::path p(filename);
      candidate =
          p.stem().string() + "-" + std::to_string(i) + p.extension().string();
    }
    target = canonicalOrNormalized(uploadRoot / candidate);
    if (!pathStartsWith(target, uploadRoot))
      return std::nullopt;
    if (!std::filesystem::exists(target, ec)) {
      outFilename = candidate;
      return target;
    }
  }
  return std::nullopt;
}

void cleanupUploads(const std::vector<std::filesystem::path> &paths) {
  std::error_code ec;
  for (const auto &p : paths)
    std::filesystem::remove(p, ec);
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
    return queueMethodNotAllowed(fd, &server, *location);
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

HttpResponse WebServer::makeErrorResponse(const ServerConfig *server,
                                           int statusCode) {
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

  return response;
}

void WebServer::queueError(int fd, const ServerConfig *server, int statusCode) {
  queueResponse(fd, makeErrorResponse(server, statusCode));
}

void WebServer::queueMethodNotAllowed(int fd, const ServerConfig *server,
                                      const LocationConfig &location) {
  HttpResponse response = makeErrorResponse(server, 405);
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
    return queueMethodNotAllowed(fd, &server, location);

  std::error_code ec;
  const std::filesystem::path uploadRoot =
      canonicalOrNormalized(*location.uploadDirectory());
  std::filesystem::create_directories(uploadRoot, ec);
  if (ec)
    return queueError(fd, &server, 500);

  std::vector<std::string> savedNames;
  std::vector<std::filesystem::path> writtenPaths;

  bool isMultipart = false;
  std::string boundary;
  if (auto contentType = request.header("Content-Type")) {
    std::string lowerCt;
    lowerCt.reserve(contentType->size());
    for (unsigned char c : *contentType)
      lowerCt.push_back(static_cast<char>(std::tolower(c)));
    if (lowerCt.find("multipart/form-data") != std::string::npos) {
      if (auto maybeBoundary = extractBoundary(*contentType)) {
        isMultipart = true;
        boundary = std::move(*maybeBoundary);
      }
    }
  }

  if (isMultipart) {
    auto parts = parseMultipartParts(request.body(), boundary);
    for (const auto &[partHeaders, partBody] : parts) {
      std::string filename = partFilename(partHeaders);
      if (filename.empty())
        continue;
      filename = sanitizeFilename(std::move(filename));

      std::string chosenName;
      auto target = resolveUniqueTarget(uploadRoot, filename, chosenName);
      if (!target) {
        cleanupUploads(writtenPaths);
        return queueError(fd, &server, 500);
      }
      if (!pathStartsWith(*target, uploadRoot)) {
        cleanupUploads(writtenPaths);
        return queueError(fd, &server, 403);
      }

      std::ofstream file(*target, std::ios::binary);
      if (!file) {
        cleanupUploads(writtenPaths);
        return queueError(fd, &server, 500);
      }
      file.write(partBody.data(), static_cast<std::streamsize>(partBody.size()));
      if (!file) {
        cleanupUploads(writtenPaths);
        std::filesystem::remove(*target, ec);
        return queueError(fd, &server, 500);
      }
      savedNames.push_back(chosenName);
      writtenPaths.push_back(*target);
    }
    if (savedNames.empty())
      return queueError(fd, &server, 400);
  } else {
    std::string filename;
    if (auto contentDisposition = request.header("Content-Disposition"))
      filename = filenameFromContentDisposition(*contentDisposition);
    filename = sanitizeFilename(std::move(filename));

    std::string chosenName;
    auto target = resolveUniqueTarget(uploadRoot, filename, chosenName);
    if (!target)
      return queueError(fd, &server, 500);
    if (!pathStartsWith(*target, uploadRoot))
      return queueError(fd, &server, 403);

    std::ofstream file(*target, std::ios::binary);
    if (!file)
      return queueError(fd, &server, 500);
    std::string_view body = request.body();
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!file)
      return queueError(fd, &server, 500);

    savedNames.push_back(chosenName);
  }

  HttpResponse response;
  response.setStatus(201);
  if (savedNames.size() == 1) {
    response.setHeader("Location",
                        locationForUpload(request.path(), savedNames[0]));
  } else {
    std::string loc(request.path());
    if (!loc.ends_with('/'))
      loc += '/';
    response.setHeader("Location", loc);
  }

  std::string bodyText = "Created " + std::to_string(savedNames.size()) +
                          " file" + (savedNames.size() == 1 ? "\n" : "s\n");
  for (const std::string &name : savedNames)
    bodyText += name + "\n";
  response.setBody(bodyText, "text/plain; charset=utf-8");
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
