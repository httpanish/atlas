#include <atlas>
#include <iostream>

int main() {
    std::cout << "Starting Atlas minimal TCP server on port 8080...\n";
    atlas::run_single_connection_server(8080);
    std::cout << "Atlas minimal TCP server exited cleanly.\n";
    return 0;
}
