#include "atlas"
#include <sstream>
#include <utility>

namespace atlas {

std::optional<Request> parse_request(const std::string& raw) {
    if (raw.empty()) {
        return std::nullopt;
    }

    // 1. Extract the first line (the request line)
    auto line_end = raw.find('\n');
    std::string first_line;
    if (line_end == std::string::npos) {
        first_line = raw;
    } else {
        first_line = raw.substr(0, line_end);
    }

    // Strip trailing carriage return '\r' if present
    if (!first_line.empty() && first_line.back() == '\r') {
        first_line.pop_back();
    }

    // 2. Extract method, path, and version from the request line
    std::istringstream stream(first_line);
    std::string method;
    std::string path;
    std::string version;

    // We must successfully read exactly three space-separated tokens
    if (!(stream >> method >> path >> version)) {
        return std::nullopt;
    }

    // Check that there is no extra trailing garbage on the request line
    std::string extra;
    if (stream >> extra) {
        return std::nullopt;
    }

    // 3. Validation:
    // - For this stage, we only support GET requests
    if (method != "GET") {
        return std::nullopt;
    }

    // - Path must start with '/'
    if (path.empty() || path.front() != '/') {
        return std::nullopt;
    }

    // - Version must be HTTP/1.0 or HTTP/1.1
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return std::nullopt;
    }

    // 4. Construct and return the Request object
    return Request(std::move(method), std::move(path));
}

} // namespace atlas
