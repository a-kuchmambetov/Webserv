#include "WebServer.hpp"

#include <iostream>

int main() {

  try {
    webserv::WebServer server("server.config");

    server.run();
  } catch (const std::runtime_error &e) {
    std::cerr << "Error: " << e.what() << '\n';
  }
  return 0;
}
