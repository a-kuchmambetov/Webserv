#include "HttpTypes.hpp"

#include <algorithm>
#include <cctype>

namespace webserv {

namespace {

char toLowerAscii(const char ch) {
  return static_cast<char>(
      std::tolower(static_cast<unsigned char>(ch)));
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size())
    return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (toLowerAscii(lhs[i]) != toLowerAscii(rhs[i]))
      return false;
  }
  return true;
}

} // namespace

bool CaseInsensitiveLess::operator()(std::string_view lhs,
                                     std::string_view rhs) const noexcept {
  return std::lexicographical_compare(
      lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
      [](const char left, const char right) {
        return toLowerAscii(left) < toLowerAscii(right);
      });
}

std::string_view toString(HttpMethod method) noexcept {
  switch (method) {
  case HttpMethod::Get:
    return "GET";
  case HttpMethod::Post:
    return "POST";
  case HttpMethod::Delete:
    return "DELETE";
  case HttpMethod::Unknown:
    break;
  }
  return "UNKNOWN";
}

HttpMethod parseHttpMethod(std::string_view method) noexcept {
  if (equalsIgnoreCase(method, "GET"))
    return HttpMethod::Get;
  if (equalsIgnoreCase(method, "POST"))
    return HttpMethod::Post;
  if (equalsIgnoreCase(method, "DELETE"))
    return HttpMethod::Delete;
  return HttpMethod::Unknown;
}

std::string reasonPhraseFor(int statusCode) {
  switch (statusCode) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 204:
    return "No Content";
  case 301:
    return "Moved Permanently";
  case 302:
    return "Found";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 408:
    return "Request Timeout";
  case 411:
    return "Length Required";
  case 413:
    return "Payload Too Large";
  case 431:
    return "Request Header Fields Too Large";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 502:
    return "Bad Gateway";
  case 504:
    return "Gateway Timeout";
  case 505:
    return "HTTP Version Not Supported";
  default:
    return "Unknown Status";
  }
}

std::string normalizeHeaderName(std::string_view name) {
  std::string normalized(name);
  bool upperNext = true;

  for (std::size_t i = 0; i < normalized.size(); ++i) {
    if (normalized[i] == '-') {
      upperNext = true;
      continue;
    }

    normalized[i] = upperNext
                        ? static_cast<char>(std::toupper(
                              static_cast<unsigned char>(normalized[i])))
                        : toLowerAscii(normalized[i]);
    upperNext = false;
  }

  return normalized;
}

} // namespace webserv
