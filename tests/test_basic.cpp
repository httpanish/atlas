#include <atlas/atlas.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

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
// serialize_response
// ----------------------------------------------------------------------------

void test_serialize_response() {
    CHECK(atlas::serialize_response(atlas::Response(200, "Hello")) ==
          "HTTP/1.1 200 OK\r\n"
          "Content-Length: 5\r\n"
          "Content-Type: text/plain\r\n"
          "Connection: close\r\n"
          "\r\n"
          "Hello");

    CHECK(atlas::serialize_response(atlas::Response(404, "Not Found")) ==
          "HTTP/1.1 404 Not Found\r\n"
          "Content-Length: 9\r\n"
          "Content-Type: text/plain\r\n"
          "Connection: close\r\n"
          "\r\n"
          "Not Found");
}

// The first line of a serialized response, without its "\r\n".
std::string status_line(int status_code) {
    const std::string raw = atlas::serialize_response(atlas::Response(status_code, ""));
    return raw.substr(0, raw.find("\r\n"));
}

void test_reason_phrases() {
    CHECK(status_line(200) == "HTTP/1.1 200 OK");
    CHECK(status_line(201) == "HTTP/1.1 201 Created");
    CHECK(status_line(204) == "HTTP/1.1 204 No Content");
    CHECK(status_line(405) == "HTTP/1.1 405 Method Not Allowed");
    CHECK(status_line(408) == "HTTP/1.1 408 Request Timeout");
    CHECK(status_line(418) == "HTTP/1.1 418 I'm a teapot");
    CHECK(status_line(500) == "HTTP/1.1 500 Internal Server Error");
    CHECK(status_line(503) == "HTTP/1.1 503 Service Unavailable");
    // Unknown codes get an empty reason phrase, which HTTP/1.1 allows.
    CHECK(status_line(299) == "HTTP/1.1 299 ");
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

} // namespace

int main() {
    test_route_registration();
    test_route_overwrite();
    test_get_helper();
    test_const_app_access();
    test_handle();
    test_serialize_response();
    test_reason_phrases();
    test_socket_raii();

    std::cout << "All Atlas tests passed.\n";
    return 0;
}
