#include "ServerConfig.hpp"
#include "Parser.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace webserv {

const std::vector<ListenEndpoint> &
ServerConfig::listenEndpoints() const noexcept {
  return _listenEndpoints;
}

const std::vector<std::string> &ServerConfig::serverNames() const noexcept {
  return _serverNames;
}

std::size_t ServerConfig::clientMaxBodySize() const noexcept {
  return _clientMaxBodySize;
}

const std::filesystem::path &ServerConfig::root() const noexcept {
  return _root;
}

const std::vector<std::string> &ServerConfig::indexFiles() const noexcept {
  return _indexFiles;
}

const ErrorPageMap &ServerConfig::errorPages() const noexcept {
  return _errorPages;
}

const std::vector<LocationConfig> &ServerConfig::locations() const noexcept {
  return _locations;
}

void ServerConfig::setListenEndpoints(std::vector<ListenEndpoint> endpoints) {
  _listenEndpoints = std::move(endpoints);
}

void ServerConfig::addListenEndpoint(ListenEndpoint endpoint) {
  _listenEndpoints.push_back(std::move(endpoint));
}

void ServerConfig::setServerNames(std::vector<std::string> names) {
  _serverNames = std::move(names);
}

void ServerConfig::setClientMaxBodySize(std::size_t bytes) noexcept {
  _clientMaxBodySize = bytes;
}

void ServerConfig::setRoot(std::filesystem::path value) {
  _root = std::move(value);
}

void ServerConfig::setIndexFiles(std::vector<std::string> values) {
  _indexFiles = std::move(values);
}

void ServerConfig::setErrorPages(ErrorPageMap pages) {
  _errorPages = std::move(pages);
}

void ServerConfig::setErrorPage(int statusCode,
                                std::filesystem::path filePath) {
  _errorPages[statusCode] = std::move(filePath);
}

void ServerConfig::setLocations(std::vector<LocationConfig> values) {
  _locations = std::move(values);
}

void ServerConfig::addLocation(LocationConfig location) {
  _locations.push_back(std::move(location));
}

const LocationConfig *
ServerConfig::findBestLocation(std::string_view requestPath) const noexcept {
  const LocationConfig *best = nullptr;
  std::size_t bestLen = 0;
  for (const LocationConfig &loc : _locations) {
    const std::string &prefix = loc.pathPrefix();
    if (requestPath.starts_with(prefix) && prefix.size() > bestLen) {
      bestLen = prefix.size();
      best = &loc;
    }
  }
  return best;
}

std::optional<std::filesystem::path>
ServerConfig::errorPageFor(int statusCode) const {
  auto it = _errorPages.find(statusCode);
  if (it == _errorPages.end())
    return std::nullopt;
  return it->second;
}

std::size_t
ServerConfig::effectiveClientMaxBodySize(const LocationConfig *location) const noexcept {
  if (location) {
    std::optional<std::size_t> locLimit = location->clientMaxBodySize();
    if (locLimit.has_value())
      return *locLimit;
  }
  return _clientMaxBodySize;
}

} // namespace webserv
