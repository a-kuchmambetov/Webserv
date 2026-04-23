#include "HttpTypes.hpp"
#include "LocationConfig.hpp"
#include "Parser.hpp"
#include "ServerConfig.hpp"
#include "Tokenizer.hpp"

#include <exception>
#include <iostream>
#include <vector>

namespace {

void printLocationConfigContent(const webserv::LocationConfig &location) {
  std::cout << "Location path prefix: " << location.pathPrefix() << std::endl;

  for (const webserv::HttpMethod &method : location.allowedMethods())
    std::cout << "Allowed method: " << webserv::toString(method) << std::endl;

  if (location.redirect().has_value())
    std::cout << "Redirect status: " << location.redirect()->statusCode
              << ", target: " << location.redirect()->target << std::endl;

  std::cout << "Path to the root folder: " << location.root() << std::endl;
  std::cout << "Autoindex: " << (location.autoindexEnabled() ? "on" : "off")
            << std::endl;

  for (const std::string &file_name : location.indexFiles())
    std::cout << "Index file name: " << file_name << std::endl;

  if (location.uploadDirectory().has_value())
    std::cout << "Upload directory: " << *location.uploadDirectory()
              << std::endl;

  for (const auto &handler : location.cgiHandlers())
    std::cout << "CGI extension: " << handler.first
              << ", executable: " << handler.second << std::endl;

  if (location.clientMaxBodySize().has_value())
    std::cout << "Client max body size: " << *location.clientMaxBodySize()
              << std::endl;
}

void printServerConfigContent(const webserv::ServerConfig &server) {
  for (const std::string &server_name : server.serverNames())
    std::cout << "Server name: " << server_name << std::endl;

  std::cout << "Client max body size: " << server.clientMaxBodySize()
            << std::endl;
  std::cout << "Path to the root folder: " << server.root() << std::endl;

  for (const std::string &file_name : server.indexFiles())
    std::cout << "Index file name: " << file_name << std::endl;

  for (const auto &errorPage : server.errorPages())
    std::cout << "Error page code: " << errorPage.first
              << ", error page path: " << errorPage.second << std::endl;

  for (const auto &location : server.locations()) {
    std::cout << std::endl;
    printLocationConfigContent(location);
  }
}

void testCase(const std::string filePath) {
  using namespace webserv;

  Tokenizer tokenizer;
  tokenizer.readFile(filePath);
  Parser parser(tokenizer.getTokens());
  const std::vector<ServerConfig> t = parser.parse();

  std::cout << "--- Start of test case for " << filePath << " ---" << std::endl;
  for (const auto &serverData : t) {
    printServerConfigContent(serverData);
    std::cout << std::endl;
  }

  std::cout << "--- End of test case ---" << std::endl << std::endl;
}

} // namespace

int main() {
  try {
    testCase("fixtures/valid_big.conf");
    testCase("fixtures/valid_full.conf");
    testCase("fixtures/valid_spaced.conf");
  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }
}
