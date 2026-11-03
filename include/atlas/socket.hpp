#pragma once

namespace atlas {

// Owns a POSIX socket file descriptor and closes it on destruction (RAII).
// Move-only, because exactly one object may own a given descriptor.
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { close(); }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    int get() const noexcept { return fd_; }
    bool is_valid() const noexcept { return fd_ >= 0; }
    void close();

private:
    int fd_ = -1;
};

} // namespace atlas
