#include "Parser.hpp"
#include "ServerConfig.hpp"
#include "Tokenizer.hpp"
#include "Validator.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

webserv::LocationConfig makeValidLocation(const std::string &prefix = "/") {
  webserv::LocationConfig loc;
  loc.setPathPrefix(prefix);
  loc.allowMethod(webserv::HttpMethod::Get);
  return loc;
}

webserv::ServerConfig makeValidServer() {
  webserv::ServerConfig s;
  s.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  s.addLocation(makeValidLocation());
  return s;
}

std::vector<webserv::ServerConfig> parseString(const std::string &input) {
  webserv::Tokenizer tkner(input);
  webserv::Parser parser(tkner.getTokens());

  return parser.parse();
}

void expectValidationRuntimeErrorContaining(
    const std::vector<webserv::ServerConfig> &servers,
    const std::string &message) {
  webserv::Validator validator(servers);

  EXPECT_THROW(
      {
        try {
          validator.validate();
        } catch (const std::runtime_error &e) {
          EXPECT_NE(std::string(e.what()).find(message), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

void expectValidationPasses(const std::vector<webserv::ServerConfig> &servers) {
  webserv::Validator validator(servers);

  EXPECT_NO_THROW(validator.validate());
}

void expectConfigValidates(const std::string &input) {
  const auto servers = parseString(input);
  webserv::Validator validator(servers);

  EXPECT_NO_THROW(validator.validate());
}

} // namespace

// With zero servers there is nothing to validate; the validator must accept
// this without throwing.
TEST(ValidatorTest, EmptyServerList) {
  std::vector<webserv::ServerConfig> servers;
  webserv::Validator validator(servers);

  EXPECT_NO_THROW(validator.validate());
}
// A server with one listen endpoint and one well-formed location is the
// canonical valid input.
TEST(ValidatorTest, MinimalValidServer) {
  std::vector<webserv::ServerConfig> servers = {makeValidServer()};
  webserv::Validator validator(servers);

  EXPECT_NO_THROW(validator.validate());
}
// Every server must declare at least one listen endpoint.
TEST(ValidatorTest, ServerWithNoListenDirective) {
  webserv::ServerConfig server;
  server.addLocation(makeValidLocation());
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "no listen directive defined");
}
// The same host and port pair listed twice for one server is invalid.
TEST(ValidatorTest, DuplicateListenEndpoint) {
  webserv::ServerConfig server = makeValidServer();
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "duplicate listen endpoint");
}
// A wildcard listen endpoint after a specific host on the same port is
// rejected by the current implementation.
TEST(ValidatorTest, WildcardAfterSpecificHostOnSamePort) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  server.addListenEndpoint(webserv::ListenEndpoint{"0.0.0.0", 8080});
  server.addLocation(makeValidLocation());
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "wild card address");
}
// The wildcard-overlap check is asymmetric: wildcard before a specific host on
// the same port currently passes.
TEST(ValidatorTest, WildcardBeforeSpecificHostOnSamePort) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"0.0.0.0", 8080});
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  server.addLocation(makeValidLocation());
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// The wildcard check is per-port, so different ports do not collide.
TEST(ValidatorTest, DifferentPortsDoNotCollide) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"0.0.0.0", 8080});
  server.addListenEndpoint(webserv::ListenEndpoint{"0.0.0.0", 8081});
  server.addLocation(makeValidLocation());
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// Error page status codes below 400 are not error codes.
TEST(ValidatorTest, ErrorPageCodeBelow400) {
  webserv::ServerConfig server = makeValidServer();
  server.setErrorPage(200, std::filesystem::path{"./oops.html"});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "out of range 400-599");
}
// Error page status codes above 599 are rejected.
TEST(ValidatorTest, ErrorPageCodeAbove599) {
  webserv::ServerConfig server = makeValidServer();
  server.setErrorPage(600, std::filesystem::path{"./oops.html"});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "out of range 400-599");
}
// Code 400 is the inclusive lower bound for error pages.
TEST(ValidatorTest, ErrorPageCodeAtLowerBound400) {
  webserv::ServerConfig server = makeValidServer();
  server.setErrorPage(400, std::filesystem::path{"./400.html"});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// Code 599 is the inclusive upper bound for error pages.
TEST(ValidatorTest, ErrorPageCodeAtUpperBound599) {
  webserv::ServerConfig server = makeValidServer();
  server.setErrorPage(599, std::filesystem::path{"./599.html"});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// Empty server-level index entries are meaningless.
TEST(ValidatorTest, EmptyIndexFileEntryAtServerLevel) {
  webserv::ServerConfig server = makeValidServer();
  server.setIndexFiles({"index.html", ""});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "index file entry must not be empty");
}
// Server-level index entries must be filenames, not paths.
TEST(ValidatorTest, IndexFileEntryContainingSlashAtServerLevel) {
  webserv::ServerConfig server = makeValidServer();
  server.setIndexFiles({"sub/index.html"});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "must be a filename, not a path");
}
// Two locations with the same prefix make routing ambiguous.
TEST(ValidatorTest, DuplicateLocationPrefixInSameServer) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  server.addLocation(makeValidLocation("/api"));
  server.addLocation(makeValidLocation("/api"));
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "duplicate location prefix");
}
// Every location needs a non-empty prefix.
TEST(ValidatorTest, EmptyLocationPathPrefix) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  server.addLocation(makeValidLocation(""));
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "must not be empty");
}
// Request paths begin with /, so location prefixes must too.
TEST(ValidatorTest, LocationPathPrefixNotStartingWithSlash) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  server.addLocation(makeValidLocation("api"));
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "must start with '/'");
}
// A location with no allowed methods cannot serve any request.
TEST(ValidatorTest, LocationWithEmptyAllowedMethods) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc;
  loc.setPathPrefix("/");
  loc.clearAllowedMethods();
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "allowed methods must not be empty");
}
// Redirects must use 3xx status codes.
TEST(ValidatorTest, RedirectWithNon3xxStatusCodeBelow) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/redir");
  loc.setRedirect(webserv::RedirectRule{200, "/elsewhere"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "is not a 3xx code");
}
// Codes >= 400 are errors, not redirects.
TEST(ValidatorTest, RedirectWithNon3xxStatusCodeAbove) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/redir");
  loc.setRedirect(webserv::RedirectRule{404, "/elsewhere"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "is not a 3xx code");
}
// A redirect must point somewhere.
TEST(ValidatorTest, RedirectWithEmptyTarget) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/redir");
  loc.setRedirect(webserv::RedirectRule{301, ""});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "redirect target must not be empty");
}
// A location is either a redirect target or a CGI endpoint, not both.
TEST(ValidatorTest, RedirectTogetherWithCgiHandler) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/cgi");
  loc.setRedirect(webserv::RedirectRule{301, "/elsewhere"});
  loc.setCgiHandler(".py", std::filesystem::path{"/usr/bin/python3"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "redirect and cgi handlers");
}
// A redirect endpoint cannot also accept uploads.
TEST(ValidatorTest, RedirectTogetherWithUploadDirectory) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/upload");
  loc.setRedirect(webserv::RedirectRule{301, "/elsewhere"});
  loc.setUploadDirectory(std::filesystem::path{"./var/uploads"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "redirect and upload_store");
}
// A simple 301 redirect with no conflicting directives is valid.
TEST(ValidatorTest, RedirectWithValid3xxCodePasses) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/old");
  loc.setRedirect(webserv::RedirectRule{301, "/new"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// Redirect code 300 is the inclusive lower bound.
TEST(ValidatorTest, RedirectAtLowerBound300) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/multi");
  loc.setRedirect(webserv::RedirectRule{300, "/options"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// Redirect code 399 is the inclusive upper bound.
TEST(ValidatorTest, RedirectAtUpperBound399) {
  webserv::ServerConfig server = makeValidServer();
  webserv::LocationConfig loc = makeValidLocation("/edge");
  loc.setRedirect(webserv::RedirectRule{399, "/somewhere"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationPasses(servers);
}
// Location-level index entries also reject empty filenames.
TEST(ValidatorTest, EmptyIndexFileEntryAtLocationLevel) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc = makeValidLocation();
  loc.setIndexFiles({"index.html", ""});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "index file entry must not be empty");
}
// Location-level index entries must also be filenames, not paths.
TEST(ValidatorTest, LocationIndexEntryContainingSlash) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc = makeValidLocation();
  loc.setIndexFiles({"a/b.html"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers,
                                         "must be a filename, not a path");
}
// CGI extensions must include the leading dot.
TEST(ValidatorTest, CgiExtensionMissingLeadingDot) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc = makeValidLocation("/cgi");
  loc.setCgiHandler("py", std::filesystem::path{"/usr/bin/python3"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "must start with '.'");
}
// CGI handlers need a non-empty executable path.
TEST(ValidatorTest, CgiHandlerWithEmptyExecutable) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc = makeValidLocation("/cgi");
  loc.setCgiHandler(".py", std::filesystem::path{""});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "empty executable path");
}
// An empty CGI extension also fails the leading-dot check.
TEST(ValidatorTest, CgiExtensionWithEmptyString) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc = makeValidLocation("/cgi");
  loc.setCgiHandler("", std::filesystem::path{"/usr/bin/python3"});
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "must start with '.'");
}
// When a server has no server_name, the validator still labels it by index.
TEST(ValidatorTest, ErrorContextIncludesServerIndexWhenNoName) {
  webserv::ServerConfig server;
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "server 0 \"\"");
}
// When server names are set, the first one appears in the error context.
TEST(ValidatorTest, ErrorContextIncludesFirstServerName) {
  webserv::ServerConfig server;
  server.setServerNames({"static.local", "extra"});
  std::vector<webserv::ServerConfig> servers = {server};

  expectValidationRuntimeErrorContaining(servers, "server 0 \"static.local\"");
}
// Location-level errors include the first listen endpoint and the location
// prefix.
TEST(ValidatorTest, LocationContextUsesFirstListenEndpoint) {
  webserv::ServerConfig server;
  server.addListenEndpoint(webserv::ListenEndpoint{"127.0.0.1", 8080});
  webserv::LocationConfig loc;
  loc.setPathPrefix("/foo");
  loc.clearAllowedMethods();
  server.addLocation(loc);
  std::vector<webserv::ServerConfig> servers = {server};
  webserv::Validator validator(servers);

  EXPECT_THROW(
      {
        try {
          validator.validate();
        } catch (const std::runtime_error &e) {
          const std::string message = e.what();
          EXPECT_NE(message.find("server (127.0.0.1:8080)"), std::string::npos);
          EXPECT_NE(message.find("location '/foo'"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}
// The validator visits every server in order and reports the correct index for
// later failures.
TEST(ValidatorTest, ValidatorIteratesAllServersLaterOnesStillValidate) {
  std::vector<webserv::ServerConfig> servers;
  servers.push_back(makeValidServer());
  servers.push_back(webserv::ServerConfig{});

  webserv::Validator validator(servers);
  EXPECT_THROW(
      {
        try {
          validator.validate();
        } catch (const std::runtime_error &e) {
          const std::string message = e.what();
          EXPECT_NE(message.find("server 1"), std::string::npos);
          EXPECT_NE(message.find("no listen directive defined"),
                    std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}
// Duplicate listen checks are per-server, not across servers.
TEST(ValidatorTest, TwoServersWithSameListenEndpointValidateIndependently) {
  std::vector<webserv::ServerConfig> servers = {
      makeValidServer(),
      makeValidServer(),
  };

  expectValidationPasses(servers);
}
// End-to-end smoke test for a realistic multi-server config.
TEST(ValidatorTest, FullValidBigConfigPasses) {
  std::string input = R"(
server {
    listen 127.0.0.1:8080;
    server_name static.local;
    root ./www/static;
    index index.html index2.html index3.html;
    client_max_body_size 10M;
    error_page 400 ./www/errors/400.html;
    error_page 403 ./www/errors/403.html;
    error_page 404 ./www/errors/404.html;
    error_page 405 ./www/errors/405.html;
    error_page 413 ./www/errors/413.html;
    error_page 500 ./www/errors/500.html;
    location / { methods GET; index index.html; autoindex off; }
    location /assets { methods GET; root ./www/static/assets; autoindex on; }
    location /docs {
        methods GET;
        root ./www/static/docs;
        index index.html;
        autoindex on;
    }
    location /old-home { methods GET; return 301 /; }
    location /delete-test {
        methods DELETE;
        root ./www/static/delete-test;
        autoindex off;
    }
}
server {
    listen 127.0.0.1:8081;
    server_name upload.local;
    root ./www/upload;
    index index.html;
    client_max_body_size 50M;
    error_page 400 ./www/errors/400.html;
    error_page 403 ./www/errors/403.html;
    error_page 404 ./www/errors/404.html;
    error_page 405 ./www/errors/405.html;
    error_page 413 ./www/errors/413.html;
    error_page 500 ./www/errors/500.html;
    location / { methods GET; index index.html; autoindex off; }
    location /upload {
        methods GET POST DELETE;
        root ./www/upload;
        index upload.html;
        autoindex off;
        upload_store ./var/uploads;
    }
    location /files { methods GET DELETE; root ./var/uploads; autoindex on; }
    location /images {
        methods GET POST;
        root ./www/upload/images;
        autoindex on;
        upload_store ./var/uploads/images;
    }
}
server {
    listen 127.0.0.1:8082;
    server_name cgi.local;
    root ./www/cgi;
    index index.html;
    client_max_body_size 20M;
    error_page 400 ./www/errors/400.html;
    error_page 403 ./www/errors/403.html;
    error_page 404 ./www/errors/404.html;
    error_page 405 ./www/errors/405.html;
    error_page 413 ./www/errors/413.html;
    error_page 500 ./www/errors/500.html;
    location / { methods GET; index index.html; autoindex off; }
    location /cgi-bin {
        methods GET POST;
        root ./www/cgi/cgi-bin;
        autoindex off;
        cgi .py /usr/bin/python3;
    }
    location /scripts {
        methods GET POST;
        root ./www/cgi/scripts;
        autoindex on;
        cgi .py /usr/bin/python3;
        cgi .php /usr/bin/php-cgi;
    }
    location /form {
        methods GET POST;
        root ./www/cgi/form;
        index form.html;
        autoindex off;
    }
    location /legacy-cgi { methods GET; return 302 /cgi-bin/hello.py; }
})";

  expectConfigValidates(input);
}
// Smaller fully-valid config exercising the same tokenizer-parser-validator
// pipeline.
TEST(ValidatorTest, FullValidFullConfigPasses) {
  std::string input = R"(
server {
    listen 127.0.0.1:8080;
    server_name static.local;
    root ./www/static;
    index index.html;
    client_max_body_size 10M;
    client_header_buffer_size 16K;
    error_page 404 ./www/errors/404.html;
    location / {
        methods GET POST DELETE;
        root ./www/static;
        index index.html;
        autoindex off;
    }
    location /upload {
        methods POST;
        upload_store ./var/uploads;
        client_max_body_size 5M;
    }
    location /cgi-bin {
        methods GET POST;
        cgi .py /usr/bin/python3;
    }
    location /old {
        methods GET;
        return 301 /new;
    }
})";

  expectConfigValidates(input);
}
// Whitespace-insensitive tokenization must not change validation outcomes.
TEST(ValidatorTest, ValidSpacedAndValidNoSpaceConfigsYieldIdenticalValidation) {
  std::string spaced = "server {"
                       "listen 127.0.0.1:8080;"
                       "server_name static.local;"
                       "root ./www/static;"
                       "index index.html;"
                       "client_max_body_size 10M;"
                       "location / {"
                       "methods GET;"
                       "index index.html;"
                       "autoindex off;"
                       "}"
                       "}";
  std::string noSpace =
      "server {listen 127.0.0.1:8080;server_name static.local;"
      "root ./www/static;index index.html;client_max_body_size 10M;"
      "location / {methods GET;index index.html;autoindex off;}}";

  expectConfigValidates(spaced);
  expectConfigValidates(noSpace);
}
