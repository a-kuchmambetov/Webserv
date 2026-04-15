#pragma once

#include "HttpTypes.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace webserv {

class HttpRequest;
class LocationConfig;
class ServerConfig;

struct CgiExecutionConfig {
  std::filesystem::path scriptPath;
  std::filesystem::path executablePath;
  std::filesystem::path workingDirectory;
  std::string pathInfo;
  std::vector<std::string> argv;
  EnvironmentMap extraEnvironment;
};

class CgiRequest {
public:
  CgiRequest() = default;
  ~CgiRequest();

  CgiRequest(const CgiRequest &) = delete;
  CgiRequest &operator=(const CgiRequest &) = delete;
  CgiRequest(CgiRequest &&other) noexcept;
  CgiRequest &operator=(CgiRequest &&other) noexcept;

  void configure(const HttpRequest &request, const ServerConfig &server,
                 const LocationConfig &location, CgiExecutionConfig config);
  [[nodiscard]] bool launch();

  [[nodiscard]] int stdinFd() const noexcept;
  [[nodiscard]] int stdoutFd() const noexcept;
  [[nodiscard]] pid_t pid() const noexcept;

  [[nodiscard]] bool wantsRead() const noexcept;
  [[nodiscard]] bool wantsWrite() const noexcept;
  [[nodiscard]] bool isFinished() const noexcept;

  [[nodiscard]] IoResult onStdoutReadable();
  [[nodiscard]] IoResult onStdinWritable();

  [[nodiscard]] std::string_view outputBuffer() const noexcept;
  [[nodiscard]] std::string releaseOutputBuffer();

  [[nodiscard]] bool reap(int waitStatus) noexcept;
  [[nodiscard]] std::optional<int> exitStatus() const noexcept;

  void terminate() noexcept;
  void closePipes() noexcept;

private:
  void buildEnvironment(const HttpRequest &request, const ServerConfig &server,
                        const LocationConfig &location);
  [[nodiscard]] std::vector<std::string> buildArgv() const;
  [[nodiscard]] std::vector<std::string> buildEnvpStorage() const;
  [[nodiscard]] static std::vector<char *>
  buildMutableCStringVector(std::vector<std::string> &storage);

  CgiExecutionConfig _config;
  EnvironmentMap _environment;
  std::string _requestBodyToWrite;
  std::size_t _requestBodyWriteOffset{0};
  std::string _outputBuffer;

  pid_t _pid{-1};
  int _stdinWriteFd{-1};
  int _stdoutReadFd{-1};

  bool _stdinClosed{true};
  bool _stdoutClosed{true};
  bool _finished{false};
  std::optional<int> _exitStatus;
};

} // namespace webserv
