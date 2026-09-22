#include "atlas"
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

} // namespace atlas
