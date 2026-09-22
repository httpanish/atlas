#include <atlas>
#include <iostream>

int main() {
    atlas::App app;

    app["/"] = "Hello World from Atlas Web Server!";
    app["/about"] = "About Atlas: A modern, simple C++ HTTP library";

    std::cout << "Starting Atlas HTTP server on port 8080...\n";
    atlas::run_single_connection_server(app, 8080);
    std::cout << "Atlas HTTP server exited cleanly.\n";
    return 0;
}
