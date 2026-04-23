#include "Parser.hpp"
#include "ServerConfig.hpp"
#include "Tokenizer.hpp"
#include "Validator.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

constexpr std::string GREEN = "\033[32m";
constexpr std::string YELLOW = "\033[33m";
constexpr std::string RED = "\033[31m";
constexpr std::string RESET = "\033[0m";

void testCase(const std::string filePath) {
  using namespace webserv;

  std::cout << "--- Start of test case for " << YELLOW << filePath << RESET
            << " ---" << std::endl;
  try {
    Tokenizer tokenizer;
    tokenizer.readFile(filePath);
    Parser parser(tokenizer.getTokens());
    const std::vector<ServerConfig> t = parser.parse();

    Validator validator(t);

    validator.validate();
  } catch (const std::exception &e) {
    std::cout << RED << "Error: " << e.what() << RESET << std::endl;
    std::cout << "--- End of test case ---" << std::endl << std::endl;
    return;
  }

  std::cout << GREEN << "No errors in " << filePath << RESET << std::endl;
  std::cout << "--- End of test case ---" << std::endl << std::endl;
}

int main() {
  testCase("fixtures/valid_big.conf");
  testCase("fixtures/valid_full.conf");
  testCase("fixtures/invalid_missing_semi.conf");
  testCase("fixtures/valid_spaced.conf");
}
