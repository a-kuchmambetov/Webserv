#include "Tokenizer.hpp"

#include <cstddef>
#include <fstream>

namespace webserv {

namespace {

bool isDelimiter(char element, std::string delimeters) {
  for (char letter : delimeters) {
    if (letter == element)
      return true;
  }
  return false;
}

TokenType getDelimType(char letter) {
  switch (letter) {
  case '{':
    return OpenBracket;
  case '}':
    return CloseBracket;
  case ';':
    return Semicol;
  default:
    return Undefined;
  }
}

} // namespace

void Tokenizer::readFile(std::string fileName) {
  std::ifstream reader(fileName);

  std::string buf;
  while (std::getline(reader, buf)) {
    std::size_t commentPos = buf.find("#");

    if (commentPos != std::string::npos)
      buf.erase(buf.begin() + commentPos, buf.end());

    _file_content.append(buf);
  }
  tokenizeContent();
}

const std::vector<Token> &Tokenizer::getTokens() const noexcept {
  return _tokens;
}

void Tokenizer::tokenizeContent() {
  std::size_t startPos = 0;

  for (std::size_t i = 0; i < _file_content.length(); i++) {
    char letter = _file_content[i];

    if (isDelimiter(letter, " ")) {
      if (startPos < i)
        _tokens.push_back({Word, _file_content.substr(startPos, i - startPos)});
      startPos = i + 1;
    } else if (isDelimiter(letter, "{};")) {
      if (startPos < i)
        _tokens.push_back({Word, _file_content.substr(startPos, i - startPos)});

      _tokens.push_back({getDelimType(letter), std::string(1, letter)});
      startPos = i + 1;
    }
  }

  if (startPos < _file_content.length())
    _tokens.push_back({Word, _file_content.substr(startPos)});
}

} // namespace webserv
