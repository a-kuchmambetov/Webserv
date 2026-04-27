#pragma once

#include <string>
#include <vector>

namespace webserv {

enum TokenType { Word, OpenBracket, CloseBracket, Semicol, Undefined };

struct Token {
  TokenType tokenType;
  std::string value;
};

class Tokenizer {
public:
  Tokenizer() = default;
  Tokenizer(std::string fullFileContent) noexcept;
  ~Tokenizer() = default;

  Tokenizer(const Tokenizer &) = delete;
  Tokenizer &operator=(const Tokenizer &) = delete;
  Tokenizer(Tokenizer &&) = delete;
  Tokenizer &operator=(Tokenizer &&) = delete;

  void readFile(std::string fileName);
  const std::vector<Token> &getTokens() const noexcept;

private:
  void tokenizeContent();

  std::string _fileContent;
  std::vector<Token> _tokens;
};
} // namespace webserv