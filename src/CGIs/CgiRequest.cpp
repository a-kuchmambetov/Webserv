#include "CgiRequest.hpp"
#include "HttpRequest.hpp"
#include "LocationConfig.hpp"
#include "ServerConfig.hpp"

#include <cctype>
#include <cerrno>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace webserv {

CgiRequest::~CgiRequest() {
  terminate();
  closePipes();
}

CgiRequest::CgiRequest(CgiRequest &&other) noexcept
    : _config(std::move(other._config)),
      _environment(std::move(other._environment)),
      _requestBodyToWrite(std::move(other._requestBodyToWrite)),
      _requestBodyWriteOffset(other._requestBodyWriteOffset),
      _outputBuffer(std::move(other._outputBuffer)),
      _pid(other._pid),
      _stdinWriteFd(other._stdinWriteFd),
      _stdoutReadFd(other._stdoutReadFd),
      _stdinClosed(other._stdinClosed),
      _stdoutClosed(other._stdoutClosed),
      _finished(other._finished),
      _exitStatus(other._exitStatus) {
  other._pid = -1;
  other._stdinWriteFd = -1;
  other._stdoutReadFd = -1;
  other._stdinClosed = true;
  other._stdoutClosed = true;
}

CgiRequest &CgiRequest::operator=(CgiRequest &&other) noexcept {
  if (this != &other) {
    terminate();
    closePipes();
    _config = std::move(other._config);
    _environment = std::move(other._environment);
    _requestBodyToWrite = std::move(other._requestBodyToWrite);
    _requestBodyWriteOffset = other._requestBodyWriteOffset;
    _outputBuffer = std::move(other._outputBuffer);
    _pid = other._pid;
    _stdinWriteFd = other._stdinWriteFd;
    _stdoutReadFd = other._stdoutReadFd;
    _stdinClosed = other._stdinClosed;
    _stdoutClosed = other._stdoutClosed;
    _finished = other._finished;
    _exitStatus = other._exitStatus;
    other._pid = -1;
    other._stdinWriteFd = -1;
    other._stdoutReadFd = -1;
    other._stdinClosed = true;
    other._stdoutClosed = true;
  }
  return *this;
}

void CgiRequest::configure(const HttpRequest &request,
                           const ServerConfig &server,
                           const LocationConfig &location,
                           CgiExecutionConfig config) {
  _config = std::move(config);
  _requestBodyToWrite = std::string(request.body());
  _requestBodyWriteOffset = 0;
  buildEnvironment(request, server, location);
}

void CgiRequest::buildEnvironment(const HttpRequest &request,
                                  const ServerConfig &server,
                                  const LocationConfig &location) {
  (void)location;
  _environment.clear();

  _environment["GATEWAY_INTERFACE"] = "CGI/1.1";
  _environment["SERVER_PROTOCOL"] = "HTTP/1.1";
  _environment["SERVER_SOFTWARE"] = "webserv/1.0";

  if (!server.listenEndpoints().empty()) {
    _environment["SERVER_NAME"] = server.listenEndpoints()[0].host;
    _environment["SERVER_PORT"] =
        std::to_string(server.listenEndpoints()[0].port);
  }

  _environment["REQUEST_METHOD"] = std::string(request.methodText());
  _environment["QUERY_STRING"] = std::string(request.queryString());
  _environment["SCRIPT_NAME"] = std::string(request.path());
  _environment["PATH_INFO"] = _config.pathInfo;
  _environment["SCRIPT_FILENAME"] = _config.scriptPath.string();

  if (request.hasContentLength())
    _environment["CONTENT_LENGTH"] = std::to_string(request.contentLength());

  if (auto ct = request.header("Content-Type"))
    _environment["CONTENT_TYPE"] = std::string(*ct);

  // HTTP headers → HTTP_<NAME> (RFC 3875 §4.1.18)
  for (const auto &[name, value] : request.headers()) {
    if (name == "Content-Type" || name == "Content-Length")
      continue;
    std::string envName = "HTTP_";
    for (char c : name)
      envName += (c == '-')
                     ? '_'
                     : static_cast<char>(
                           std::toupper(static_cast<unsigned char>(c)));
    _environment[envName] = value;
  }

  for (const auto &[key, val] : _config.extraEnvironment)
    _environment[key] = val;
}

