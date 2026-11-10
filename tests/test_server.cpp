#include <atlas/atlas.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <csignal>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

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

// Every test server uses port 0 so the OS picks a free port, and quiet logs.
atlas::ServerOptions test_options() {
    atlas::ServerOptions options;
    options.port = 0;
    options.log_requests = false;
    return options;
}

// Runs a Server on a background thread for the lifetime of this object.
// The destructor stops the server and waits for run() to return.
class RunningServer {
public:
    RunningServer(const atlas::App& app, atlas::ServerOptions options)
        : server_(app, std::move(options)), thread_([this] { server_.run(); }) {}

    ~RunningServer() {
        server_.stop();
        thread_.join();
    }

    int port() const { return server_.port(); }

private:
    atlas::Server server_;  // declared before thread_, so it exists before run() starts
    std::thread thread_;
};

// Opens a TCP connection to 127.0.0.1:port. Reads on it give up after 3 seconds,
// so a broken server fails the test instead of hanging it.
atlas::Socket connect_to(int port) {
    atlas::Socket socket(::socket(AF_INET, SOCK_STREAM, 0));
    CHECK(socket.is_valid());

    timeval timeout{};
    timeout.tv_sec = 3;
    ::setsockopt(socket.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    CHECK(::connect(socket.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return socket;
}

// Reads until the server closes the connection, and returns everything received.
std::string read_all(const atlas::Socket& socket) {
    std::string received;
    char buffer[1024];
    while (true) {
        ssize_t n = ::recv(socket.get(), buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return received;
        }
        received.append(buffer, static_cast<std::size_t>(n));
    }
}

// Sends raw bytes on a fresh connection and returns the full response.
std::string exchange(int port, std::string_view raw_request) {
    atlas::Socket socket = connect_to(port);
    CHECK(::send(socket.get(), raw_request.data(), raw_request.size(), 0) ==
          static_cast<ssize_t>(raw_request.size()));
    return read_all(socket);
}

// An App with a couple of routes, shared by the tests below.
atlas::App test_app() {
    atlas::App app;
    app["/"] = "home";
    app["/about"] = "about";
    return app;
}

// ----------------------------------------------------------------------------
// Construction
// ----------------------------------------------------------------------------

void test_port_zero_picks_a_port() {
    atlas::App app;
    atlas::Server server(app, test_options());
    CHECK(server.port() > 0);
}

void test_port_in_use_throws() {
    atlas::App app;
    atlas::Server first(app, test_options());

    atlas::ServerOptions options = test_options();
    options.port = first.port();
    bool threw = false;
    try {
        atlas::Server second(app, options);
    } catch (const std::system_error&) {
        threw = true;
    }
    CHECK(threw);
}

void test_zero_threads_or_queue_throws() {
    atlas::App app;
    for (bool zero_threads : {true, false}) {
        atlas::ServerOptions options = test_options();
        if (zero_threads) {
            options.threads = 0;
        } else {
            options.queue_size = 0;
        }
        bool threw = false;
        try {
            atlas::Server server(app, options);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }
}

// ----------------------------------------------------------------------------
// Serving (milestone 1)
// ----------------------------------------------------------------------------

void test_serves_requests_one_after_another() {
    const atlas::App app = test_app();
    RunningServer server(app, test_options());

    const std::string first = exchange(server.port(), "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(first.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(first.ends_with("\r\n\r\nhome"));

    const std::string second = exchange(server.port(), "GET /about HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(second.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(second.ends_with("\r\n\r\nabout"));
}

void test_malformed_request_gets_400() {
    const atlas::App app = test_app();
    RunningServer server(app, test_options());

    const std::string response = exchange(server.port(), "GARBAGE\r\n\r\n");
    CHECK(response.starts_with("HTTP/1.1 400 Bad Request\r\n"));
}

void test_client_that_hangs_up_does_not_stop_the_server() {
    const atlas::App app = test_app();
    RunningServer server(app, test_options());

    {
        atlas::Socket quitter = connect_to(server.port());
    }  // closed without sending anything

    const std::string response = exchange(server.port(), "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(response.starts_with("HTTP/1.1 200 OK\r\n"));
}

void test_silent_client_gets_408() {
    const atlas::App app = test_app();
    atlas::ServerOptions options = test_options();
    options.read_timeout = std::chrono::milliseconds(100);
    RunningServer server(app, options);

    atlas::Socket silent = connect_to(server.port());  // connects, never sends
    const auto start = std::chrono::steady_clock::now();
    const std::string response = read_all(silent);
    const auto waited = std::chrono::steady_clock::now() - start;

    CHECK(response.starts_with("HTTP/1.1 408 Request Timeout\r\n"));
    CHECK(waited >= std::chrono::milliseconds(80));
    CHECK(waited < std::chrono::milliseconds(1000));
}

void test_stop_while_idle_returns_quickly() {
    const atlas::App app = test_app();
    atlas::Server server(app, test_options());
    std::thread runner([&] { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let run() start waiting

    const auto stop_time = std::chrono::steady_clock::now();
    server.stop();
    runner.join();
    CHECK(std::chrono::steady_clock::now() - stop_time < std::chrono::milliseconds(300));
}

// ----------------------------------------------------------------------------
// Thread pool (milestone 2)
// ----------------------------------------------------------------------------
//
// A client that connects and sends nothing keeps one worker busy until the
// read timeout. The tests below use that to fill up workers on purpose.

// Gives the server a moment to accept() a connection we just opened, so tests
// can control the order in which connections reach the server.
void let_server_accept() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void test_silent_client_does_not_block_others() {
    const atlas::App app = test_app();
    atlas::ServerOptions options = test_options();
    options.threads = 2;
    options.read_timeout = std::chrono::milliseconds(1000);
    RunningServer server(app, options);

    atlas::Socket silent = connect_to(server.port());  // occupies one worker
    let_server_accept();

    const auto start = std::chrono::steady_clock::now();
    const std::string response = exchange(server.port(), "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(response.starts_with("HTTP/1.1 200 OK\r\n"));
    // Served by the second worker, without waiting out the silent client's timeout.
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500));
}

// ----------------------------------------------------------------------------
// Logging
// ----------------------------------------------------------------------------

// Sends std::cout into a string for as long as this object lives.
class CaptureStdout {
public:
    CaptureStdout() : previous_(std::cout.rdbuf(captured_.rdbuf())) {}
    ~CaptureStdout() { std::cout.rdbuf(previous_); }
    std::string text() const { return captured_.str(); }

private:
    std::ostringstream captured_;
    std::streambuf* previous_;
};

void test_logs_one_line_per_request() {
    const atlas::App app = test_app();
    atlas::ServerOptions options = test_options();
    options.log_requests = true;

    CaptureStdout capture;
    {
        RunningServer server(app, options);
        exchange(server.port(), "GET /about HTTP/1.1\r\nHost: x\r\n\r\n");
        exchange(server.port(), "GARBAGE\r\n\r\n");
    }  // server stopped, so every log line has been written

    // e.g. "GET /about 200 0.4ms\n- - 400 0.1ms\n"
    const std::regex expected(R"(GET /about 200 \d+\.\dms\n- - 400 \d+\.\dms\n)");
    CHECK(std::regex_match(capture.text(), expected));
}

void test_no_logs_when_disabled() {
    const atlas::App app = test_app();
    CaptureStdout capture;
    {
        RunningServer server(app, test_options());  // log_requests = false
        exchange(server.port(), "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    }
    CHECK(capture.text().empty());
}

// A stand-in for "the program's own handler", to check run() puts it back.
void program_handler(int) {}

void test_signal_stops_run_and_restores_handler() {
    for (int signal_number : {SIGINT, SIGTERM}) {
        struct sigaction mine{};
        mine.sa_handler = program_handler;
        sigemptyset(&mine.sa_mask);
        struct sigaction original{};
        CHECK(::sigaction(signal_number, &mine, &original) == 0);

        const atlas::App app = test_app();
        atlas::Server server(app, test_options());
        std::atomic<bool> finished = false;
        std::thread runner([&] {
            server.run();
            finished = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let run() install its handler

        ::kill(::getpid(), signal_number);  // as if the user pressed Ctrl+C

        // Wait up to a second for run() to return, rather than join() and hang forever.
        for (int i = 0; i < 100 && !finished; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(finished);
        runner.join();

        struct sigaction after{};
        CHECK(::sigaction(signal_number, nullptr, &after) == 0);
        CHECK(after.sa_handler == program_handler);

        ::sigaction(signal_number, &original, nullptr);
    }
}

} // namespace

int main() {
    test_port_zero_picks_a_port();
    test_port_in_use_throws();
    test_zero_threads_or_queue_throws();
    test_serves_requests_one_after_another();
    test_malformed_request_gets_400();
    test_client_that_hangs_up_does_not_stop_the_server();
    test_silent_client_gets_408();
    test_stop_while_idle_returns_quickly();
    test_signal_stops_run_and_restores_handler();
    test_silent_client_does_not_block_others();
    test_logs_one_line_per_request();
    test_no_logs_when_disabled();

    std::cout << "All Atlas server tests passed.\n";
    return 0;
}
