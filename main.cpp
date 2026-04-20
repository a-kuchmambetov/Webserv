#include "ServerConfig.hpp"
#include "WebServer.hpp"

#include <iostream>

int main() {
  webserv::ServerConfig serverConfig;

  std::vector<std::string> names;
  names.push_back("Static.local");
  serverConfig.setServerNames(names);
  // serverConfig.addListenEndpoint({"127.0.0.1", 8080});
  serverConfig.addListenEndpoint({"0.0.0.0", 8080});

  std::vector<webserv::ServerConfig> servers;
  servers.push_back(serverConfig);
  webserv::WebServer server(servers);

  try {
    server.run();
  } catch (const std::runtime_error &e) {
    std::cerr << "Error: " << e.what() << '\n';
  }
  return 0;
}
