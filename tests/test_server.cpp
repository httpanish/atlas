#include <atlas/atlas.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <system_error>

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

} // namespace

int main() {
    test_port_zero_picks_a_port();
    test_port_in_use_throws();
    test_zero_threads_or_queue_throws();

    std::cout << "All Atlas server tests passed.\n";
    return 0;
}
