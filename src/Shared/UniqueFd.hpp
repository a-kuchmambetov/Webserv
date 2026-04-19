#pragma once

#include <unistd.h>

class UniqueFd {
public:
  UniqueFd() noexcept : _fd(-1) {}
  explicit UniqueFd(int fd) noexcept : _fd(fd) {}

  ~UniqueFd() {
    if (_fd >= 0)
      close(_fd);
  }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd &&other) noexcept : _fd(other._fd) { other._fd = -1; }

  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
      if (_fd >= 0)
        close(_fd);
      _fd = other._fd;
      other._fd = -1;
    }
    return *this;
  }

  int get() const noexcept { return _fd; }

  int release() noexcept {
    int fd = _fd;
    _fd = -1;
    return fd;
  }

private:
  int _fd;
};
