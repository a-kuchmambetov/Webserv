#pragma once

#include "LocationConfig.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace webserv {

class ServerConfig {
public:
  ServerConfig() = default;

  [[nodiscard]] const std::vector<ListenEndpoint> &
  listenEndpoints() const noexcept;
  [[nodiscard]] const std::vector<std::string> &serverNames() const noexcept;
  [[nodiscard]] std::size_t clientMaxBodySize() const noexcept;
  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] const std::vector<std::string> &indexFiles() const noexcept;
  [[nodiscard]] const ErrorPageMap &errorPages() const noexcept;
  [[nodiscard]] const std::vector<LocationConfig> &locations() const noexcept;

  void setListenEndpoints(std::vector<ListenEndpoint> endpoints);
  void addListenEndpoint(ListenEndpoint endpoint);
  void setServerNames(std::vector<std::string> names);
  void setClientMaxBodySize(std::size_t bytes) noexcept;
  void setRoot(std::filesystem::path value);
  void setIndexFiles(std::vector<std::string> values);
  void setErrorPages(ErrorPageMap pages);
  void setErrorPage(int statusCode, std::filesystem::path filePath);
  void setLocations(std::vector<LocationConfig> values);
  void addLocation(LocationConfig location);

  [[nodiscard]] const LocationConfig *
  findBestLocation(std::string_view requestPath) const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path>
  errorPageFor(int statusCode) const;
  [[nodiscard]] std::size_t effectiveClientMaxBodySize(
      const LocationConfig *location = nullptr) const noexcept;

  [[nodiscard]] static std::vector<ServerConfig>
  parseFile(const std::filesystem::path &filePath);

private:
  std::vector<ListenEndpoint> _listenEndpoints;
  std::vector<std::string> _serverNames;
  std::size_t _clientMaxBodySize{0};

  std::filesystem::path _root;
  std::vector<std::string> _indexFiles;
  ErrorPageMap _errorPages;
  std::vector<LocationConfig> _locations;
};

} // namespace webserv
