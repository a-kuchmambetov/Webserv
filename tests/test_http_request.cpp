#include "HttpRequest.hpp"

// This lib for writing tests
#include <gtest/gtest.h>
#include <string>

// 400 Bad Request Generic malformed request. Use when syntax is broken and no
// more specific status fits.
TEST(HttpRequestTest, BadRequest) {
  std::string raw = "TRASH GET /index.html HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Empty request line	Client sends \r\n or nothing meaningful before
// headers.
TEST(HttpRequestTest, EmptyRequestLine) {
  std::string raw = "\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid request line format	Request line is not exactly: METHOD SP
// TARGET SP VERSION CRLF.
TEST(HttpRequestTest, InvalidRequestLineFormat) {
  std::string raw = "HTTP/1.1 GET /index.html\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Missing method	Example: /index.html HTTP/1.1.
TEST(HttpRequestTest, MissingMethod) {
  std::string raw = "HTTP/1.1 /index.html\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid method token	Method contains invalid chars, spaces, control
// chars, etc.
TEST(HttpRequestTest, AdditionalSpace) {
  std::string raw = "GET  HTTP/1.1 /index.html\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Missing request target	Example: GET HTTP/1.1.
TEST(HttpRequestTest, MissingRequestTarget) {
  std::string raw = "GET HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400 Invalid request target	Empty target, invalid characters, control chars,
// invalid URI form.
TEST(HttpRequestTest, InvalidRequestTarget) {
  std::string raw = "GET ftp://example.com/file HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid percent-encoding	Example: /abc%ZZ, /abc%, /abc%2.
// Invalid because Z is not a hexadecimal digit.
TEST(HttpRequestTest, InvalidPercentEncoding) {
  std::string raw = "GET /index%ZZfile.html HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid HTTP version format	Example: HTTP/1, HTTP/abc, HTP/1.1.
TEST(HttpRequestTest, InvalidHttpVersion) {
  std::string raw = "GET /index.html HTTP/1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Header line without colon	Example: Host localhost.
TEST(HttpRequestTest, HeaderLineWithoutColon) {
  std::string raw = "GET /index.html HTTP/1.1\r\n"
                    "Host localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Empty header name	Example: : value.
TEST(HttpRequestTest, EmptyHeaderName) {
  std::string raw = "GET /index.html HTTP/1.1\r\n"
                    ": localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid header name	Header name contains spaces, control chars, or
// invalid separators.
TEST(HttpRequestTest, InvalidHeaderName) {
  std::string raw = "GET /index.html HTTP/1.1\r\n"
                    "Hos t: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid header value	Header value contains forbidden control
// characters.
TEST(HttpRequestTest, InvalidHeaderValue) {
  std::string raw = "GET /index.html HTTP/1.1\r\n"
                    "Host: localh\tost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Header continuation / folded header Old-style multiline headers. Better
// reject.
// Started from SP
TEST(HttpRequestTest, FoldedHeaderLine) {
  std::string raw = "GET /index.html HTTP/1.1\r\n"
                    " Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Headers not terminated No \r\n\r\n before timeout or max header size.
TEST(HttpRequestTest, HeaderNotTerminated) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost"
                    "Content-Length: 0\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid Content-Length Non-numeric, negative, duplicated with different
// values, overflow.
TEST(HttpRequestTest, InvalidContentLength) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: abc\r\n"
                    "\r\n"
                    "hello";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400 Invalid Transfer-Encoding syntax	Malformed value.
TEST(HttpRequestTest, InvalidTransferEncoding) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Transfer-Encoding: chunked,\r\n"
                    "\r\n"
                    "5\r\n"
                    "hello\r\n"
                    "0\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400 Transfer-Encoding + Content-Length conflict	Safer for Webserv:
// reject instead of guessing.
TEST(HttpRequestTest, ContenLengthAndTransferEncoding) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "hello\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Invalid chunk size	Chunk size is not valid hex.
TEST(HttpRequestTest, InvalidChunkSize) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "Z\r\n"
                    "hello\r\n"
                    "0\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  // req.setMaxBodySize(100000);
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Malformed chunk ending	Chunk data not followed by correct \r\n.
TEST(HttpRequestTest, MalformedChunkEnding) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "5"
                    "hello\r\n"
                    "0\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Chunked body missing final chunk	No terminating 0\r\n\r\n.
TEST(HttpRequestTest, MissingFinalChuck) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "5\r\n"
                    "hello\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 400);
}
// 400	Body shorter than Content-Length	Client closes early before full
// body arrives.
TEST(HttpRequestTest, BodyShorterThanContentLength) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: 10\r\n"
                    "\r\n"
                    "hello";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_FALSE(req.isComplete());
  EXPECT_EQ(req.errorStatus(), 0);
}
// 411	Length Required	Request needs a body, usually POST, but has no
// Content-Length and no supported Transfer-Encoding: chunked.
TEST(HttpRequestTest, LengthRequired) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 411);
}
// 413	Payload Too Large	Content-Length is bigger than
// client_max_body_size, or chunked body grows past limit.
TEST(HttpRequestTest, PayloadTooLarge) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: 10\r\n"
                    "\r\n"
                    "hello";
  webserv::HttpRequest req;
  req.setMaxBodySize(1);
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 413);
}
// 414	URI Too Long Request target is longer than your configured max URI
// length.
// NOTE: I'm not sure if we need that...
// TEST(HttpRequestTest, UriTooLong) {
//   std::string raw =
//       "POST "
//       "/loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
//       "oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
//       "oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
//       "oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
//       "oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
//       "oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo"
//       "ooooooooooooooong HTTP/1.1\r\n"
//       "Host: localhost\r\n"
//       "Content-Length: 10\r\n"
//       "\r\n"
//       "hello";
//   webserv::HttpRequest req;
//   req.append(raw);
//   EXPECT_EQ(req.errorStatus(), 414);
// }
// 415 Unsupported Media Type	Optional. Use if you decide uploads only accept
// specific content types, for example rejecting non-multipart/form-data upload
// requests.
// NOTE: I'm not sure if we need this either...
// 431	Request Header Fields Too Large	One header or total headers exceed your
// configured header limit.
TEST(HttpRequestTest, RequestHeaderTooLarge) {
  std::string raw = "PUT /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: 10\r\n"
                    "\r\n"
                    "hello";
  webserv::HttpRequest req;
  // IMPORTANT: we need to implement this
  //req.setMaxHeaderSize(1);
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 431);
}
// 501	Not Implemented	Method is syntactically valid but your server does not
// implement it at all. Example: PUT, PATCH, OPTIONS, unless you add support.
TEST(HttpRequestTest, NotImplementedMethod) {
  std::string raw = "PUT /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: 10\r\n"
                    "\r\n"
                    "hello";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 501);
}
// 501	Unsupported transfer coding	Example: Transfer-Encoding: gzip or
// anything other than chunked, if you only support chunked.
TEST(HttpRequestTest, UnsupportedTransferCoding) {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Transfer-Encoding: gzip, chunked\r\n"
                    "\r\n"
                    "5\r\n"
                    "hello\r\n"
                    "0\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 501);
}
// 505	HTTP Version Not Supported	Version is valid syntax but unsupported.
// Example: HTTP/2.0, HTTP/3.0.
TEST(HttpRequestTest, HttpVersionNotSupported) {
  std::string raw = "GET /index HTTP/2.0\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  webserv::HttpRequest req;
  req.append(raw);

  EXPECT_EQ(req.errorStatus(), 505);
}

// 400	Extra malformed data after request	Especially if you do not support
// pipelining.
// IMPORTANT: it's not your responsibility

// 405	Method Not Allowed	Method is valid and implemented, but forbidden
// by matched location config. Example: POST /static/file when only GET is
// allowed. Must send Allow header ideally.
// IMPORTANT: server must check with request with location config

// 408 Request Timeout	Client connects but does not finish request line,
// headers, or body in your configured timeout. Important because request must
// not hang forever.
// IMPORTANT: part of the connection and server loop