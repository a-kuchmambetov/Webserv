#include "UniqueFd.hpp"

UniqueFd::UniqueFd() noexcept : _fd(-1) {}
UniqueFd::UniqueFd(int fd) noexcept : _fd(fd) {}

UniqueFd::~UniqueFd() {
  if (_fd >= 0)
    close(_fd);
}

UniqueFd::UniqueFd(UniqueFd &&other) noexcept : _fd(other._fd) {
  other._fd = -1;
}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
  if (this != &other) {
    if (_fd >= 0)
      close(_fd);
    _fd = other._fd;
    other._fd = -1;
  }
  return *this;
}

int UniqueFd::get() const noexcept { return _fd; }

int UniqueFd::release() noexcept {
  int fd = _fd;
  _fd = -1;
  return fd;
}
