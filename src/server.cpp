#include "atlas/server.hpp"

#include "atlas/request_parser.hpp"
#include "atlas/serializer.hpp"
#include "atlas/socket.hpp"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace atlas {

namespace {

// Throws a std::system_error built from errno, e.g. "bind 127.0.0.1:8080: Address already in use".
[[noreturn]] void throw_errno(int error, const std::string& what) {
    throw std::system_error(error, std::generic_category(), what);
}

// Creates a TCP socket bound to host:port and starts listening on it.
// Throws std::system_error on failure.
Socket open_listening_socket(const std::string& host, int port) {
    const std::string where = host + ":" + std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    addrinfo* addresses = nullptr;
    const std::string port_str = std::to_string(port);
    if (int status = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &addresses); status != 0) {
        // getaddrinfo() has its own error codes instead of errno, so report EINVAL
        // and put its message in the text.
        throw_errno(EINVAL, "getaddrinfo " + where + " (" + gai_strerror(status) + ")");
    }

    // Bind to the first address that works.
    Socket listener;
    int bind_error = 0;
    for (addrinfo* addr = addresses; addr != nullptr; addr = addr->ai_next) {
        Socket candidate(::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol));
        if (!candidate.is_valid()) {
            bind_error = errno;
            continue;
        }

        // Lets the port be reused immediately after the server exits.
        int enable = 1;
        ::setsockopt(candidate.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

        if (::bind(candidate.get(), addr->ai_addr, addr->ai_addrlen) == 0) {
            listener = std::move(candidate);
            break;
        }
        bind_error = errno;
    }
    ::freeaddrinfo(addresses);

    if (!listener.is_valid()) {
        throw_errno(bind_error, "bind " + where);
    }

    // SOMAXCONN: let the kernel queue as many waiting clients as it allows.
    if (::listen(listener.get(), SOMAXCONN) < 0) {
        throw_errno(errno, "listen " + where);
    }

    return listener;
}

// Asks the OS which port a socket ended up on (matters when we asked for port 0).
int bound_port(const Socket& socket) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        throw_errno(errno, "getsockname");
    }
    return ntohs(address.sin_port);  // ntohs: network byte order -> host byte order
}

// Blocks until a client connects. Returns an invalid Socket on failure.
Socket accept_client(const Socket& listener) {
    Socket client(::accept(listener.get(), nullptr, nullptr));
    if (!client.is_valid()) {
        std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
    }
    return client;
}

// What read_request() got from the client. Exactly one of three things:
//   - request is set:       a full request arrived
//   - error_status is set:  reply with this status and an empty body
//   - neither is set:       nothing to answer (the client sent nothing, or
//                           recv() failed), so close without replying
struct ReadResult {
    std::optional<Request> request;
    int error_status = 0;
};

// Streams bytes from the client into the parser until a full request has
// arrived, the input is rejected, or the connection ends.
ReadResult read_request(const Socket& client) {
    RequestParser parser;
    char buffer[1024];
    bool received_anything = false;

    while (parser.status() == RequestParser::Status::NeedMore) {
        ssize_t received = ::recv(client.get(), buffer, sizeof(buffer), 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;  // interrupted by a signal, so retry
            }
            std::cerr << "recv() failed: " << std::strerror(errno) << "\n";
            return {};
        }
        if (received == 0) {
            // Client closed the connection. A half-sent request gets a 400;
            // a connection that never sent anything is closed silently.
            return received_anything ? ReadResult{std::nullopt, 400} : ReadResult{};
        }
        received_anything = true;
        // Any bytes after a complete request are ignored: one request per connection.
        parser.feed(std::string_view(buffer, static_cast<std::size_t>(received)));
    }

    if (parser.status() == RequestParser::Status::Error) {
        return {std::nullopt, status_for(parser.error())};
    }
    return {parser.take_request(), 0};
}

// send() may write only part of the buffer, so keep going until it's all sent.
bool send_all(const Socket& client, std::string_view data) {
    std::size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t sent = ::send(client.get(), data.data() + total_sent, data.size() - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "send() failed: " << std::strerror(errno) << "\n";
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }
    return true;
}

Response respond(const App& app, const Request& req) {
    std::cout << "Dispatched to route: " << req.method() << " " << req.path() << "\n";
    return app.handle(req);
}

} // namespace

Server::Server(const App& app, ServerOptions options)
    : app_(app), options_(std::move(options)) {
    if (options_.threads == 0 || options_.queue_size == 0) {
        throw std::invalid_argument("ServerOptions: threads and queue_size must be at least 1");
    }
    listener_ = open_listening_socket(options_.host, options_.port);
    port_ = bound_port(listener_);
}

void run_single_connection_server(const App& app, int port) {
    Socket listener = open_listening_socket("127.0.0.1", port);
    std::cout << "Atlas HTTP server listening on http://127.0.0.1:" << port
              << " (waiting for 1 client)...\n";

    Socket client = accept_client(listener);
    if (!client.is_valid()) {
        return;
    }
    std::cout << "Client successfully connected!\n";

    const ReadResult result = read_request(client);
    if (!result.request && result.error_status == 0) {
        std::cout << "Client sent nothing. Closing without a response.\n";
        return;
    }

    Response response;
    if (result.request) {
        response = respond(app, *result.request);
    } else {
        std::cerr << "Rejected request with status " << result.error_status << ".\n";
        response = Response(result.error_status, "");
    }
    const std::string raw_response = serialize_response(response);

    if (send_all(client, raw_response)) {
        std::cout << "--- Sent HTTP Response (" << raw_response.size() << " bytes) ---\n"
                  << raw_response;
    }

    std::cout << "Closing client connection and stopping server.\n";
    // Both sockets are closed automatically when they go out of scope.
}

void run_single_connection_server(int port) {
    App default_app;
    default_app["/"] = "Hello from Atlas!";
    run_single_connection_server(default_app, port);
}

} // namespace atlas
