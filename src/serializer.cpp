#include "atlas"

namespace atlas {

std::string serialize_response(const Response& response) {
    // 1. Determine the status reason phrase
    std::string reason = "OK";
    if (response.status_code() == 404) {
        reason = "Not Found";
    } else if (response.status_code() == 400) {
        reason = "Bad Request";
    }

    std::string raw;

    // 2. HTTP/1.1 status line
    raw += "HTTP/1.1 " + std::to_string(response.status_code()) + " " + reason + "\r\n";

    // 3. Response headers
    raw += "Content-Length: " + std::to_string(response.body().size()) + "\r\n";
    raw += "Content-Type: text/plain\r\n";

    // 4. Blank line to separate headers from the body
    raw += "\r\n";

    // 5. Response body
    raw += response.body();

    return raw;
}

} // namespace atlas
