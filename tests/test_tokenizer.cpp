#include "Tokenizer.hpp"

#include <exception>
#include <iostream>
#include <string>

static const char *tokenColor(webserv::TokenType t) {
  using namespace webserv;

  switch (t) {
  case Word:
    return "\033[32m";
  case OpenBracket:
    return "\033[33m";
  case CloseBracket:
    return "\033[33m";
  case Semicol:
    return "\033[36m";
  case Undefined:
    return "\033[31m";
  }
  return "\033[0m";
}
static void dumpTokens(const std::string &filePath) {
  using namespace webserv;

  Tokenizer tokenizer;
  std::cout << "--- " << filePath << " ---" << std::endl;
  tokenizer.readFile(filePath);
  for (const Token &token : tokenizer.getTokens()) {
    const char *color = tokenColor(token.tokenType);
    bool newline = token.tokenType != Word;
    std::cout << " " << color << token.value << "\033[0m";
    if (newline)
      std::cout << std::endl;
  }
  std::cout << std::endl;
}

int main() {
  try {
    dumpTokens("fixtures/valid_big.conf");
  } catch (const std::exception &e) {
    std::cerr << "Caught exception: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
