#include "LocationConfig.hpp"

#include <utility>

namespace webserv {

const std::string &LocationConfig::pathPrefix() const noexcept {
  return _pathPrefix;
}

const MethodSet &LocationConfig::allowedMethods() const noexcept {
  return _allowedMethods;
}

const std::optional<RedirectRule> &LocationConfig::redirect() const noexcept {
  return _redirect;
}

const std::filesystem::path &LocationConfig::root() const noexcept {
  return _root;
}

bool LocationConfig::autoindexEnabled() const noexcept {
  return _autoindex;
}

const std::vector<std::string> &LocationConfig::indexFiles() const noexcept {
  return _indexFiles;
}

const std::optional<std::filesystem::path> &
LocationConfig::uploadDirectory() const noexcept {
  return _uploadDirectory;
}

const CgiHandlerMap &LocationConfig::cgiHandlers() const noexcept {
  return _cgiHandlers;
}

std::optional<std::size_t> LocationConfig::clientMaxBodySize() const noexcept {
  return _clientMaxBodySize;
}

void LocationConfig::setPathPrefix(std::string value) {
  _pathPrefix = std::move(value);
}

void LocationConfig::setAllowedMethods(MethodSet methods) {
  _allowedMethods = std::move(methods);
}

void LocationConfig::allowMethod(HttpMethod method) {
  _allowedMethods.insert(method);
}

void LocationConfig::clearAllowedMethods() noexcept {
  _allowedMethods.clear();
}

void LocationConfig::setRedirect(std::optional<RedirectRule> redirect) {
  _redirect = std::move(redirect);
}

void LocationConfig::clearRedirect() noexcept {
  _redirect.reset();
}

void LocationConfig::setRoot(std::filesystem::path value) {
  _root = std::move(value);
}

void LocationConfig::setAutoindex(bool enabled) noexcept {
  _autoindex = enabled;
}

void LocationConfig::setIndexFiles(std::vector<std::string> values) {
  _indexFiles = std::move(values);
}

void LocationConfig::setUploadDirectory(std::optional<std::filesystem::path> value) {
  _uploadDirectory = std::move(value);
}

void LocationConfig::disableUploads() noexcept {
  _uploadDirectory.reset();
}

void LocationConfig::setCgiHandlers(CgiHandlerMap handlers) {
  _cgiHandlers = std::move(handlers);
}

void LocationConfig::setCgiHandler(std::string extension,
                                   std::filesystem::path executable) {
  _cgiHandlers[std::move(extension)] = std::move(executable);
}

void LocationConfig::setClientMaxBodySize(std::optional<std::size_t> bytes) noexcept {
  _clientMaxBodySize = bytes;
}

bool LocationConfig::matches(std::string_view requestPath) const noexcept {
  return requestPath.starts_with(_pathPrefix);
}

bool LocationConfig::isMethodAllowed(HttpMethod method) const noexcept {
  return _allowedMethods.contains(method);
}

bool LocationConfig::hasRedirect() const noexcept {
  return _redirect.has_value();
}

bool LocationConfig::uploadsEnabled() const noexcept {
  return _uploadDirectory.has_value();
}

std::optional<std::filesystem::path>
LocationConfig::cgiHandlerFor(std::string_view extension) const {
  auto it = _cgiHandlers.find(std::string(extension));
  if (it == _cgiHandlers.end())
    return std::nullopt;
  return it->second;
}

} // namespace webserv
