#pragma once

#include "HttpTypes.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace webserv {

class LocationConfig {
public:
  LocationConfig() = default;

  [[nodiscard]] const std::string &pathPrefix() const noexcept;
  [[nodiscard]] const MethodSet &allowedMethods() const noexcept;
  [[nodiscard]] const std::optional<RedirectRule> &redirect() const noexcept;
  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] bool autoindexEnabled() const noexcept;
  [[nodiscard]] const std::vector<std::string> &indexFiles() const noexcept;
  [[nodiscard]] const std::optional<std::filesystem::path> &
  uploadDirectory() const noexcept;
  [[nodiscard]] const CgiHandlerMap &cgiHandlers() const noexcept;
  [[nodiscard]] std::optional<std::size_t> clientMaxBodySize() const noexcept;

  void setPathPrefix(std::string value);
  void setAllowedMethods(MethodSet methods);
  void allowMethod(HttpMethod method);
  void clearAllowedMethods() noexcept;
  void setRedirect(std::optional<RedirectRule> redirect);
  void clearRedirect() noexcept;
  void setRoot(std::filesystem::path value);
  void setAutoindex(bool enabled) noexcept;
  void setIndexFiles(std::vector<std::string> values);
  void setUploadDirectory(std::optional<std::filesystem::path> value);
  void disableUploads() noexcept;
  void setCgiHandlers(CgiHandlerMap handlers);
  void setCgiHandler(std::string extension, std::filesystem::path executable);
  void setClientMaxBodySize(std::optional<std::size_t> bytes) noexcept;

  [[nodiscard]] bool matches(std::string_view requestPath) const noexcept;
  [[nodiscard]] bool isMethodAllowed(HttpMethod method) const noexcept;
  [[nodiscard]] bool hasRedirect() const noexcept;
  [[nodiscard]] bool uploadsEnabled() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path>
  cgiHandlerFor(std::string_view extension) const;

private:
  std::string _pathPrefix{"/"};
  MethodSet _allowedMethods{HttpMethod::Get, HttpMethod::Post,
                            HttpMethod::Delete};
  std::optional<RedirectRule> _redirect;

  std::filesystem::path _root;
  bool _autoindex{false};
  std::vector<std::string> _indexFiles;

  std::optional<std::filesystem::path> _uploadDirectory;
  CgiHandlerMap _cgiHandlers;
  std::optional<std::size_t> _clientMaxBodySize;
};

} // namespace webserv
