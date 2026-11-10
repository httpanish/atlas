#include <atlas/atlas.hpp>
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

    // 1. Simulate a raw HTTP request arriving over the wire
    const std::string raw_request =
        "GET /about HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: curl/8.0 (x86_64; Linux)\r\n"
        "\r\n";
    std::cout << "--- 1. Incoming Raw Request ---\n" << raw_request;

    // 2. Parse the raw bytes into a Request
    atlas::RequestParser parser;
    parser.feed(raw_request);
    if (parser.status() != atlas::RequestParser::Status::Complete) {
        std::cerr << "Failed to parse request! (status "
                  << atlas::status_for(parser.error()) << ")\n";
        return 1;
    }
    atlas::Request req = parser.take_request();
    std::cout << "--- 2. Parsed Request Object ---\n";
    std::cout << "Method: " << req.method() << ", Path: " << req.path()
              << ", Version: " << req.version() << "\n";
    // commented() returns nullptr when the header is absent, so check first.
    if (const atlas::CommentedValue* ua = req.commented("user-agent"); ua && !ua->comments.empty()) {
        std::cout << "User-Agent comment: " << ua->comments.front() << "\n";
    }
    std::cout << "\n";

    // 3. Dispatch to App to produce Response
    atlas::Response res = app.handle(req);
    std::cout << "--- 3. Produced Response Object ---\n";
    std::cout << "Status: " << res.status_code() << ", Body: \"" << res.body() << "\"\n\n";

    // 4. Serialize Response into raw HTTP response
    std::string raw_response = atlas::serialize_response(res);
    std::cout << "--- 4. Serialized HTTP Response (ready for wire) ---\n";
    std::cout << raw_response << "\n";

    return 0;
}
