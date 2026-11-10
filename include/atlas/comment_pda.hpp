#pragma once

#include "atlas/parse_error.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace atlas {

// A pushdown automaton that splits a header value into words and comments.
//
// RFC 9110 comments nest:   comment = "(" *( ctext / quoted-pair / comment ) ")"
// A finite machine can't match parentheses to any depth, so this one keeps a
// stack: push on '(', pop on ')', and accept only if the stack ends up empty.
//
//   CommentPda pda;
//   for (char c : std::string_view("Foo/1.0 (X11; (n) L)")) pda.feed(c);
//   pda.finish();   // true
//   pda.words();    // {"Foo/1.0"}
//   pda.comments(); // {"X11; (n) L"}
class CommentPda {
public:
    // Feeds one byte of a header value (leading whitespace already stripped).
    // Returns false on error; error() then says why.
    bool feed(char c);

    // Call at the end of the value. Fails if a comment is still open or a
    // backslash escape is unfinished.
    bool finish();

    ParseError error() const { return error_; }
    const std::vector<std::string>& words() const { return words_; }
    const std::vector<std::string>& comments() const { return comments_; }

    void reset();

private:
    enum class State { Outside, InComment, Escape, Failed };

    // One open '(' on the stack. `start` is the offset (in value_) of the byte
    // just after it, so the comment text can be cut out when it closes.
    struct Frame {
        std::size_t start;
    };

    bool fail(ParseError e);
    void end_word();

    State state_ = State::Outside;
    ParseError error_ = ParseError::None;
    std::vector<Frame> stack_;
    std::string value_;          // every byte fed so far
    std::string current_word_;   // the word being built while Outside
    std::vector<std::string> words_;
    std::vector<std::string> comments_;
};

} // namespace atlas
