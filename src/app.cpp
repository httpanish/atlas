#include "atlas/app.hpp"

#include <stdexcept>

namespace atlas {

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
    return routes_.contains(path);
}

std::string App::get(const std::string& path) const {
    auto it = routes_.find(path);
    return it != routes_.end() ? it->second : "";
}

std::size_t App::size() const {
    return routes_.size();
}

Response App::handle(const Request& req) const {
    auto it = routes_.find(req.path());
    if (it == routes_.end()) {
        return Response(404, "Not Found");
    }
    return Response(200, it->second);
}

} // namespace atlas
