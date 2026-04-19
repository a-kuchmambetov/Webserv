#include "HttpRequest.hpp"
#include <algorithm>
#include <cstddef>

namespace webserv {

ParseOutcome HttpRequest::append(std::string_view bytes) {
  _rawBuffer.append(bytes);

  while (69) {
    switch (_state) {
    case RequestParseState::StartLine:
      if (!parseStartLine()) // receives false in both needMoreData and fail situation(not good)
        return ParseOutcome::NeedMoreData;
      break;
    case RequestParseState::Headers:
      if (!parseHeaders())
        return ParseOutcome::NeedMoreData;
      break;
    case RequestParseState::Body:
      if (!parseBody())
        return ParseOutcome::NeedMoreData;
      break;
    case RequestParseState::ChunkSize: // chunkdata chunk trailer ??
      if (!parseChunkedBody())
        return ParseOutcome::NeedMoreData;
      break;
    case RequestParseState::Complete:
      return ParseOutcome::Complete;
    case webserv::RequestParseState::Error:
      return ParseOutcome::Error;
    default:
      _state = RequestParseState::Error;
      return ParseOutcome::Error;
    }
  }
}

// METHOD SP TARGET SP VERSION
// GET /smth HTTP/1.1

/*
malformed start line	400
unknown method syntax	400
invalid headers	400
invalid body format	400
wrong HTTP version	505
body too large	413
headers too large (optional)	431
*/
bool HttpRequest::parseStartLine() {
  std::string::size_type pos = _rawBuffer.find("\r\n");
  if (pos == std::string::npos)
    return false; // still need to wait for more data from T

  // extract first line
  std::string line = _rawBuffer.substr(0, pos);
  _rawBuffer.erase(0, pos + 2);

  // space checks
  if (line.empty() || std::count(line.begin(), line.end(), ' ') != 2 ||
      line.front() == ' ' || line.back() == ' ')
    return (setError(400), false);

  // space pos
  std::size_t sp1 = line.find(' ');
  std::size_t sp2 = line.find(' ', sp1 + 1);

  // extract parts
  _methodText = line.substr(0, sp1);
  _target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  _httpVersion = line.substr(sp2 + 1);

  // validation
  _method = parseHttpMethod(_methodText);
  if (_method == HttpMethod::Unknown)
    return (setError(400), false);
  if (_httpVersion != "HTTP/1.1")  // only 1.1 ?
    return (setError(505), false);
  if (_target.empty() || _target[0] != '/')
    return (setError(400), false);
  parseTarget();

  _state = RequestParseState::Headers;
  return true;
}

HttpMethod parseHttpMethod(std::string_view method) noexcept{
    if (method == "GET")
        return HttpMethod::Get;
    if (method == "POST")
        return HttpMethod::Post;
    if (method == "DELETE")
        return HttpMethod::Delete;
    return HttpMethod::Unknown;
}

void HttpRequest::setError(int statusCode) noexcept{
    _errorStatus = statusCode;
    _state = RequestParseState::Error;
}




} // namespace webserv