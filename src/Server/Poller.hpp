#pragma once

#include "HttpTypes.hpp"
#include "UniqueFd.hpp"

#include <cstdint>
#include <map>
#include <vector>

#include <sys/epoll.h>

namespace webserv {

struct EventsData {
  int eventsReadyN;
  std::vector<epoll_event> *events;
};

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
  
  [[nodiscard]] EventsData getEventsReady();
  [[nodiscard]] FdType getFdType(const int fd) const noexcept;

private:
  UniqueFd _epollFd;
  std::vector<epoll_event> _events;
  int _timeout{-1};
  std::map<int, FdType> _pollingFds;
};

} // namespace webserv
