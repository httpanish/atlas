#include <atlas>
#include <iostream>

int main() {
    atlas::App app;

    // Define routes
    app["/"] = "Hello World!";
    app["/about"] = "About Atlas";

    std::cout << "--- Registered Routes ---\n";
    std::cout << "Route '/'     : " << app["/"] << "\n";
    std::cout << "Route '/about': " << app["/about"] << "\n";
    std::cout << "Total routes  : " << app.size() << "\n\n";

    // Simulate incoming HTTP requests
    std::cout << "--- Handling Requests ---\n";
    atlas::Request req1("GET", "/");
    atlas::Response res1 = app.handle(req1);
    std::cout << req1.method() << " " << req1.path() << " -> Status: " 
              << res1.status_code() << ", Body: \"" << res1.body() << "\"\n";

    atlas::Request req2("GET", "/about");
    atlas::Response res2 = app.handle(req2);
    std::cout << req2.method() << " " << req2.path() << " -> Status: " 
              << res2.status_code() << ", Body: \"" << res2.body() << "\"\n";

    atlas::Request req3("GET", "/contact");
    atlas::Response res3 = app.handle(req3);
    std::cout << req3.method() << " " << req3.path() << " -> Status: " 
              << res3.status_code() << ", Body: \"" << res3.body() << "\"\n";

    return 0;
}
