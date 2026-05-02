#include "WebServer.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

bool hasConfExtension(const std::filesystem::path &configPath) {
  return configPath.extension() == ".conf";
}

void printUsage(const char *programName) {
  std::cerr << "Usage: " << programName << " [config.conf]\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 2) {
    printUsage(argv[0]);
    return 1;
  }

  const std::filesystem::path configPath =
      (argc == 2) ? std::filesystem::path(argv[1])
                  : std::filesystem::path("server.conf");

  if (!hasConfExtension(configPath)) {
    std::cerr << "Error: config file must have .conf extension\n";
    printUsage(argv[0]);
    return 1;
  }

  try {
    webserv::WebServer server(configPath);

    server.run();
  } catch (const std::runtime_error &e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
