#pragma once

#include <string>
#include <utility>

namespace atlas {

class Response {
public:
    Response() = default;
    Response(std::string body) : body_(std::move(body)) {}
    Response(int status_code, std::string body)
        : status_code_(status_code), body_(std::move(body)) {}

    int status_code() const { return status_code_; }
    const std::string& body() const { return body_; }

private:
    int status_code_ = 200;
    std::string body_;
};

} // namespace atlas
