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

    // Simulate raw HTTP request text arriving over the wire
    std::string raw_http = 
        "GET /about HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: curl/8.0\r\n"
        "\r\n";

    std::cout << "--- Parsing Raw HTTP Request ---\n";
    std::cout << raw_http;

    auto req = atlas::parse_request(raw_http);
    if (req.has_value()) {
        std::cout << "Successfully parsed Request: "
                  << req->method() << " " << req->path() << "\n";

        atlas::Response res = app.handle(*req);
        std::cout << "Response -> Status: " << res.status_code() 
                  << ", Body: \"" << res.body() << "\"\n";
    } else {
        std::cout << "Failed to parse request!\n";
    }

    return 0;
}
