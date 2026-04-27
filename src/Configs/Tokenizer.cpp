#include "Tokenizer.hpp"

#include <cstddef>
#include <fstream>
#include <sstream>

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

void readStream(std::istream &src, std::string &dest) noexcept {
  std::string buf;

  while (std::getline(src, buf)) {
    std::size_t commentPos = buf.find("#");

    if (commentPos != std::string::npos)
      buf.erase(buf.begin() + commentPos, buf.end());

    dest.append(buf);
  }
}

} // namespace

Tokenizer::Tokenizer(std::string fullFileContent) noexcept {
  std::istringstream input(fullFileContent);

  readStream(input, _fileContent);
  tokenizeContent();
}

void Tokenizer::readFile(std::string fileName) {
  std::ifstream reader(fileName);

  readStream(reader, _fileContent);
  tokenizeContent();
}

const std::vector<Token> &Tokenizer::getTokens() const noexcept {
  return _tokens;
}

void Tokenizer::tokenizeContent() {
  std::size_t startPos = 0;

  for (std::size_t i = 0; i < _fileContent.length(); i++) {
    char letter = _fileContent[i];

    if (isDelimiter(letter, " ")) {
      if (startPos < i)
        _tokens.push_back({Word, _fileContent.substr(startPos, i - startPos)});
      startPos = i + 1;
    } else if (isDelimiter(letter, "{};")) {
      if (startPos < i)
        _tokens.push_back({Word, _fileContent.substr(startPos, i - startPos)});

      _tokens.push_back({getDelimType(letter), std::string(1, letter)});
      startPos = i + 1;
    }
  }

  if (startPos < _fileContent.length())
    _tokens.push_back({Word, _fileContent.substr(startPos)});
}

} // namespace webserv
