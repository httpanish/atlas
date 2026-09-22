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

    // 1. Simulate raw HTTP request arriving over the wire
    std::string raw_request = 
        "GET /about HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: curl/8.0\r\n"
        "\r\n";

    std::cout << "--- 1. Incoming Raw Request ---\n" << raw_request;

    // 2. Parse raw text into Request
    auto req = atlas::parse_request(raw_request);
    if (!req.has_value()) {
        std::cerr << "Failed to parse request!\n";
        return 1;
    }

    std::cout << "--- 2. Parsed Request Object ---\n";
    std::cout << "Method: " << req->method() << ", Path: " << req->path() << "\n\n";

    // 3. Dispatch to App to produce Response
    atlas::Response res = app.handle(*req);
    std::cout << "--- 3. Produced Response Object ---\n";
    std::cout << "Status: " << res.status_code() << ", Body: \"" << res.body() << "\"\n\n";

    // 4. Serialize Response into raw HTTP response
    std::string raw_response = atlas::serialize_response(res);
    std::cout << "--- 4. Serialized HTTP Response (ready for wire) ---\n";
    std::cout << raw_response << "\n";

    return 0;
}
