#pragma once

#include "atlas/request.hpp"
#include "atlas/response.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace atlas {

// An App is a map of path -> response body.
//
// Thread safety: a Server calls handle() from several threads at once. That is
// safe because handle() only reads the map, as long as no route is added or
// changed while the Server is running.
class App {
public:
    // Register or overwrite a route:  app["/"] = "Hello World!";
    std::string& operator[](const std::string& path);

    // Read-only lookup. Throws std::out_of_range if the path is not registered.
    const std::string& operator[](const std::string& path) const;

    bool contains(const std::string& path) const;

    // Returns the route's body, or an empty string if the path is not registered.
    std::string get(const std::string& path) const;

    std::size_t size() const;

    // Produces a 200 with the route's body, or a 404 if the path is not registered.
    Response handle(const Request& req) const;

private:
    std::unordered_map<std::string, std::string> routes_;
};

} // namespace atlas
