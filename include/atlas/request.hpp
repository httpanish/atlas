#pragma once

#include <string>
#include <utility>

namespace atlas {

class Request {
public:
    Request() = default;
    Request(std::string method, std::string path, std::string body = "")
        : method_(std::move(method)), path_(std::move(path)), body_(std::move(body)) {}

    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& body() const { return body_; }

private:
    std::string method_ = "GET";
    std::string path_ = "/";
    std::string body_;
};

} // namespace atlas
