#include <atlas>

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// Unlike assert(), CHECK still runs when NDEBUG is defined (e.g. Release builds).
#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr \
                      << "\n";                                                   \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace {

// Checks that a parser finished successfully with the expected request line.
void check_parsed(const atlas::HttpParser& parser, const std::string& method,
                  const std::string& path) {
    CHECK(parser.is_complete());
    CHECK(!parser.has_error());
    auto req = parser.get_request();
    CHECK(req.has_value());
    CHECK(req->method() == method);
    CHECK(req->path() == path);
}

// Checks that a parser rejected its input.
void check_rejected(const atlas::HttpParser& parser) {
    CHECK(parser.has_error());
    CHECK(!parser.is_complete());
    CHECK(!parser.get_request().has_value());
}

// ----------------------------------------------------------------------------
// App
// ----------------------------------------------------------------------------

void test_route_registration() {
    atlas::App app;
    app["/"] = "Hello World!";
    app["/about"] = "About Atlas";

    CHECK(app.size() == 2);
    CHECK(app["/"] == "Hello World!");
    CHECK(app["/about"] == "About Atlas");
    CHECK(app.contains("/"));
    CHECK(app.contains("/about"));
    CHECK(!app.contains("/nonexistent"));
}

void test_route_overwrite() {
    atlas::App app;
    app["/"] = "Hello World!";
    app["/about"] = "About Atlas";
    app["/"] = "Welcome to Atlas!";

    CHECK(app.size() == 2);
    CHECK(app["/"] == "Welcome to Atlas!");
}

void test_get_helper() {
    atlas::App app;
    app["/about"] = "About Atlas";

    CHECK(app.get("/about") == "About Atlas");
    CHECK(app.get("/missing") == "");
}

void test_const_app_access() {
    atlas::App app;
    app["/"] = "Welcome to Atlas!";
    app["/about"] = "About Atlas";

    const atlas::App& const_app = app;
    CHECK(const_app["/"] == "Welcome to Atlas!");
    CHECK(const_app.contains("/about"));
}

void test_handle() {
    atlas::App app;
    app["/"] = "Welcome to Atlas!";
    app["/about"] = "About Atlas";

    atlas::Request req_root("GET", "/");
    CHECK(req_root.method() == "GET");
    CHECK(req_root.path() == "/");

    atlas::Response res_root = app.handle(req_root);
    CHECK(res_root.status_code() == 200);
    CHECK(res_root.body() == "Welcome to Atlas!");

    atlas::Response res_about = app.handle(atlas::Request("GET", "/about"));
    CHECK(res_about.status_code() == 200);
    CHECK(res_about.body() == "About Atlas");

    atlas::Response res_missing = app.handle(atlas::Request("GET", "/not-found"));
    CHECK(res_missing.status_code() == 404);
    CHECK(res_missing.body() == "Not Found");
}

// ----------------------------------------------------------------------------
// parse_request
// ----------------------------------------------------------------------------

void test_parse_request_valid() {
    atlas::App app;
    app["/about"] = "About Atlas";

    auto parsed = atlas::parse_request("GET /about HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
    CHECK(parsed.has_value());
    CHECK(parsed->method() == "GET");
    CHECK(parsed->path() == "/about");

    atlas::Response res = app.handle(*parsed);
    CHECK(res.status_code() == 200);
    CHECK(res.body() == "About Atlas");
}

void test_parse_request_invalid() {
    CHECK(!atlas::parse_request("").has_value());
    CHECK(!atlas::parse_request("GET /about").has_value());
    CHECK(!atlas::parse_request("POST /about HTTP/1.1\r\n\r\n").has_value());
    CHECK(!atlas::parse_request("GET about HTTP/1.1\r\n\r\n").has_value());
    CHECK(!atlas::parse_request("GET / HTTP/2.0\r\n\r\n").has_value());
}

// ----------------------------------------------------------------------------
// serialize_response
// ----------------------------------------------------------------------------

void test_serialize_response() {
    CHECK(atlas::serialize_response(atlas::Response(200, "Hello")) ==
          "HTTP/1.1 200 OK\r\n"
          "Content-Length: 5\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n"
          "Hello");

    CHECK(atlas::serialize_response(atlas::Response(404, "Not Found")) ==
          "HTTP/1.1 404 Not Found\r\n"
          "Content-Length: 9\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n"
          "Not Found");
}

// ----------------------------------------------------------------------------
// Socket
// ----------------------------------------------------------------------------

void test_socket_raii() {
    atlas::Socket empty_sock;
    CHECK(!empty_sock.is_valid());
    CHECK(empty_sock.get() == -1);

    int fds[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    atlas::Socket sock1(fds[0]);
    atlas::Socket sock2(fds[1]);
    CHECK(sock1.is_valid());
    CHECK(sock2.is_valid());

    const char msg[] = "ping";
    CHECK(::send(sock1.get(), msg, sizeof(msg), 0) == sizeof(msg));

    char buf[16] = {0};
    CHECK(::recv(sock2.get(), buf, sizeof(buf), 0) == sizeof(msg));
    CHECK(std::string(buf) == "ping");

    atlas::Socket moved_sock(std::move(sock1));
    CHECK(moved_sock.is_valid());
    CHECK(!sock1.is_valid());

    atlas::Socket assign_sock;
    assign_sock = std::move(moved_sock);
    CHECK(assign_sock.is_valid());

    assign_sock.close();
    CHECK(!assign_sock.is_valid());
}

// ----------------------------------------------------------------------------
// HttpParser (streaming)
// ----------------------------------------------------------------------------

void test_parser_complete_requests() {
    {
        atlas::HttpParser parser;
        parser.feed("GET / HTTP/1.1\r\n\r\n");
        check_parsed(parser, "GET", "/");
    }
    {
        atlas::HttpParser parser;
        parser.feed("GET /about HTTP/1.1\r\nHost: localhost\r\n\r\n");
        check_parsed(parser, "GET", "/about");
    }
}

void test_parser_one_byte_at_a_time() {
    const std::string full_req = "GET /about HTTP/1.1\r\nHost: localhost\r\n\r\n";
    atlas::HttpParser parser;
    for (char c : full_req) {
        CHECK(!parser.is_complete());  // must not complete before the final '\n'
        parser.feed(&c, 1);
    }
    check_parsed(parser, "GET", "/about");
}

void test_parser_uneven_chunks() {
    const std::vector<std::string> chunks = {
        "GE", "T /", "ab", "out HTTP/1.1\r", "\nHost: localhost\r\n", "\r\n"};
    atlas::HttpParser parser;
    for (const auto& chunk : chunks) {
        parser.feed(chunk);
    }
    check_parsed(parser, "GET", "/about");
}

void test_parser_incomplete_request() {
    atlas::HttpParser parser;
    parser.feed("GET /about HTTP/1.1\r\n");
    CHECK(!parser.is_complete());
    CHECK(!parser.has_error());
    CHECK(!parser.get_request().has_value());

    parser.feed("\r\n");
    CHECK(parser.is_complete());
    CHECK(parser.get_request().has_value());
}

void test_parser_rejects_bad_input() {
    for (const char* raw : {
             "POST / HTTP/1.1\r\n\r\n",     // unsupported method
             "GET about HTTP/1.1\r\n\r\n",  // path without leading '/'
             "GET / HTTP/2.0\r\n\r\n",      // unsupported version
         }) {
        atlas::HttpParser parser;
        parser.feed(raw);
        check_rejected(parser);
    }
}

} // namespace

int main() {
    test_route_registration();
    test_route_overwrite();
    test_get_helper();
    test_const_app_access();
    test_handle();
    test_parse_request_valid();
    test_parse_request_invalid();
    test_serialize_response();
    test_socket_raii();
    test_parser_complete_requests();
    test_parser_one_byte_at_a_time();
    test_parser_uneven_chunks();
    test_parser_incomplete_request();
    test_parser_rejects_bad_input();

    std::cout << "All Atlas tests passed.\n";
    return 0;
}
