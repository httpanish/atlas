#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atlas {

// A header value split into its plain words and its (parenthesised) comments.
// Only filled in for headers that allow comments: User-Agent and Via.
//
//   "Foo/1.0 (X11; (n) L)"  ->  words = {"Foo/1.0"}, comments = {"X11; (n) L"}
struct CommentedValue {
    std::vector<std::string> words;
    std::vector<std::string> comments;
};

class Request {
public:
    using Headers = std::unordered_map<std::string, std::string>;
    using CommentedHeaders = std::unordered_map<std::string, CommentedValue>;

    Request() = default;

    // Short form, handy for routing tests.
    Request(std::string method, std::string path, std::string body = "")
        : method_(std::move(method)), path_(std::move(path)), body_(std::move(body)) {}

    // Full form, used by RequestParser. Header names must already be lowercase.
    Request(std::string method, std::string path, std::string version, Headers headers,
            CommentedHeaders commented, std::string body)
        : method_(std::move(method)),
          path_(std::move(path)),
          version_(std::move(version)),
          headers_(std::move(headers)),
          commented_(std::move(commented)),
          body_(std::move(body)) {}

    const std::string& method() const { return method_; }

    // The raw request target, query string included (no splitting or decoding).
    const std::string& path() const { return path_; }

    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }

    // All headers, keyed by lowercase name.
    const Headers& headers() const { return headers_; }

    // Case-insensitive lookup: header("Host") and header("host") are the same.
    std::optional<std::string_view> header(std::string_view name) const {
        auto it = headers_.find(to_lower(name));
        if (it == headers_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Words and comments of a User-Agent or Via header. nullptr otherwise.
    const CommentedValue* commented(std::string_view name) const {
        auto it = commented_.find(to_lower(name));
        return it == commented_.end() ? nullptr : &it->second;
    }

private:
    // ASCII-only lowercasing, which is all header names need.
    static std::string to_lower(std::string_view s) {
        std::string out(s);
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    }

    std::string method_ = "GET";
    std::string path_ = "/";
    std::string version_ = "HTTP/1.1";
    Headers headers_;
    CommentedHeaders commented_;
    std::string body_;
};

} // namespace atlas
