#include "atlas"

#include <unistd.h>

#include <utility>

namespace atlas {

Socket::Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

void Socket::close() {
    if (is_valid()) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace atlas
