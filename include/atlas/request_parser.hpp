#pragma once

#include "atlas/comment_pda.hpp"
#include "atlas/parse_error.hpp"
#include "atlas/request.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace atlas {

// A streaming HTTP/1.x request parser (strict RFC 9112).
//
// Feed it bytes in chunks of any size, even one byte at a time, and check
// status() after each feed():
//
//   RequestParser parser;
//   parser.feed("GET /hello HTTP/1.1\r\nHost: x\r\n\r\n");
//   if (parser.status() == RequestParser::Status::Complete) {
//       Request req = parser.take_request();
//   }
//
// Inside it is a finite state machine with one `case` per State:
//   - Mealy: each transition can run an action (append a byte, commit a header).
//   - Moore: each state has one fixed Status, and status() reads only the state.
class RequestParser {
public:
    enum class Status { NeedMore, Complete, Error };

    // Consumes bytes until the request is complete, an error occurs, or the
    // input runs out. Returns how many bytes were consumed:
    //   - on Complete it stops right after the last body byte; the rest
    //     belongs to the next request.
    //   - on Error it stops *at* the bad byte, which isn't counted.
    //   - once Complete or Error, it returns 0 until reset().
    std::size_t feed(std::string_view bytes);

    Status status() const;
    ParseError error() const { return error_; }  // meaningful only on Error

    // Moves the finished Request out. Call only when status() == Complete.
    Request take_request();

    // Clears everything so the parser can parse another request.
    // (Assigning a freshly constructed parser resets every member to its
    // default in one line, so no field can be forgotten.)
    void reset() { *this = RequestParser{}; }

private:
    enum class State {
        Method,
        Target,
        Version,
        RequestLineLF,
        HeaderStart,
        HeaderName,
        BeforeValue,
        Value,
        HeaderLineLF,
        HeadersEndLF,
        Body,
        Done,
        Error,
    };

    void step(char c);  // runs one byte through the request line / header states
    bool count_byte_against_limits();
    void validate_version();
    void commit_header();
    void finish_headers();
    void fail(ParseError e);

    State state_ = State::Method;
    ParseError error_ = ParseError::None;

    // Pieces of the request, filled in as bytes arrive.
    std::string method_;
    std::string target_;
    std::string version_;
    std::string name_;   // header name being read (lowercased)
    std::string value_;  // header value being read
    std::string body_;
    Request::Headers headers_;
    Request::CommentedHeaders commented_;

    // Runs alongside Value for headers that may contain comments.
    CommentPda pda_;
    bool pda_active_ = false;

    // Counters for the size limits.
    std::size_t request_line_bytes_ = 0;
    std::size_t header_line_bytes_ = 0;
    std::size_t header_bytes_ = 0;
    std::size_t header_count_ = 0;
    std::size_t body_remaining_ = 0;
};

} // namespace atlas
