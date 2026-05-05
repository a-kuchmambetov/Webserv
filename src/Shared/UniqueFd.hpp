#pragma once

#include <unistd.h>

class UniqueFd {
public:
  UniqueFd() noexcept;
  explicit UniqueFd(int fd) noexcept;
  ~UniqueFd();

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  UniqueFd(UniqueFd &&other) noexcept;
  UniqueFd &operator=(UniqueFd &&other) noexcept;

  int get() const noexcept;
  int release() noexcept;

private:
  int _fd;
};
