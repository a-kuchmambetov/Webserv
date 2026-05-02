#pragma once

#include "HttpTypes.hpp"
#include "UniqueFd.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include <sys/epoll.h>

namespace webserv {

using EventsData = std::span<const epoll_event>;

class Poller {
public:
  Poller();
  ~Poller() = default;

  Poller(const Poller &) = delete;
  Poller &operator=(const Poller &) = delete;
  Poller(Poller &&other) = delete;
  Poller &operator=(Poller &&other) = delete;

  void addFd(int fd, uint32_t events, FdType fdType = FdType::Listener);
  void modFd(int fd, uint32_t events);
  void removeFd(int fd) noexcept;

  [[nodiscard]] EventsData getEventsReady();
  [[nodiscard]] FdType getFdType(int fd) const noexcept;

private:
  UniqueFd _epollFd;
  std::vector<epoll_event> _events;
  int _timeout{1000};
  std::map<int, FdType> _pollingFds;
};

} // namespace webserv
