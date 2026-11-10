#include "atlas/server.hpp"

#include "atlas/request_parser.hpp"
#include "atlas/serializer.hpp"
#include "atlas/socket.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace atlas {

namespace {

// Creates a TCP socket bound to 127.0.0.1:port and starts listening on it.
// Returns an invalid Socket (and logs why) on failure.
Socket open_listening_socket(int port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    addrinfo* addresses = nullptr;
    const std::string port_str = std::to_string(port);
    if (int status = ::getaddrinfo("127.0.0.1", port_str.c_str(), &hints, &addresses); status != 0) {
        std::cerr << "getaddrinfo() failed: " << gai_strerror(status) << "\n";
        return {};
    }

    // Bind to the first address that works.
    Socket listener;
    for (addrinfo* addr = addresses; addr != nullptr; addr = addr->ai_next) {
        Socket candidate(::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol));
        if (!candidate.is_valid()) {
            continue;
        }

        // Lets the port be reused immediately after the server exits.
        int enable = 1;
        ::setsockopt(candidate.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

        if (::bind(candidate.get(), addr->ai_addr, addr->ai_addrlen) == 0) {
            listener = std::move(candidate);
            break;
        }
    }
    ::freeaddrinfo(addresses);

    if (!listener.is_valid()) {
        std::cerr << "Failed to bind to 127.0.0.1 on port " << port << "\n";
        return {};
    }

    if (::listen(listener.get(), /*backlog=*/1) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        return {};
    }

    return listener;
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

void run_single_connection_server(const App& app, int port) {
    Socket listener = open_listening_socket(port);
    if (!listener.is_valid()) {
        return;
    }
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
