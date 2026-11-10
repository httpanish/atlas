#pragma once

#include "atlas/app.hpp"
#include "atlas/socket.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

namespace atlas {

// Settings for a Server. Fill in only what you need:
//
//   atlas::Server server(app, {.port = 3000, .threads = 8});
//
// This lives outside Server on purpose. A struct nested inside a class can't be
// used as a default argument (`Options options = {}`) of that same class,
// because its default values aren't known until the class is complete.
struct ServerOptions {
    std::string host = "127.0.0.1";
    int port = 8080;                               // 0 = let the OS pick a free port
    std::size_t threads = 4;                       // worker threads
    std::size_t queue_size = 64;                   // max connections waiting for a worker
    std::chrono::milliseconds read_timeout{5000};  // max silence from a client before 408 (must be > 0)
    bool log_requests = true;                      // one line per request on stdout
};

// An HTTP server that serves an App until it is told to stop.
//
//   atlas::Server server(app);
//   server.run();  // blocks until Ctrl+C, SIGTERM, or stop()
//
// The App is held by reference: it must outlive the Server, and must not be
// changed while run() is executing.
//
// Requests are handled on several worker threads at once (options.threads),
// so App::handle() can run concurrently. Anything a route touches that can
// change must be protected (e.g. with a std::mutex).
class Server {
public:
    // Binds and starts listening right away, so port() is valid immediately.
    // Throws std::system_error if the address can't be used, and
    // std::invalid_argument if threads or queue_size is 0, or read_timeout
    // isn't positive.
    explicit Server(const App& app, ServerOptions options = {});

    // Not copyable or movable: a Server owns its listening socket, and its
    // worker threads point back at it.
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Serves clients until stop(), Ctrl+C (SIGINT) or SIGTERM, then returns.
    // Requests already being handled are finished first. Can be called once.
    void run();

    // Asks run() to return. Safe to call from any thread, even before run()
    // starts. run() notices within about 200 ms.
    void stop() { stop_requested_ = true; }

    // The port actually bound (useful with port = 0).
    int port() const { return port_; }

private:
    // What each worker thread runs: take a connection off the queue, handle it, repeat.
    void worker_loop();

    // Reads one request from the client and answers it. The caller closes the socket.
    void handle_connection(const Socket& client);

    // Answers 503 Service Unavailable without reading the request. Used when
    // the queue is full, and for queued connections when shutting down.
    void reject(const Socket& client);

    // Writes one line like "GET /about 200 0.4ms" to stdout, if log_requests is on.
    void log_request(const std::optional<Request>& request, int status_code,
                     std::chrono::steady_clock::time_point start) const;

    const App& app_;
    ServerOptions options_;
    Socket listener_;
    int port_ = 0;

    // Written by stop() on one thread, read by run() on another. std::atomic
    // makes that safe; a plain bool here would be a data race.
    std::atomic<bool> stop_requested_ = false;

    // Connections accepted by run() and waiting for a worker. Shared by all
    // threads, so queue_ and stopping_ are only touched with queue_mutex_ locked.
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;  // wakes a sleeping worker when work arrives
    std::queue<Socket> queue_;
    bool stopping_ = false;

    // Workers log at the same time. Holding this while writing keeps one
    // log line from being cut in half by another.
    mutable std::mutex log_mutex_;
};

} // namespace atlas
