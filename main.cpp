#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int parsePositiveInt(const std::string &s) {
  int value = 0;

  for (const char c : s) {
    if (c < '0' || c > '9')
      return -1;

    value = value * 10 + (c - '0');
  }

  return value;
}

static int parseContentLength(const std::string &headers) {
  const std::string key = "Content-Length:";
  std::string::size_type pos = headers.find(key);
  if (pos == std::string::npos)
    return 0;

  pos += key.length();

  while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t'))
    ++pos;

  std::string number;
  while (pos < headers.size() && headers[pos] >= '0' && headers[pos] <= '9') {
    number += headers[pos];
    ++pos;
  }

  if (number.empty())
    return 0;

  return parsePositiveInt(number);
}

static std::string receiveHttpRequest(const int clientFd) {
  std::string request;
  char buffer[4096];

  std::string::size_type headerEnd = std::string::npos;
  int contentLength = -1;

  while (true) {
    const ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
    if (bytesRead <= 0)
      break;

    request.append(buffer, bytesRead);

    if (headerEnd == std::string::npos) {
      headerEnd = request.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        std::string headers = request.substr(0, headerEnd + 4);
        contentLength = parseContentLength(headers);
      }
    }

    if (headerEnd != std::string::npos && contentLength >= 0) {
      if (request.size() - headerEnd + 4 >=
          static_cast<std::string::size_type>(contentLength))
        break;
    }
  }

  return request;
}

int main() {
  const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd < 0) {
    std::cerr << "socket() failed\n";
    return 1;
  }

  constexpr int opt = 1;
  if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "setsockopt() failed\n";
    close(serverFd);
    return 1;
  }

  sockaddr_in serverAddr = sockaddr_in();
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(8080);
  serverAddr.sin_addr.s_addr = INADDR_ANY;

  if (bind(serverFd, reinterpret_cast<struct sockaddr *>(&serverAddr),
           sizeof(serverAddr)) < 0) {
    std::cerr << "bind() failed\n";
    close(serverFd);
    return 1;
  }

  if (listen(serverFd, 10) < 0) {
    std::cerr << "listen() failed\n";
    close(serverFd);
    return 1;
  }

  std::cout << "Listening on 0.0.0.0:8080\n";
  std::cout << "Send POST requests to http://127.0.0.1:8080/\n\n";

  while (true) {
    sockaddr_in clientAddr = sockaddr_in();
    socklen_t clientLen = sizeof(clientAddr);

    const int clientFd = accept(
        serverFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
    if (clientFd < 0) {
      std::cerr << "accept() failed\n";
      continue;
    }

    std::cout << "Client connected: " << inet_ntoa(clientAddr.sin_addr) << ":"
              << ntohs(clientAddr.sin_port) << "\n";

    std::string request = receiveHttpRequest(clientFd);

    const std::string::size_type headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
      std::string body = request.substr(headerEnd + 4);

      std::cout << "----- POST BODY -----\n";
      std::cout << body << "\n";
      std::cout << "---------------------\n\n";
    } else {
      std::cout << "Could not parse HTTP request\n";
    }

    std::string response = "HTTP/1.1 204 No Content\r\n"
                           "Connection: close\r\n"
                           "\r\n";

    send(clientFd, response.c_str(), response.size(), 0);
    close(clientFd);
  }

  close(serverFd);
  return 0;
}
