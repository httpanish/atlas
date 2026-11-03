#pragma once

#include "atlas/response.hpp"

#include <string>

namespace atlas {

// Converts a Response into HTTP/1.1 wire format.
std::string serialize_response(const Response& response);

} // namespace atlas
