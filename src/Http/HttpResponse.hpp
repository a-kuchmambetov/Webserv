#pragma once

#include "HttpTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace webserv {

class CgiResult;

class HttpResponse {
public:
  struct FileBody {
    std::filesystem::path path;
    std::uintmax_t size{0};
    std::string contentType;

    FileBody() = default;
    FileBody(std::filesystem::path filePath, std::uintmax_t fileSize,
             std::string mimeType = {});
  };

  using Payload = std::variant<std::monostate, std::string, FileBody>;

  HttpResponse() = default;

  void reset();
  void setStatus(int statusCode);
  void setStatus(int statusCode, std::string reasonPhrase);
  [[nodiscard]] int statusCode() const noexcept;
  [[nodiscard]] std::string_view reasonPhrase() const noexcept;

  void setHeader(std::string name, std::string value);
  void removeHeader(std::string_view name);
  [[nodiscard]] bool hasHeader(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<std::string_view>
  header(std::string_view name) const noexcept;
  [[nodiscard]] const HeaderMap &headers() const noexcept;

  void setBody(std::string bytes);
  void setBody(std::string bytes, std::string contentType);
  void setFileBody(FileBody fileBody);
  void clearBody() noexcept;
  [[nodiscard]] const Payload &payload() const noexcept;
  [[nodiscard]] bool hasMemoryBody() const noexcept;
  [[nodiscard]] std::string_view body() const noexcept;

  void setConnectionPreference(ConnectionPreference preference) noexcept;
  [[nodiscard]] ConnectionPreference connectionPreference() const noexcept;
  [[nodiscard]] bool shouldCloseConnection() const noexcept;

  [[nodiscard]] std::string serializeHead() const;
  [[nodiscard]] std::string serialize() const;

  [[nodiscard]] static HttpResponse error(int statusCode,
                                          std::string body = {});
  [[nodiscard]] static HttpResponse redirect(int statusCode,
                                             std::string target);
  [[nodiscard]] static HttpResponse fromCgi(const CgiResult &cgiResult);
  [[nodiscard]] static std::string
  mimeTypeFor(const std::filesystem::path &filePath);

private:
  void ensureRequiredHeaders();

  int _statusCode{200};
  std::string _reasonPhrase{"OK"};
  std::string _httpVersion{"HTTP/1.1"};
  HeaderMap _headers;
  Payload _payload;
  ConnectionPreference _connectionPreference{ConnectionPreference::KeepAlive};
};

} // namespace webserv