bool CgiRequest::launch() {
  int stdinPipe[2];
  int stdoutPipe[2];

  if (pipe(stdinPipe) == -1)
    return false;

  if (pipe(stdoutPipe) == -1) {
    close(stdinPipe[0]);
    close(stdinPipe[1]);
    return false;
  }

  // Parent ends are non-blocking so epoll can drive them
  if (fcntl(stdinPipe[1], F_SETFL, O_NONBLOCK) == -1 ||
      fcntl(stdoutPipe[0], F_SETFL, O_NONBLOCK) == -1) {
    close(stdinPipe[0]);
    close(stdinPipe[1]);
    close(stdoutPipe[0]);
    close(stdoutPipe[1]);
    return false;
  }

  // Build before fork — vectors live in parent's address space
  auto argvStorage = buildArgv();
  auto argv = buildMutableCStringVector(argvStorage);
  auto envpStorage = buildEnvpStorage();
  auto envp = buildMutableCStringVector(envpStorage);

  pid_t pid = fork();
  if (pid == -1) {
    close(stdinPipe[0]);
    close(stdinPipe[1]);
    close(stdoutPipe[0]);
    close(stdoutPipe[1]);
    return false;
  }

  if (pid == 0) {
    // Child: wire pipes to standard streams then exec
    dup2(stdinPipe[0], STDIN_FILENO);
    dup2(stdoutPipe[1], STDOUT_FILENO);
    close(stdinPipe[0]);
    close(stdinPipe[1]);
    close(stdoutPipe[0]);
    close(stdoutPipe[1]);

    if (!_config.workingDirectory.empty())
      chdir(_config.workingDirectory.c_str());

    execve(argv[0], argv.data(), envp.data());
    _exit(1); // execve only returns on failure
  }

  // Parent: keep only the ends we use
  close(stdinPipe[0]);
  close(stdoutPipe[1]);

  _pid = pid;
  _stdoutReadFd = stdoutPipe[0];
  _stdoutClosed = false;

  if (_requestBodyToWrite.empty()) {
    close(stdinPipe[1]);
    _stdinWriteFd = -1;
    _stdinClosed = true;
  } else {
    _stdinWriteFd = stdinPipe[1];
    _stdinClosed = false;
  }

  return true;
}

int CgiRequest::stdinFd() const noexcept { return _stdinWriteFd; }
int CgiRequest::stdoutFd() const noexcept { return _stdoutReadFd; }
pid_t CgiRequest::pid() const noexcept { return _pid; }

bool CgiRequest::wantsRead() const noexcept {
  return !_stdoutClosed && _stdoutReadFd != -1;
}

bool CgiRequest::wantsWrite() const noexcept {
  return !_stdinClosed && _stdinWriteFd != -1;
}

bool CgiRequest::isFinished() const noexcept { return _finished; }

IoResult CgiRequest::onStdoutReadable() {
  char buf[16 * 1024];
  ssize_t n = read(_stdoutReadFd, buf, sizeof(buf));

  if (n > 0) {
    _outputBuffer.append(buf, static_cast<std::size_t>(n));
    return IoResult::Continue;
  }
  if (n == 0 || (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
    close(_stdoutReadFd);
    _stdoutReadFd = -1;
    _stdoutClosed = true;
    _finished = true;
    return IoResult::Closed;
  }
  return IoResult::Continue;
}

IoResult CgiRequest::onStdinWritable() {
  if (_requestBodyWriteOffset >= _requestBodyToWrite.size()) {
    close(_stdinWriteFd);
    _stdinWriteFd = -1;
    _stdinClosed = true;
    return IoResult::Complete;
  }

  const char *ptr = _requestBodyToWrite.data() + _requestBodyWriteOffset;
  std::size_t remaining =
      _requestBodyToWrite.size() - _requestBodyWriteOffset;

  ssize_t n = write(_stdinWriteFd, ptr, remaining);
  if (n > 0) {
    _requestBodyWriteOffset += static_cast<std::size_t>(n);
    if (_requestBodyWriteOffset >= _requestBodyToWrite.size()) {
      close(_stdinWriteFd);
      _stdinWriteFd = -1;
      _stdinClosed = true;
      return IoResult::Complete;
    }
    return IoResult::Continue;
  }
  if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return IoResult::Continue;

  close(_stdinWriteFd);
  _stdinWriteFd = -1;
  _stdinClosed = true;
  return IoResult::Error;
}

std::string_view CgiRequest::outputBuffer() const noexcept {
  return _outputBuffer;
}

std::string CgiRequest::releaseOutputBuffer() {
  return std::move(_outputBuffer);
}

bool CgiRequest::reap(int waitStatus) noexcept {
  if (_pid == -1)
    return false;
  _exitStatus = WEXITSTATUS(waitStatus);
  _pid = -1;
  _finished = true;
  return true;
}

std::optional<int> CgiRequest::exitStatus() const noexcept {
  return _exitStatus;
}

void CgiRequest::terminate() noexcept {
  if (_pid == -1)
    return;
  kill(_pid, SIGTERM);
  int status;
  waitpid(_pid, &status, 0);
  _pid = -1;
}

void CgiRequest::closePipes() noexcept {
  if (_stdinWriteFd != -1) {
    close(_stdinWriteFd);
    _stdinWriteFd = -1;
    _stdinClosed = true;
  }
  if (_stdoutReadFd != -1) {
    close(_stdoutReadFd);
    _stdoutReadFd = -1;
    _stdoutClosed = true;
  }
}

std::vector<std::string> CgiRequest::buildArgv() const {
  std::vector<std::string> argv;
  argv.push_back(_config.executablePath.string());
  argv.push_back(_config.scriptPath.string());
  for (const auto &arg : _config.argv)
    argv.push_back(arg);
  return argv;
}

std::vector<std::string> CgiRequest::buildEnvpStorage() const {
  std::vector<std::string> envp;
  envp.reserve(_environment.size());
  for (const auto &[key, value] : _environment)
    envp.push_back(key + "=" + value);
  return envp;
}

std::vector<char *>
CgiRequest::buildMutableCStringVector(std::vector<std::string> &storage) {
  std::vector<char *> ptrs;
  ptrs.reserve(storage.size() + 1);
  for (auto &s : storage)
    ptrs.push_back(s.data());
  ptrs.push_back(nullptr);
  return ptrs;
}

} // namespace webserv
