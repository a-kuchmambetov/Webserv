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
  ~Tokenizer() = default;

  Tokenizer(const Tokenizer &) = delete;
  Tokenizer &operator=(const Tokenizer &) = delete;
  Tokenizer(Tokenizer &&) = delete;
  Tokenizer &operator=(Tokenizer &&) = delete;

  void readFile(std::string fileName);
  const std::vector<Token> &getTokens() const noexcept;

private:
  void tokenizeContent();

  std::string _file_content;
  std::vector<Token> _tokens;
};
} // namespace webserv