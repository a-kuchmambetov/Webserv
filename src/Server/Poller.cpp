#include "Poller.hpp"
#include "HttpTypes.hpp"
#include "UniqueFd.hpp"

#include <stdexcept>

namespace webserv {

Poller::Poller() : _epollFd(epoll_create1(EPOLL_CLOEXEC)) {
  if (_epollFd.get() == -1)
    throw std::runtime_error("epoll_create1 failed");
}

void Poller::addFd(const UniqueFd &fd, const uint32_t events,
                   const FdType fdType) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd.get();

  if (epoll_ctl(_epollFd.get(), EPOLL_CTL_ADD, fd.get(), &ev) == -1) {
    throw std::runtime_error("epoll_ctl ADD fd failed");
  }

  _pollingFds.emplace(fd.get(), fdType);
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

int Poller::getReadyEventsCount(std::vector<epoll_event> &events,
                                const int timeout) const {
  return epoll_wait(_epollFd.get(), events.data(),
                    static_cast<int>(events.size()), timeout);
}

FdType Poller::getFdType(const int fd) const noexcept {
  return _pollingFds.at(fd);
}

} // namespace webserv
