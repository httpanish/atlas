#include <atlas>
#include <cassert>
#include <iostream>

int main() {
    atlas::App app;

    // 1. Test basic route assignment
    app["/"] = "Hello World!";
    app["/about"] = "About Atlas";

    assert(app.size() == 2);
    assert(app["/"] == "Hello World!");
    assert(app["/about"] == "About Atlas");
    assert(app.contains("/"));
    assert(app.contains("/about"));
    assert(!app.contains("/nonexistent"));

    // 2. Test route overwrite
    app["/"] = "Welcome to Atlas!";
    assert(app.size() == 2);
    assert(app["/"] == "Welcome to Atlas!");

    // 3. Test get() helper
    assert(app.get("/about") == "About Atlas");
    assert(app.get("/missing") == "");

    // 4. Test reading from a const App
    const atlas::App& const_app = app;
    assert(const_app["/"] == "Welcome to Atlas!");
    assert(const_app.contains("/about"));

    // 5. Test Request and Response handling
    atlas::Request req_root("GET", "/");
    assert(req_root.method() == "GET");
    assert(req_root.path() == "/");

    atlas::Response res_root = app.handle(req_root);
    assert(res_root.status_code() == 200);
    assert(res_root.body() == "Welcome to Atlas!");

    atlas::Request req_about("GET", "/about");
    atlas::Response res_about = app.handle(req_about);
    assert(res_about.status_code() == 200);
    assert(res_about.body() == "About Atlas");

    // 6. Test 404 Not Found for unregistered routes
    atlas::Request req_missing("GET", "/not-found");
    atlas::Response res_missing = app.handle(req_missing);
    assert(res_missing.status_code() == 404);
    assert(res_missing.body() == "Not Found");

    // 7. Test parse_request with valid GET requests
    std::string raw1 = "GET /about HTTP/1.1\r\nHost: localhost:8080\r\n\r\n";
    auto parsed1 = atlas::parse_request(raw1);
    assert(parsed1.has_value());
    assert(parsed1->method() == "GET");
    assert(parsed1->path() == "/about");

    // Pass the parsed request to app.handle()
    atlas::Response res_parsed1 = app.handle(*parsed1);
    assert(res_parsed1.status_code() == 200);
    assert(res_parsed1.body() == "About Atlas");

    // Valid single-line GET without headers
    std::string raw2 = "GET / HTTP/1.0";
    auto parsed2 = atlas::parse_request(raw2);
    assert(parsed2.has_value());
    assert(parsed2->method() == "GET");
    assert(parsed2->path() == "/");
    assert(app.handle(*parsed2).body() == "Welcome to Atlas!");

    // 8. Test parse_request with malformed / unsupported inputs
    // Empty string
    assert(!atlas::parse_request("").has_value());

    // Incomplete request line (missing HTTP version)
    assert(!atlas::parse_request("GET /about").has_value());

    // Unsupported method (POST is not supported yet)
    assert(!atlas::parse_request("POST /about HTTP/1.1\r\n").has_value());

    // Invalid path without leading slash
    assert(!atlas::parse_request("GET about HTTP/1.1\r\n").has_value());

    // Unsupported HTTP version
    assert(!atlas::parse_request("GET / HTTP/2.0\r\n").has_value());

    // Extra tokens on the request line
    assert(!atlas::parse_request("GET /about HTTP/1.1 extra_token\r\n").has_value());

    std::cout << "All Atlas tests (including parse_request) passed successfully!\n";
    return 0;
}
