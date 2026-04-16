#include "HttpRequest.hpp"

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

} // namespace webserv