#include "atlas/serializer.hpp"

#include <string_view>

namespace atlas {

namespace {

// Codes not listed here get an empty reason phrase ("HTTP/1.1 299 "),
// which HTTP/1.1 allows: clients only look at the number.
std::string_view reason_phrase(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 418: return "I'm a teapot";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default:  return "";
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
    raw += "Connection: close\r\n";  // the server closes the socket after every response
    raw += "\r\n";  // blank line ends the headers
    raw += response.body();
    return raw;
}

} // namespace atlas
