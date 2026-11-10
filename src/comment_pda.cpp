#include "atlas/comment_pda.hpp"

#include "char_class.hpp"

namespace atlas {

namespace {

// How deeply comments may nest: "(((...)))" with more than 8 open at once
// is rejected, so the stack can't grow without bound.
constexpr std::size_t kMaxCommentDepth = 8;  // -> CommentTooDeep (400)

} // namespace

bool CommentPda::fail(ParseError e) {
    state_ = State::Failed;
    error_ = e;
    return false;
}

// A word ends at whitespace, a comma, a '(' or the end of the value.
void CommentPda::end_word() {
    if (!current_word_.empty()) {
        words_.push_back(std::move(current_word_));
        current_word_.clear();
    }
}

bool CommentPda::feed(char ch) {
    const auto c = static_cast<unsigned char>(ch);
    if (state_ == State::Failed) {
        return false;
    }
    value_ += ch;

    switch (state_) {
        case State::Outside:
            if (c == '(') {
                end_word();
                stack_.push_back({value_.size()});  // the comment starts after '('
                state_ = State::InComment;
            } else if (c == ')') {
                return fail(ParseError::UnbalancedComment);
            } else if (chars::is_space_or_tab(c) || c == ',') {
                end_word();
            } else if (chars::is_field_vchar(c)) {
                current_word_ += ch;
            } else {
                return fail(ParseError::BadHeaderValue);
            }
            return true;

        case State::InComment:
            if (c == '(') {
                if (stack_.size() >= kMaxCommentDepth) {
                    return fail(ParseError::CommentTooDeep);
                }
                stack_.push_back({value_.size()});
            } else if (c == ')') {
                const Frame closed = stack_.back();
                stack_.pop_back();
                if (stack_.empty()) {
                    // The outermost comment just closed. Its text is the raw
                    // bytes between its '(' and this ')', nesting included.
                    const std::size_t end = value_.size() - 1;  // offset of this ')'
                    comments_.push_back(value_.substr(closed.start, end - closed.start));
                    state_ = State::Outside;
                }
            } else if (c == '\\') {
                state_ = State::Escape;
            } else if (chars::is_field_vchar(c) || chars::is_space_or_tab(c)) {
                // ctext: stays in the comment, no stack change.
            } else {
                return fail(ParseError::BadHeaderValue);
            }
            return true;

        case State::Escape:
            // The escaped byte is taken literally, so "\(" doesn't push.
            if (chars::is_field_vchar(c) || chars::is_space_or_tab(c)) {
                state_ = State::InComment;
                return true;
            }
            return fail(ParseError::BadHeaderValue);

        case State::Failed:
            break;
    }
    return false;
}

bool CommentPda::finish() {
    if (state_ == State::Failed) {
        return false;
    }
    end_word();
    // InComment or Escape means a '(' was never closed: the stack isn't empty.
    if (state_ != State::Outside) {
        return fail(ParseError::UnbalancedComment);
    }
    return true;
}

// Assigning a fresh CommentPda resets every member to its default at once.
void CommentPda::reset() { *this = CommentPda{}; }

} // namespace atlas
