#pragma once

#include "ServerConfig.hpp"

#include <vector>

namespace webserv {

class Validator {
public:
  explicit Validator(const std::vector<ServerConfig> &servers);

  Validator(const Validator &) = delete;
  Validator &operator=(const Validator &) = delete;
  Validator(Validator &&) = delete;
  Validator &operator=(Validator &&) = delete;

  void validate() const;

private:
  void validateServer(const ServerConfig &server, std::size_t index) const;
  void validateLocation(const LocationConfig &location,
                        const ServerConfig &server) const;

  const std::vector<ServerConfig> &_servers;
};

} // namespace webserv