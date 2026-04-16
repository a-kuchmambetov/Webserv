#include "HttpRequest.hpp"
#include <cstddef>

namespace webserv {

ParseOutcome HttpRequest::append(std::string_view bytes) {
  _rawBuffer.append(bytes);

  while (69) {
    switch (_state) {
    case RequestParseState::StartLine:
      if (!parseStartLine())
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
bool HttpRequest::parseStartLine() {
  size_t pos = _rawBuffer.find("\r\n");
  if (pos == std::string::npos)
    return false;

  // extraction
  std::string line = _rawBuffer.substr(0, pos);
  _rawBuffer.erase(0, pos + 2);

  std::size_t sp1 = line.find(' ');
  if (sp1 == std::string::npos)
    return (setError(228), false); // need to check error codes
  std::size_t sp2 = line.find(' ', sp1 + 1);
  if (sp2 == std::string::npos)
    return (setError(228), false); // need to check error code
  _methodText = line.substr(0, sp1);
  std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  _httpVersion = line.substr(sp2 + 1);

  // validation
  _method = parseHttpMethod(_methodText);
  if (_method == HttpMethod::Unknown)
    return (setError(228), false); // need to check error code

  if (_httpVersion != "HTTP/1.1") // only 1.1 ? 
    return (setError(228), false); // need to check error code
  _target = target;
  parseTarget();

  _state = RequestParseState::Headers;
  return true;
}

} // namespace webserv