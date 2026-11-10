#include <atlas/atlas.hpp>
#include <iostream>

int main() {
    atlas::App app;

    app["/"] = "Hello World from Atlas Web Server!";
    app["/about"] = "About Atlas: A modern, simple C++ HTTP library";

    atlas::Server server(app, {.port = 8080});
    std::cout << "Atlas HTTP server listening on http://127.0.0.1:" << server.port()
              << " (Ctrl+C to stop)\n";
    server.run();
    std::cout << "Atlas HTTP server exited cleanly.\n";
    return 0;
}
