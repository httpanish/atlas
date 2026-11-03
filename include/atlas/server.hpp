#pragma once

#include "atlas/app.hpp"

namespace atlas {

// Listens on 127.0.0.1:port, serves exactly ONE request with the given App,
// then closes the connection and returns.
void run_single_connection_server(const App& app, int port = 8080);

// Same as above, using an App that only serves "/".
void run_single_connection_server(int port = 8080);

} // namespace atlas
