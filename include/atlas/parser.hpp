#pragma once

#include "atlas/request.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace atlas {

// A streaming, table-driven HTTP request parser.
//
// Bytes can be fed in chunks of any size (even one byte at a time). Each byte
// is classified with a lookup table, and the next state comes from a
// transition table. See src/parser.cpp for both tables.
class HttpParser {
public:
    // The numeric values index rows of the transition table, so keep them in order.
    enum State {
        STATE_METHOD = 0,
        STATE_PATH = 1,
        STATE_VERSION = 2,
        STATE_REQ_LINE_CR = 3,
        STATE_HEADER_START = 4,
        STATE_HEADER_LINE = 5,
        STATE_HEADER_LINE_CR = 6,
        STATE_EMPTY_LINE_CR = 7,
        STATE_COMPLETE = 8,
        STATE_ERROR = 9
    };

    // Feeds a chunk of bytes. Returns false once the input has been rejected.
    bool feed(const char* data, std::size_t size);
    bool feed(std::string_view chunk);

    bool is_complete() const { return state_ == STATE_COMPLETE; }
    bool has_error() const { return state_ == STATE_ERROR; }
    State state() const { return state_; }

    // Returns the parsed Request, or std::nullopt until parsing is complete.
    std::optional<Request> get_request() const;

    // Clears all state so the parser can be reused for another request.
    void reset();

private:
    void consume_char(char c);
    void append_to_token(char c);
    bool finished_token_is_valid() const;

    State state_ = STATE_METHOD;
    std::string method_;
    std::string path_;
    std::string version_;
};

// Parses a complete request held in a single string.
std::optional<Request> parse_request(const std::string& raw);

} // namespace atlas
