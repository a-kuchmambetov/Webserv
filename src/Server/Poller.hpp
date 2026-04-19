#pragma once

#include "HttpTypes.hpp"
#include "UniqueFd.hpp"

#include <cstdint>
#include <map>
#include <vector>

#include <sys/epoll.h>

namespace webserv {

class Poller {
public:
  Poller();
  ~Poller();

  Poller(const Poller &) = delete;
  Poller &operator=(const Poller &) = delete;
  Poller(Poller &&other) = delete;
  Poller &operator=(Poller &&other) = delete;

  void addFd(const UniqueFd &fd, const uint32_t events,
             const FdType fdType = FdType::Listener);
  void modFd(int fd, uint32_t events);
  void removeFd(int fd) noexcept;
  [[nodiscard]] int getReadyEventsCount(std::vector<epoll_event> &events,
                                        const int timeout = -1) const;
  [[nodiscard]] FdType getFdType(const int fd) const noexcept;

private:
  UniqueFd _epollFd;
  std::map<int, FdType> _pollingFds;
};

} // namespace webserv
