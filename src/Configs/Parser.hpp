#pragma once

#include "ServerConfig.hpp"
#include "Tokenizer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace webserv {

class Parser {
public:
  explicit Parser(const std::vector<Token> &tokens);

  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;
  Parser(Parser &&) = delete;
  Parser &operator=(Parser &&) = delete;

  [[nodiscard]] std::vector<ServerConfig> parse();

private:
  [[nodiscard]] bool atEnd() const noexcept;
  [[nodiscard]] const Token &peek() const;
  const Token &advance();
  std::string expectWord();
  void expectOpenBracket();
  void expectCloseBracket();
  void expectSemicolon();

  ServerConfig parseServer();
  LocationConfig parseLocation();

  void parseListenDirective(ServerConfig &server);
  void parseServerNameDirective(ServerConfig &server);
  void parseErrorPageDirective(ServerConfig &server);

  void parseRootDirective(ServerConfig &server);
  void parseRootDirective(LocationConfig &location);
  void parseIndexDirective(ServerConfig &server);
  void parseIndexDirective(LocationConfig &location);
  void parseClientMaxBodySizeDirective(ServerConfig &server);
  void parseClientMaxBodySizeDirective(LocationConfig &location);
  void parseClientMaxHeaderSizeDirective(ServerConfig &server);

  void parseMethodsDirective(LocationConfig &location);
  void parseAutoindexDirective(LocationConfig &location);
  void parseReturnDirective(LocationConfig &location);
  void parseUploadStoreDirective(LocationConfig &location);
  void parseCgiDirective(LocationConfig &location);

  [[nodiscard]] static ListenEndpoint parseEndpoint(const std::string &value);
  [[nodiscard]] static std::size_t
  parseSize(const std::string &value, const std::string runtimeErrorName);
  [[nodiscard]] static std::size_t parseBodySize(const std::string &value);
  [[nodiscard]] static std::size_t parseHeaderSize(const std::string &value);
  [[nodiscard]] static HttpMethod parseMethod(const std::string &value);

  const std::vector<Token> &_tokens;
  std::size_t _pos{0};
};

} // namespace webserv
