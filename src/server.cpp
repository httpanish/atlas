#include "atlas"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

namespace atlas {

void run_single_connection_server(const App& app, int port) {
    // 1. Resolve localhost address using getaddrinfo()
    struct addrinfo hints{};
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP stream socket
    hints.ai_flags = AI_PASSIVE;      // Suitable for bind()

    struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    int status = ::getaddrinfo("127.0.0.1", port_str.c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo() failed: " << gai_strerror(status) << "\n";
        return;
    }

    // Loop through the results and bind to the first possible address
    int raw_listen_fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        raw_listen_fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (raw_listen_fd < 0) {
            continue;
        }

        // Enable SO_REUSEADDR so port can be rebound immediately
        int opt = 1;
        ::setsockopt(raw_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (::bind(raw_listen_fd, p->ai_addr, p->ai_addrlen) == 0) {
            break; // Successfully bound!
        }

        ::close(raw_listen_fd);
        raw_listen_fd = -1;
    }

    // Free the addrinfo structure allocated by getaddrinfo()
    ::freeaddrinfo(res);

    if (raw_listen_fd < 0) {
        std::cerr << "Failed to bind to 127.0.0.1 on port " << port << "\n";
        return;
    }

    // Wrap the listening socket in our RAII wrapper
    Socket listen_socket(raw_listen_fd);

    // 2. Call listen() to wait for incoming connections
    if (::listen(listen_socket.get(), 1) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        return;
    }

    std::cout << "Atlas HTTP server listening on http://127.0.0.1:" << port << " (waiting for 1 client)...\n";

    // 3. Call accept() for one client
    sockaddr_storage client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int raw_client_fd = ::accept(listen_socket.get(), reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (raw_client_fd < 0) {
        std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
        return;
    }

    // Wrap the client socket in our RAII wrapper
    Socket client_socket(raw_client_fd);
    std::cout << "Client successfully connected!\n";

    // 4. Repeatedly recv() and stream bytes directly into the table-driven HttpParser
    HttpParser parser;
    char buffer[1024];

    while (!parser.is_complete() && !parser.has_error()) {
        ssize_t bytes_received = ::recv(client_socket.get(), buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            parser.feed(buffer, static_cast<size_t>(bytes_received));
        } else if (bytes_received == 0) {
            // Client closed connection (orderly EOF)
            break;
        } else {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            std::cerr << "recv() failed: " << std::strerror(errno) << "\n";
            break;
        }
    }

    // 5. Produce Request and dispatch to App
    auto req = parser.get_request();
    Response response;

    if (req.has_value()) {
        std::cout << "Dispatched to route: " << req->method() << " " << req->path() << "\n";
        // 6. Let App handle the Request and produce a Response
        response = app.handle(*req);
    } else {
        std::cerr << "Parser reported error or incomplete request! Returning 400 Bad Request.\n";
        response = Response(400, "Bad Request");
    }

    // 7. Serialize the Response into standard HTTP/1.1 wire format
    std::string response_data = serialize_response(response);

    // 8. Repeatedly send() until the entire HTTP response is sent or an error occurs
    size_t total_sent = 0;
    bool send_failed = false;

    while (total_sent < response_data.size()) {
        ssize_t bytes_sent = ::send(client_socket.get(),
                                    response_data.data() + total_sent,
                                    response_data.size() - total_sent,
                                    0);
        if (bytes_sent < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            std::cerr << "send() failed: " << std::strerror(errno) << "\n";
            send_failed = true;
            break;
        }
        total_sent += static_cast<size_t>(bytes_sent);
    }

    if (!send_failed) {
        std::cout << "--- Sent HTTP Response (" << total_sent << " bytes) ---\n"
                  << response_data;
    }

    std::cout << "Closing client connection and stopping server.\n";

    // client_socket and listen_socket are automatically closed by RAII upon function exit
}

void run_single_connection_server(int port) {
    App default_app;
    default_app["/"] = "Hello from Atlas!";
    run_single_connection_server(default_app, port);
}

} // namespace atlas
