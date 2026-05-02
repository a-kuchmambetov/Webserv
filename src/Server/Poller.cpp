#include "Poller.hpp"
#include "HttpTypes.hpp"

#include <cerrno>
#include <cstddef>
#include <stdexcept>

namespace webserv {

Poller::Poller() : _epollFd(epoll_create1(EPOLL_CLOEXEC)), _events(64) {
  if (_epollFd.get() == -1)
    throw std::runtime_error("epoll_create1 failed");
}

void Poller::addFd(int fd, uint32_t events, FdType fdType) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;

  if (epoll_ctl(_epollFd.get(), EPOLL_CTL_ADD, fd, &ev) == -1) {
    throw std::runtime_error("epoll_ctl ADD fd failed");
  }

  _pollingFds.emplace(fd, fdType);
}

void Poller::modFd(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;

  if (epoll_ctl(_epollFd.get(), EPOLL_CTL_MOD, fd, &ev) == -1)
    throw std::runtime_error("epoll_ctl MOD fd failed");
}

void Poller::removeFd(int fd) noexcept {
  // Best-effort: ENOENT means it's already gone, anything else we swallow
  // because removeFd is called from cleanup paths where throwing is harmful.
  _pollingFds.erase(fd);
  epoll_ctl(_epollFd.get(), EPOLL_CTL_DEL, fd, nullptr);
}

EventsData Poller::getEventsReady() {
  int n = epoll_wait(_epollFd.get(), _events.data(),
                     static_cast<int>(_events.size()), _timeout);
  if (n == -1) {
    if (errno == EINTR)
      return {};
    throw std::runtime_error("epoll_wait failed");
  }
  return {_events.data(), static_cast<std::size_t>(n)};
}

FdType Poller::getFdType(int fd) const noexcept {
  auto it = _pollingFds.find(fd);
  if (it == _pollingFds.end())
    return FdType::Unknown;
  return it->second;
}

} // namespace webserv
