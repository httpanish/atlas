#include "atlas"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

namespace atlas {

void run_single_connection_server(int port) {
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

    // Always free the addrinfo structure allocated by getaddrinfo()
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

    std::cout << "Atlas TCP server listening on 127.0.0.1:" << port << " (waiting for 1 client)...\n";

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

    // 4. Repeatedly recv() and append received bytes until EOF or error
    std::string received_data;
    char buffer[1024];

    while (true) {
        ssize_t bytes_received = ::recv(client_socket.get(), buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            received_data.append(buffer, static_cast<size_t>(bytes_received));
        } else if (bytes_received == 0) {
            // recv() returning 0 indicates client has closed their write half (orderly EOF)
            std::cout << "Client finished sending (received EOF).\n";
            break;
        } else {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            std::cerr << "recv() failed: " << std::strerror(errno) << "\n";
            break;
        }
    }

    std::cout << "--- Received Data from Client (" << received_data.size() << " bytes) ---\n"
              << received_data << "\n";

    // 5. Repeatedly send() until the entire response is sent or an error occurs
    const std::string response_data = "Hello from Atlas TCP server!\n";
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
        std::cout << "--- Sent Response to Client (" << total_sent << " bytes) ---\n"
                  << response_data;
    }

    std::cout << "Closing client connection and stopping server.\n";

    // client_socket and listen_socket are automatically closed by RAII upon function exit
}

} // namespace atlas
