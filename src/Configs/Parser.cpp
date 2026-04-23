#include "Parser.hpp"
#include "HttpTypes.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace webserv {

Parser::Parser(const std::vector<Token> &tokens) : _tokens(tokens) {}

std::vector<ServerConfig> Parser::parse() {
  std::vector<ServerConfig> servers;
  while (!atEnd()) {
    const Token &tok = peek();
    if (tok.tokenType != Word) {
      advance();
      continue;
    }
    if (tok.value != "server")
      throw std::runtime_error("unexpected top-level token: " + tok.value);
    advance();
    servers.push_back(parseServer());
  }
  return servers;
}

bool Parser::atEnd() const noexcept { return _pos >= _tokens.size(); }

const Token &Parser::peek() const {
  if (atEnd())
    throw std::runtime_error("unexpected end of config");
  return _tokens[_pos];
}

const Token &Parser::advance() {
  if (atEnd())
    throw std::runtime_error("unexpected end of config");
  return _tokens[_pos++];
}

std::string Parser::expectWord() {
  const Token &tok = peek();
  if (tok.tokenType != Word)
    throw std::runtime_error("expected word token, got delimiter");
  return advance().value;
}

void Parser::expectOpenBracket() {
  const Token &tok = peek();
  if (tok.tokenType != OpenBracket)
    throw std::runtime_error("expected '{', got: " + tok.value);
  advance();
}

void Parser::expectCloseBracket() {
  const Token &tok = peek();
  if (tok.tokenType != CloseBracket)
    throw std::runtime_error("expected '}', got: " + tok.value);
  advance();
}

void Parser::expectSemicolon() {
  const Token &tok = peek();
  if (tok.tokenType != Semicol)
    throw std::runtime_error("expected ';', got: " + tok.value);
  advance();
}

ServerConfig Parser::parseServer() {
  expectOpenBracket();
  ServerConfig server;
  while (!atEnd() && peek().tokenType != CloseBracket) {
    std::string directive = expectWord();
    if (directive == "listen")
      parseListenDirective(server);
    else if (directive == "server_name")
      parseServerNameDirective(server);
    else if (directive == "root")
      parseRootDirective(server);
    else if (directive == "index")
      parseIndexDirective(server);
    else if (directive == "client_max_body_size")
      parseClientMaxBodySizeDirective(server);
    else if (directive == "error_page")
      parseErrorPageDirective(server);
    else if (directive == "location")
      server.addLocation(parseLocation());
    else
      throw std::runtime_error("unknown server directive: " + directive);
  }
  expectCloseBracket();
  return server;
}

LocationConfig Parser::parseLocation() {
  std::string prefix = expectWord();
  expectOpenBracket();
  LocationConfig location;
  location.setPathPrefix(prefix);
  while (!atEnd() && peek().tokenType != CloseBracket) {
    std::string directive = expectWord();
    if (directive == "methods")
      parseMethodsDirective(location);
    else if (directive == "root")
      parseRootDirective(location);
    else if (directive == "index")
      parseIndexDirective(location);
    else if (directive == "client_max_body_size")
      parseClientMaxBodySizeDirective(location);
    else if (directive == "autoindex")
      parseAutoindexDirective(location);
    else if (directive == "return")
      parseReturnDirective(location);
    else if (directive == "upload_store")
      parseUploadStoreDirective(location);
    else if (directive == "cgi")
      parseCgiDirective(location);
    else
      throw std::runtime_error("unknown location directive: " + directive);
  }
  expectCloseBracket();
  return location;
}

void Parser::parseListenDirective(ServerConfig &server) {
  server.addListenEndpoint(parseEndpoint(expectWord()));
  expectSemicolon();
}

void Parser::parseServerNameDirective(ServerConfig &server) {
  std::vector<std::string> names;
  while (!atEnd() && peek().tokenType == Word)
    names.push_back(advance().value);
  server.setServerNames(std::move(names));
  expectSemicolon();
}

void Parser::parseErrorPageDirective(ServerConfig &server) {
  // Collect all leading digit words as status codes, last word is the path.
  std::vector<std::string> words;
  while (!atEnd() && peek().tokenType == Word)
    words.push_back(advance().value);
  if (words.size() < 2)
    throw std::runtime_error(
        "error_page requires at least one code and a path");
  std::filesystem::path path{words.back()};
  for (std::size_t i = 0; i + 1 < words.size(); ++i)
    server.setErrorPage(std::stoi(words[i]), path);
  expectSemicolon();
}

