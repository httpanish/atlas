#include <atlas>
#include <cassert>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

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
    assert(!atlas::parse_request("").has_value());
    assert(!atlas::parse_request("GET /about").has_value());
    assert(!atlas::parse_request("POST /about HTTP/1.1\r\n").has_value());
    assert(!atlas::parse_request("GET about HTTP/1.1\r\n").has_value());
    assert(!atlas::parse_request("GET / HTTP/2.0\r\n").has_value());
    assert(!atlas::parse_request("GET /about HTTP/1.1 extra_token\r\n").has_value());

    // 9. Test serialize_response
    // 200 OK test
    atlas::Response res_ok(200, "Hello");
    std::string raw_ok = atlas::serialize_response(res_ok);
    std::string expected_ok = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Hello";
    assert(raw_ok == expected_ok);

    // 404 Not Found test
    atlas::Response res_404(404, "Not Found");
    std::string raw_404 = atlas::serialize_response(res_404);
    std::string expected_404 = 
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 9\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Not Found";
    assert(raw_404 == expected_404);

    // 10. Test Socket RAII wrapper
    {
        // Default constructor creates invalid socket
        atlas::Socket empty_sock;
        assert(!empty_sock.is_valid());
        assert(empty_sock.get() == -1);

        // Create a connected pair of sockets to test RAII ownership and communication
        int sv[2];
        int res = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        assert(res == 0);

        atlas::Socket sock1(sv[0]);
        atlas::Socket sock2(sv[1]);
        assert(sock1.is_valid());
        assert(sock2.is_valid());

        // Test sending and receiving through RAII wrapped sockets
        const char msg[] = "ping";
        ssize_t sent = ::send(sock1.get(), msg, sizeof(msg), 0);
        assert(sent == sizeof(msg));

        char buf[16] = {0};
        ssize_t recvd = ::recv(sock2.get(), buf, sizeof(buf), 0);
        assert(recvd == sizeof(msg));
        assert(std::string(buf) == "ping");

        // Test move constructor
        atlas::Socket moved_sock(std::move(sock1));
        assert(moved_sock.is_valid());
        assert(!sock1.is_valid());
        assert(sock1.get() == -1);

        // Test move assignment
        atlas::Socket assign_sock;
        assign_sock = std::move(moved_sock);
        assert(assign_sock.is_valid());
        assert(!moved_sock.is_valid());

        // Test explicit close
        assign_sock.close();
        assert(!assign_sock.is_valid());
        assert(assign_sock.get() == -1);
    } // sock2 goes out of scope here and automatically closes its fd via RAII!

    std::cout << "All Atlas tests (including Socket RAII) passed successfully!\n";
    return 0;
}
