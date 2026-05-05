#include "CgiRequest.hpp"
#include "CgiResult.hpp"
#include "Connection.hpp"
#include "HttpResponse.hpp"
#include "HttpTypes.hpp"
#include "WebServer.hpp"

#include <cstddef>
#include <memory>
#include <utility>

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace webserv {

void WebServer::processCgiInput(int fd) {
  auto owner = _cgiFdToClient.find(fd);
  if (owner == _cgiFdToClient.end()) {
    _poller.removeFd(fd);
    return;
  }

  auto client = _connectionFds.find(owner->second);
  if (client == _connectionFds.end()) {
    _poller.removeFd(fd);
    _cgiFdToClient.erase(owner);
    return;
  }

  CgiRequest *cgi = client->second.connection.getActiveCgi();
  if (!cgi) {
    _poller.removeFd(fd);
    _cgiFdToClient.erase(owner);
    return;
  }

  const IoResult result = cgi->onStdinWritable();
  client->second.connection.markActivity();
  if (result == IoResult::Complete || result == IoResult::Error ||
      !cgi->wantsWrite()) {
    _poller.removeFd(fd);
    _cgiFdToClient.erase(fd);
  }
}

void WebServer::processCgiOutput(int fd, uint32_t events) {
  auto owner = _cgiFdToClient.find(fd);
  if (owner == _cgiFdToClient.end()) {
    _poller.removeFd(fd);
    return;
  }

  auto client = _connectionFds.find(owner->second);
  if (client == _connectionFds.end()) {
    _poller.removeFd(fd);
    _cgiFdToClient.erase(owner);
    return;
  }

  CgiRequest *cgi = client->second.connection.getActiveCgi();
  if (!cgi) {
    _poller.removeFd(fd);
    _cgiFdToClient.erase(owner);
    return;
  }

  bool finished = false;
  if (events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
    while (true) {
      const std::size_t before = cgi->outputBuffer().size();
      const IoResult result = cgi->onStdoutReadable();
      client->second.connection.markActivity();
      if (result == IoResult::Closed || result == IoResult::Error ||
          cgi->isFinished()) {
        finished = true;
        break;
      }
      if (cgi->outputBuffer().size() == before)
        break;
      // Drain greedily on hangup/error so we don't miss the EOF tail;
      // for a plain readable event, level-triggered epoll will re-notify.
      if ((events & (EPOLLHUP | EPOLLERR)) == 0)
        break;
    }
  }

  if (finished)
    finishCgi(client->first, fd);
}

void WebServer::finishCgi(int clientFd, int outputFd) {
  auto client = _connectionFds.find(clientFd);
  if (client == _connectionFds.end()) {
    _poller.removeFd(outputFd);
    _cgiFdToClient.erase(outputFd);
    return;
  }

  Connection &conn = client->second.connection;
  std::unique_ptr<CgiRequest> cgi = conn.detachCgi();
  if (!cgi) {
    _poller.removeFd(outputFd);
    _cgiFdToClient.erase(outputFd);
    return;
  }

  const int stdinFd = cgi->stdinFd();
  if (stdinFd != -1) {
    _poller.removeFd(stdinFd);
    _cgiFdToClient.erase(stdinFd);
  }
  _poller.removeFd(outputFd);
  _cgiFdToClient.erase(outputFd);

  if (cgi->pid() != -1) {
    int status = 0;
    const pid_t rc = waitpid(cgi->pid(), &status, WNOHANG);
    if (rc == cgi->pid())
      (void)cgi->reap(status);
    else if (rc == 0)
      cgi->terminate();
  }

  CgiResult result;
  result.append(cgi->releaseOutputBuffer());
  result.finalizeAtEof();

  if (result.hasError())
    return queueError(clientFd, client->second.defaultServer,
                      result.errorStatus());
  if (cgi->exitStatus().has_value() && *cgi->exitStatus() != 0 &&
      !result.headersParsed())
    return queueError(clientFd, client->second.defaultServer, 502);

  queueResponse(clientFd, HttpResponse::fromCgi(result));
}

} // namespace webserv
