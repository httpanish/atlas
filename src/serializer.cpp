#include "atlas"

#include <string_view>

namespace atlas {

namespace {

// Codes not listed here currently fall back to "OK" (see docs/routing-spec.md §5.3).
std::string_view reason_phrase(int status_code) {
    switch (status_code) {
        case 400: return "Bad Request";
        case 404: return "Not Found";
        default:  return "OK";
    }
}

} // namespace

std::string serialize_response(const Response& response) {
    const std::string status = std::to_string(response.status_code());
    const std::string length = std::to_string(response.body().size());

    std::string raw;
    raw += "HTTP/1.1 " + status + " " + std::string(reason_phrase(response.status_code())) + "\r\n";
    raw += "Content-Length: " + length + "\r\n";
    raw += "Content-Type: text/plain\r\n";
    raw += "\r\n";  // blank line ends the headers
    raw += response.body();
    return raw;
}

} // namespace atlas
