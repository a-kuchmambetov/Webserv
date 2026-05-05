#include "Validator.hpp"

#include <set>
#include <stdexcept>
#include <string>

namespace webserv {

Validator::Validator(const std::vector<ServerConfig> &servers)
    : _servers(servers) {}

void Validator::validate() const {
  for (std::size_t i = 0; i < _servers.size(); ++i)
    validateServer(_servers[i], i);
}

void Validator::validateServer(const ServerConfig &server,
                               std::size_t index) const {
  std::string server_name = "";
  if (server.serverNames().size() > 0)
    server_name = server.serverNames().at(0);
  const std::string ctx =
      "server " + std::to_string(index) + " \"" + server_name + "\"";

  if (server.listenEndpoints().empty())
    throw std::runtime_error(ctx + ": no listen directive defined");

  {
    std::set<std::pair<std::string, std::uint16_t>> seen;
    for (const ListenEndpoint &ep : server.listenEndpoints()) {
      if (!seen.insert({ep.host, ep.port}).second)
        throw std::runtime_error(ctx + ": duplicate listen endpoint " +
                                 ep.host + ":" + std::to_string(ep.port));
    }
  }

  {
    std::set<std::uint16_t> seen;
    for (const ListenEndpoint &ep : server.listenEndpoints()) {
      if (!seen.insert(ep.port).second && ep.host == "0.0.0.0")
        throw std::runtime_error(ctx +
                                 ": wild card address for already used port " +
                                 ep.host + ":" + std::to_string(ep.port));
    }
  }

  for (const auto &[code, path] : server.errorPages()) {
    if (code < 400 || code > 599)
      throw std::runtime_error(ctx + ": error_page code " +
                               std::to_string(code) +
                               " is out of range 400-599");
  }

  for (const std::string &entry : server.indexFiles()) {
    if (entry.empty())
      throw std::runtime_error(ctx + ": index file entry must not be empty");
    if (entry.find('/') != std::string::npos)
      throw std::runtime_error(ctx + ": index file entry '" + entry +
                               "' must be a filename, not a path");
  }

  {
    std::set<std::string> seenPrefixes;
    for (const LocationConfig &loc : server.locations()) {
      if (!seenPrefixes.insert(loc.pathPrefix()).second)
        throw std::runtime_error(ctx + ": duplicate location prefix '" +
                                 loc.pathPrefix() + "'");
    }
  }

  for (const LocationConfig &loc : server.locations())
    validateLocation(loc, server);
}

void Validator::validateLocation(const LocationConfig &location,
                                 const ServerConfig &server) const {
  std::string serverCtx = "server";
  if (!server.listenEndpoints().empty()) {
    const ListenEndpoint &ep = server.listenEndpoints().front();
    serverCtx += " (" + ep.host + ":" + std::to_string(ep.port) + ")";
  }
  const std::string ctx =
      serverCtx + ", location '" + location.pathPrefix() + "'";

  if (location.pathPrefix().empty())
    throw std::runtime_error(ctx + ": location path prefix must not be empty");
  if (location.pathPrefix().front() != '/')
    throw std::runtime_error(ctx +
                             ": location path prefix must start with '/'");

  if (location.allowedMethods().empty())
    throw std::runtime_error(ctx + ": allowed methods must not be empty");

  if (location.redirect().has_value()) {
    const RedirectRule &rule = *location.redirect();
    if (rule.statusCode < 300 || rule.statusCode > 399)
      throw std::runtime_error(ctx + ": redirect status code " +
                               std::to_string(rule.statusCode) +
                               " is not a 3xx code");
    if (rule.target.empty())
      throw std::runtime_error(ctx + ": redirect target must not be empty");
    if (!location.cgiHandlers().empty())
      throw std::runtime_error(
          ctx + ": redirect and cgi handlers cannot both be set");
    if (location.uploadDirectory().has_value())
      throw std::runtime_error(
          ctx + ": redirect and upload_store cannot both be set");
  }

  for (const std::string &entry : location.indexFiles()) {
    if (entry.empty())
      throw std::runtime_error(ctx + ": index file entry must not be empty");
    if (entry.find('/') != std::string::npos)
      throw std::runtime_error(ctx + ": index file entry '" + entry +
                               "' must be a filename, not a path");
  }

  for (const auto &[ext, executable] : location.cgiHandlers()) {
    if (ext.empty() || ext.front() != '.')
      throw std::runtime_error(ctx + ": CGI extension '" + ext +
                               "' must start with '.'");
    if (executable.empty())
      throw std::runtime_error(ctx + ": CGI handler for '" + ext +
                               "' has an empty executable path");
  }
}

} // namespace webserv
