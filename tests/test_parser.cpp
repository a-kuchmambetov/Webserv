#include "Parser.hpp"
#include "ServerConfig.hpp"
#include "Tokenizer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<webserv::ServerConfig> parseString(const std::string &input) {
  webserv::Tokenizer tkner(input);
  webserv::Parser parser(tkner.getTokens());

  return parser.parse();
}

void expectRuntimeErrorContaining(const std::string &input,
                                  const std::string &message) {
  EXPECT_THROW(
      {
        try {
          (void)parseString(input);
        } catch (const std::runtime_error &e) {
          EXPECT_NE(std::string(e.what()).find(message), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

} // namespace

// A parser fed zero tokens has nothing to do and must return cleanly with no
// servers.
TEST(ParserTest, EmptyInput) {
  std::vector<webserv::Token> tokens;
  webserv::Parser parser(tokens);
  std::vector<webserv::ServerConfig> servers;

  EXPECT_NO_THROW(servers = parser.parse());
  EXPECT_TRUE(servers.empty());
}
// Smoke test that the simplest legal server block parses without throwing and
// captures the listen directive.
TEST(ParserTest, MinimalValidServer) {
  std::string input = "server { listen 8080; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].listenEndpoints().size(), 1u);
  EXPECT_EQ(servers[0].listenEndpoints()[0].host, "0.0.0.0");
  EXPECT_EQ(servers[0].listenEndpoints()[0].port, 8080);
  EXPECT_TRUE(servers[0].locations().empty());
}
// The parser must accept a server block with no directives. Validation of
// required listen directives belongs to the validator.
TEST(ParserTest, EmptyServerBlock) {
  std::string input = "server { }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_TRUE(servers[0].listenEndpoints().empty());
  EXPECT_TRUE(servers[0].serverNames().empty());
  EXPECT_TRUE(servers[0].locations().empty());
}
// The parser must produce one ServerConfig per top-level server block and
// preserve their order.
TEST(ParserTest, MultipleServers) {
  std::string input = "server { listen 8080; }"
                      "server { listen 8081; }"
                      "server { listen 8082; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 3u);
  ASSERT_EQ(servers[0].listenEndpoints().size(), 1u);
  ASSERT_EQ(servers[1].listenEndpoints().size(), 1u);
  ASSERT_EQ(servers[2].listenEndpoints().size(), 1u);
  EXPECT_EQ(servers[0].listenEndpoints()[0].port, 8080);
  EXPECT_EQ(servers[1].listenEndpoints()[0].port, 8081);
  EXPECT_EQ(servers[2].listenEndpoints()[0].port, 8082);
}
// Anything other than server at the top level must be rejected immediately.
TEST(ParserTest, UnknownTopLevelDirective) {
  std::string input = "http { listen 8080; }";

  expectRuntimeErrorContaining(input, "unexpected top-level token");
}
// After the server keyword the parser must see {.
TEST(ParserTest, ServerBlockMissingOpeningBrace) {
  std::string input = "server listen 8080; }";

  expectRuntimeErrorContaining(input, "expected '{'");
}
// If the token stream ends while inside a server block, peek or advance throws
// the end-of-config error.
TEST(ParserTest, ServerBlockMissingClosingBrace) {
  std::string input = "server { listen 8080;";

  expectRuntimeErrorContaining(input, "unexpected end of config");
}
// Every directive must terminate with a semicolon.
TEST(ParserTest, MissingSemicolonAfterDirective) {
  std::string input = "server { listen 8080 server_name example.com; }";

  expectRuntimeErrorContaining(input, "expected ';'");
}
// The parser must reject any directive name it does not explicitly handle in a
// server block.
TEST(ParserTest, UnknownServerLevelDirective) {
  std::string input = "server { foo bar; }";

  expectRuntimeErrorContaining(input, "unknown server directive: foo");
}
// Location blocks have their own directive set; unknown names must throw.
TEST(ParserTest, UnknownLocationLevelDirective) {
  std::string input = "server { listen 8080; location / { foo bar; } }";

  expectRuntimeErrorContaining(input, "unknown location directive: foo");
}
// The parser splits a host:port listen value on its rightmost colon.
TEST(ParserTest, ListenWithHostPort) {
  std::string input = "server { listen 127.0.0.1:8080; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].listenEndpoints().size(), 1u);
  EXPECT_EQ(servers[0].listenEndpoints()[0].host, "127.0.0.1");
  EXPECT_EQ(servers[0].listenEndpoints()[0].port, 8080);
}
// A bare numeric listen value defaults the host to the IPv4 wildcard.
TEST(ParserTest, ListenWithPortOnly) {
  std::string input = "server { listen 8080; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].listenEndpoints().size(), 1u);
  EXPECT_EQ(servers[0].listenEndpoints()[0].host, "0.0.0.0");
  EXPECT_EQ(servers[0].listenEndpoints()[0].port, 8080);
}
// Port 0 is reserved and the parser explicitly rejects it.
TEST(ParserTest, ListenWithPortZero) {
  std::string input = "server { listen 0; }";

  expectRuntimeErrorContaining(input, "invalid port in listen endpoint");
}
// TCP ports cannot exceed 65535.
TEST(ParserTest, ListenWithPortAboveRange) {
  std::string input = "server { listen 70000; }";

  expectRuntimeErrorContaining(input, "invalid port in listen endpoint");
}
// Non-numeric host:port values bubble up the exception from std::stoul.
TEST(ParserTest, ListenWithNonNumericPort) {
  std::string input = "server { listen 127.0.0.1:abc; }";

  EXPECT_THROW((void)parseString(input), std::exception);
}
// A bare value that is neither host:port nor an all-digits port is rejected.
TEST(ParserTest, ListenWithNoColonAndNotAllDigits) {
  std::string input = "server { listen localhost; }";

  expectRuntimeErrorContaining(input, "invalid listen endpoint");
}
// listen requires exactly one word; a semicolon immediately after it is an
// expectWord error.
TEST(ParserTest, ListenMissingValue) {
  std::string input = "server { listen ; }";

  expectRuntimeErrorContaining(input, "expected word token");
}
// Each listen directive appends to the server endpoint list.
TEST(ParserTest, MultipleListenEndpointsAccumulate) {
  std::string input = "server {"
                      "listen 127.0.0.1:8080;"
                      "listen 0.0.0.0:8081;"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].listenEndpoints().size(), 2u);
  EXPECT_EQ(servers[0].listenEndpoints()[0].host, "127.0.0.1");
  EXPECT_EQ(servers[0].listenEndpoints()[0].port, 8080);
  EXPECT_EQ(servers[0].listenEndpoints()[1].host, "0.0.0.0");
  EXPECT_EQ(servers[0].listenEndpoints()[1].port, 8081);
}
// server_name accepts an unbounded run of words up to the next semicolon.
TEST(ParserTest, ServerNameWithMultipleNames) {
  std::string input = "server {"
                      "listen 8080;"
                      "server_name a.example b.example c.example;"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].serverNames().size(), 3u);
  EXPECT_EQ(servers[0].serverNames()[0], "a.example");
  EXPECT_EQ(servers[0].serverNames()[1], "b.example");
  EXPECT_EQ(servers[0].serverNames()[2], "c.example");
}
// server_name ; reads zero word tokens and stores an empty vector.
TEST(ParserTest, ServerNameWithNoNames) {
  std::string input = "server { listen 8080; server_name ; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_TRUE(servers[0].serverNames().empty());
}
// The common error_page form is one status code followed by one path.
TEST(ParserTest, ErrorPageSingleCodeAndPath) {
  std::string input = "server {"
                      "listen 8080;"
                      "error_page 404 ./www/errors/404.html;"
                      "}";

  const auto servers = parseString(input);
  const auto page = servers[0].errorPageFor(404);

  ASSERT_TRUE(page.has_value());
  EXPECT_EQ(page->string(), "./www/errors/404.html");
}
// All leading numeric words are status codes; the last word is the shared path.
TEST(ParserTest, ErrorPageMultipleCodesSharePath) {
  std::string input = "server {"
                      "listen 8080;"
                      "error_page 404 500 ./www/errors/generic.html;"
                      "}";

  const auto servers = parseString(input);
  const auto p404 = servers[0].errorPageFor(404);
  const auto p500 = servers[0].errorPageFor(500);

  ASSERT_TRUE(p404.has_value());
  ASSERT_TRUE(p500.has_value());
  EXPECT_EQ(p404->string(), "./www/errors/generic.html");
  EXPECT_EQ(p500->string(), "./www/errors/generic.html");
}
// error_page needs at least one code and one path.
TEST(ParserTest, ErrorPageWithSingleWord) {
  std::string input = "server { listen 8080; error_page ./oops.html; }";

  expectRuntimeErrorContaining(input, "error_page requires");
}
// Non-numeric error_page codes bubble up the exception from std::stoi.
TEST(ParserTest, ErrorPageWithNonNumericCode) {
  std::string input = "server { listen 8080; error_page abc ./oops.html; }";

  EXPECT_THROW((void)parseString(input), std::exception);
}
// A bare integer client_max_body_size is treated as bytes.
TEST(ParserTest, ClientMaxBodySizeInRawBytes) {
  std::string input = "server { listen 8080; client_max_body_size 1024; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_EQ(servers[0].clientMaxBodySize(), 1024u);
}
// A K suffix multiplies client_max_body_size by 1024.
TEST(ParserTest, ClientMaxBodySizeWithKSuffix) {
  std::string input = "server { listen 8080; client_max_body_size 10K; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_EQ(servers[0].clientMaxBodySize(), 10u * 1024u);
}
// An M suffix multiplies client_max_body_size by 1048576.
TEST(ParserTest, ClientMaxBodySizeWithMSuffix) {
  std::string input = "server { listen 8080; client_max_body_size 10M; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_EQ(servers[0].clientMaxBodySize(),
            static_cast<std::size_t>(10) * 1024 * 1024);
}
// A G suffix multiplies client_max_body_size by 1073741824.
TEST(ParserTest, ClientMaxBodySizeWithGSuffix) {
  std::string input = "server { listen 8080; client_max_body_size 1G; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_EQ(servers[0].clientMaxBodySize(),
            static_cast<std::size_t>(1) * 1024 * 1024 * 1024);
}
// Oversized numeric values throw from the size parser.
TEST(ParserTest, ClientMaxBodySizeWithOverflow) {
  std::string input =
      "server { listen 8080; client_max_body_size 99999999999999999999G; }";

  EXPECT_THROW((void)parseString(input), std::exception);
}
// An empty size word is malformed and rejected by parseSize.
TEST(ParserTest, ClientMaxBodySizeWithEmptyValue) {
  std::vector<webserv::Token> tokens = {
      {webserv::Word, "server"},
      {webserv::OpenBracket, "{"},
      {webserv::Word, "client_max_body_size"},
      {webserv::Word, ""},
      {webserv::Semicol, ";"},
      {webserv::CloseBracket, "}"},
  };
  webserv::Parser parser(tokens);

  EXPECT_THROW((void)parser.parse(), std::runtime_error);
}
// client_header_buffer_size uses the same size suffix parser.
TEST(ParserTest, ClientHeaderBufferSizeWithKSuffix) {
  std::string input =
      "server { listen 8080; client_header_buffer_size 16K; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_EQ(servers[0].clientMaxHeaderSize(), 16u * 1024u);
}
// Server root stores the single path word verbatim.
TEST(ParserTest, RootDirectiveCaptured) {
  std::string input = "server { listen 8080; root ./www/static; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  EXPECT_EQ(servers[0].root().string(), "./www/static");
}
// index reads zero or more words up to the semicolon.
TEST(ParserTest, IndexWithMultipleFiles) {
  std::string input =
      "server { listen 8080; index index.html home.html default.html; }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].indexFiles().size(), 3u);
  EXPECT_EQ(servers[0].indexFiles()[0], "index.html");
  EXPECT_EQ(servers[0].indexFiles()[1], "home.html");
  EXPECT_EQ(servers[0].indexFiles()[2], "default.html");
}
// The first word after location is the path prefix.
TEST(ParserTest, LocationWithExplicitPrefix) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /api { methods GET; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  EXPECT_EQ(servers[0].locations()[0].pathPrefix(), "/api");
}
// Empty location blocks keep LocationConfig defaults.
TEST(ParserTest, EmptyLocationBlock) {
  std::string input = "server { listen 8080; location / { } }";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  EXPECT_EQ(loc.pathPrefix(), "/");
  EXPECT_EQ(loc.allowedMethods().size(), 3u);
  EXPECT_TRUE(loc.allowedMethods().count(webserv::HttpMethod::Get) > 0);
  EXPECT_TRUE(loc.allowedMethods().count(webserv::HttpMethod::Post) > 0);
  EXPECT_TRUE(loc.allowedMethods().count(webserv::HttpMethod::Delete) > 0);
}
// The methods directive clears default methods before adding explicit values.
TEST(ParserTest, MethodsDirectiveReplacesDefaults) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods GET; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  ASSERT_EQ(loc.allowedMethods().size(), 1u);
  EXPECT_TRUE(loc.allowedMethods().count(webserv::HttpMethod::Get) > 0);
  EXPECT_FALSE(loc.allowedMethods().count(webserv::HttpMethod::Post) > 0);
  EXPECT_FALSE(loc.allowedMethods().count(webserv::HttpMethod::Delete) > 0);
}
// methods accepts multiple words and adds each to the allowed set.
TEST(ParserTest, MethodsDirectiveWithMultipleValues) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods GET POST; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  EXPECT_EQ(loc.allowedMethods().size(), 2u);
  EXPECT_TRUE(loc.allowedMethods().count(webserv::HttpMethod::Get) > 0);
  EXPECT_TRUE(loc.allowedMethods().count(webserv::HttpMethod::Post) > 0);
}
// Unknown HTTP method names are rejected.
TEST(ParserTest, MethodsWithUnknownMethod) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods PATCH; }"
                      "}";

  expectRuntimeErrorContaining(input, "unknown HTTP method: PATCH");
}
// methods ; clears the default method set and adds nothing.
TEST(ParserTest, MethodsWithEmptyList) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods ; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  EXPECT_TRUE(servers[0].locations()[0].allowedMethods().empty());
}
// The literal value on enables autoindex.
TEST(ParserTest, AutoindexOn) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods GET; autoindex on; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  EXPECT_TRUE(servers[0].locations()[0].autoindexEnabled());
}
// The literal value off disables autoindex.
TEST(ParserTest, AutoindexOff) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods GET; autoindex off; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  EXPECT_FALSE(servers[0].locations()[0].autoindexEnabled());
}
// Anything other than on or off is rejected.
TEST(ParserTest, AutoindexWithInvalidValue) {
  std::string input = "server {"
                      "listen 8080;"
                      "location / { methods GET; autoindex maybe; }"
                      "}";

  expectRuntimeErrorContaining(input, "autoindex value must be");
}
// return reads exactly two words: status code and target.
TEST(ParserTest, ReturnDirective) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /old { methods GET; return 301 /new; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  ASSERT_TRUE(loc.redirect().has_value());
  EXPECT_EQ(loc.redirect()->statusCode, 301);
  EXPECT_EQ(loc.redirect()->target, "/new");
}
// Non-numeric return codes bubble up the exception from std::stoi.
TEST(ParserTest, ReturnWithNonNumericCode) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /old { methods GET; return abc /new; }"
                      "}";

  EXPECT_THROW((void)parseString(input), std::exception);
}
// upload_store captures the single path word into the upload directory option.
TEST(ParserTest, UploadStoreDirective) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /upload {"
                      "methods POST;"
                      "upload_store ./var/uploads;"
                      "}"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  ASSERT_TRUE(loc.uploadDirectory().has_value());
  EXPECT_EQ(loc.uploadDirectory()->string(), "./var/uploads");
}
// cgi <ext> <executable> registers a single handler.
TEST(ParserTest, CgiDirectiveSingleHandler) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /cgi-bin {"
                      "methods GET POST;"
                      "cgi .py /usr/bin/python3;"
                      "}"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  ASSERT_EQ(loc.cgiHandlers().size(), 1u);
  const auto handler = loc.cgiHandlerFor(".py");
  ASSERT_TRUE(handler.has_value());
  EXPECT_EQ(handler->string(), "/usr/bin/python3");
}
// Multiple cgi directives accumulate into the handler map keyed by extension.
TEST(ParserTest, CgiDirectiveMultipleHandlers) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /scripts {"
                      "methods GET POST;"
                      "cgi .py /usr/bin/python3;"
                      "cgi .php /usr/bin/php-cgi;"
                      "}"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  EXPECT_EQ(loc.cgiHandlers().size(), 2u);
  EXPECT_TRUE(loc.cgiHandlerFor(".py").has_value());
  EXPECT_TRUE(loc.cgiHandlerFor(".php").has_value());
}
// cgi requires two words; a missing executable hits expectWord.
TEST(ParserTest, CgiDirectiveWithMissingExecutable) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /cgi-bin { methods GET; cgi .py; }"
                      "}";

  expectRuntimeErrorContaining(input, "expected word token");
}
// Location-level client_max_body_size is stored as an optional override.
TEST(ParserTest, LocationLevelClientMaxBodySize) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /upload {"
                      "methods POST;"
                      "client_max_body_size 5M;"
                      "}"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  const auto &loc = servers[0].locations()[0];
  ASSERT_TRUE(loc.clientMaxBodySize().has_value());
  EXPECT_EQ(*loc.clientMaxBodySize(),
            static_cast<std::size_t>(5) * 1024 * 1024);
}
// root inside a location overrides only that location's root.
TEST(ParserTest, LocationRootOverride) {
  std::string input = "server {"
                      "listen 8080;"
                      "location /api { methods GET; root ./www/api; }"
                      "}";

  const auto servers = parseString(input);

  ASSERT_EQ(servers.size(), 1u);
  ASSERT_EQ(servers[0].locations().size(), 1u);
  EXPECT_EQ(servers[0].root().string(), "");
  EXPECT_EQ(servers[0].locations()[0].root().string(), "./www/api");
}
// Non-word tokens at top level are skipped silently.
TEST(ParserTest, StrayClosingBraceAtTopLevel) {
  std::vector<webserv::Token> tokens = {
      {webserv::CloseBracket, "}"},
  };
  webserv::Parser parser(tokens);
  std::vector<webserv::ServerConfig> servers;

  EXPECT_NO_THROW(servers = parser.parse());
  EXPECT_TRUE(servers.empty());
}
// A stray semicolon at top level is also skipped.
TEST(ParserTest, StraySemicolonAtTopLevel) {
  std::vector<webserv::Token> tokens = {
      {webserv::Semicol, ";"},
  };
  webserv::Parser parser(tokens);

  const auto servers = parser.parse();

  EXPECT_TRUE(servers.empty());
}
// A realistic multi-server config parses without throwing and produces the
// expected high-level shape.
TEST(ParserTest, FullRealisticConfig) {
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
  std::vector<webserv::ServerConfig> servers;

  EXPECT_NO_THROW(servers = parseString(input));
  ASSERT_EQ(servers.size(), 3u);
  ASSERT_EQ(servers[0].listenEndpoints().size(), 1u);
  ASSERT_EQ(servers[1].listenEndpoints().size(), 1u);
  ASSERT_EQ(servers[2].listenEndpoints().size(), 1u);
  EXPECT_EQ(servers[0].listenEndpoints()[0].host, "127.0.0.1");
  EXPECT_EQ(servers[0].listenEndpoints()[0].port, 8080);
  EXPECT_EQ(servers[1].listenEndpoints()[0].port, 8081);
  EXPECT_EQ(servers[2].listenEndpoints()[0].port, 8082);
  EXPECT_GT(servers[0].locations().size(), 0u);
  EXPECT_GT(servers[2].locations().size(), 0u);
}
// Token streams with and without spaces around delimiters parse equivalently.
TEST(ParserTest, TokensWithoutSpaceFixtureParsesIdentically) {
  std::string noSpace =
      "server {listen 127.0.0.1:8080;server_name static.local;"
      "root ./www/static;index index.html;client_max_body_size 10M;"
      "location / {methods GET;index index.html;autoindex off;}}";
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

  const auto noSpaceServers = parseString(noSpace);
  const auto spacedServers = parseString(spaced);

  ASSERT_EQ(noSpaceServers.size(), spacedServers.size());
  for (std::size_t i = 0; i < noSpaceServers.size(); ++i) {
    EXPECT_EQ(noSpaceServers[i].listenEndpoints().size(),
              spacedServers[i].listenEndpoints().size());
    EXPECT_EQ(noSpaceServers[i].locations().size(),
              spacedServers[i].locations().size());
    EXPECT_EQ(noSpaceServers[i].clientMaxBodySize(),
              spacedServers[i].clientMaxBodySize());
  }
}
// The missing-semicolon fixture shape omits a semicolon after a listen
// directive and must be rejected.
TEST(ParserTest, MissingSemicolonFixtureIsRejected) {
  std::string input = "server {"
                      "listen 127.0.0.1:8080"
                      "listen 0.0.0.0:8080;"
                      "server_name static.local;"
                      "root ./www/static;"
                      "index index.html;"
                      "client_max_body_size 10M;"
                      "location / { methods GET; index index.html; "
                      "autoindex off; }"
                      "}";

  expectRuntimeErrorContaining(input, "expected ';'");
}
