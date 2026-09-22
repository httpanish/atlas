#include "atlas"
#include <stdexcept>
#include <utility>

namespace atlas {

// ==========================================
// Request Implementation
// ==========================================

Request::Request(std::string method, std::string path, std::string body)
    : method_(std::move(method)), path_(std::move(path)), body_(std::move(body)) {}

const std::string& Request::method() const {
    return method_;
}

const std::string& Request::path() const {
    return path_;
}

const std::string& Request::body() const {
    return body_;
}

// ==========================================
// Response Implementation
// ==========================================

Response::Response(std::string body)
    : status_code_(200), body_(std::move(body)) {}

Response::Response(int status_code, std::string body)
    : status_code_(status_code), body_(std::move(body)) {}

int Response::status_code() const {
    return status_code_;
}

const std::string& Response::body() const {
    return body_;
}

// ==========================================
// App Implementation
// ==========================================

std::string& App::operator[](const std::string& path) {
    return routes_[path];
}

const std::string& App::operator[](const std::string& path) const {
    auto it = routes_.find(path);
    if (it == routes_.end()) {
        throw std::out_of_range("Route not found: " + path);
    }
    return it->second;
}

bool App::contains(const std::string& path) const {
    return routes_.find(path) != routes_.end();
}

std::string App::get(const std::string& path) const {
    auto it = routes_.find(path);
    if (it != routes_.end()) {
        return it->second;
    }
    return "";
}

std::size_t App::size() const {
    return routes_.size();
}

Response App::handle(const Request& req) const {
    auto it = routes_.find(req.path());
    if (it != routes_.end()) {
        return Response(200, it->second);
    }
    return Response(404, "Not Found");
}

} // namespace atlas