void Parser::parseRootDirective(ServerConfig &server) {
  server.setRoot(std::filesystem::path{expectWord()});
  expectSemicolon();
}

void Parser::parseRootDirective(LocationConfig &location) {
  location.setRoot(std::filesystem::path{expectWord()});
  expectSemicolon();
}

void Parser::parseIndexDirective(ServerConfig &server) {
  std::vector<std::string> files;
  while (!atEnd() && peek().tokenType == Word)
    files.push_back(advance().value);
  server.setIndexFiles(std::move(files));
  expectSemicolon();
}

void Parser::parseIndexDirective(LocationConfig &location) {
  std::vector<std::string> files;
  while (!atEnd() && peek().tokenType == Word)
    files.push_back(advance().value);
  location.setIndexFiles(std::move(files));
  expectSemicolon();
}

void Parser::parseClientMaxBodySizeDirective(ServerConfig &server) {
  server.setClientMaxBodySize(parseBodySize(expectWord()));
  expectSemicolon();
}

void Parser::parseClientMaxBodySizeDirective(LocationConfig &location) {
  location.setClientMaxBodySize(
      std::optional<std::size_t>{parseBodySize(expectWord())});
  expectSemicolon();
}

void Parser::parseMethodsDirective(LocationConfig &location) {
  location.clearAllowedMethods();
  while (!atEnd() && peek().tokenType == Word)
    location.allowMethod(parseMethod(advance().value));
  expectSemicolon();
}

void Parser::parseAutoindexDirective(LocationConfig &location) {
  std::string value = expectWord();
  if (value == "on")
    location.setAutoindex(true);
  else if (value == "off")
    location.setAutoindex(false);
  else
    throw std::runtime_error("autoindex value must be 'on' or 'off', got: " +
                             value);
  expectSemicolon();
}

void Parser::parseReturnDirective(LocationConfig &location) {
  int code = std::stoi(expectWord());
  std::string target = expectWord();
  location.setRedirect(RedirectRule{code, std::move(target)});
  expectSemicolon();
}

void Parser::parseUploadStoreDirective(LocationConfig &location) {
  location.setUploadDirectory(std::filesystem::path{expectWord()});
  expectSemicolon();
}

void Parser::parseCgiDirective(LocationConfig &location) {
  std::string ext = expectWord();
  std::string executable = expectWord();
  location.setCgiHandler(std::move(ext),
                         std::filesystem::path{std::move(executable)});
  expectSemicolon();
}

ListenEndpoint Parser::parseEndpoint(const std::string &value) {
  std::size_t colon = value.rfind(':');
  if (colon != std::string::npos && colon > 0) {
    std::string host = value.substr(0, colon);
    unsigned long port = std::stoul(value.substr(colon + 1));
    if (port == 0 || port > 65535)
      throw std::runtime_error("invalid port in listen endpoint: " + value);
    return ListenEndpoint{std::move(host), static_cast<std::uint16_t>(port)};
  }
  bool allDigits = !value.empty() &&
                   std::all_of(value.begin(), value.end(),
                               [](unsigned char c) { return std::isdigit(c); });
  if (allDigits) {
    unsigned long port = std::stoul(value);
    if (port == 0 || port > 65535)
      throw std::runtime_error("invalid port in listen endpoint: " + value);
    return ListenEndpoint{"0.0.0.0", static_cast<std::uint16_t>(port)};
  }
  throw std::runtime_error("invalid listen endpoint: " + value);
}

std::size_t Parser::parseBodySize(const std::string &value) {
  if (value.empty())
    throw std::runtime_error("empty client_max_body_size value");
  char suffix =
      static_cast<char>(std::toupper(static_cast<unsigned char>(value.back())));
  std::size_t multiplier = 1;
  std::string digits = value;
  if (suffix == 'K') {
    multiplier = 1024ULL;
    digits = value.substr(0, value.size() - 1);
  } else if (suffix == 'M') {
    multiplier = 1024ULL * 1024ULL;
    digits = value.substr(0, value.size() - 1);
  } else if (suffix == 'G') {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
    digits = value.substr(0, value.size() - 1);
  }
  std::size_t base = std::stoull(digits);
  std::size_t result = base * multiplier;
  if (multiplier > 1 && result / multiplier != base)
    throw std::runtime_error("client_max_body_size overflow: " + value);
  return result;
}

HttpMethod Parser::parseMethod(const std::string &value) {
  HttpMethod method = parseHttpMethod(value);
  if (method == HttpMethod::Unknown)
    throw std::runtime_error("unknown HTTP method: " + value);
  return method;
}

} // namespace webserv
