#include "atlas/request_parser.hpp"

#include "char_class.hpp"

#include <algorithm>
#include <cstdint>

namespace atlas {

namespace {

// Hard limits, so hostile input can't make memory grow without bound.
// They're checked as each byte arrives: a 10 GB request line is rejected
// after 8 KiB, not after it has all been buffered.
constexpr std::size_t kMaxRequestLine = 8 * 1024;    // -> RequestLineTooLong (414)
constexpr std::size_t kMaxHeaderLine = 8 * 1024;     // -> HeaderLineTooLong  (431)
constexpr std::size_t kMaxHeaderCount = 100;         // -> TooManyHeaders     (431)
constexpr std::size_t kMaxHeaderBytes = 64 * 1024;   // -> HeadersTooLarge    (431)
constexpr std::size_t kMaxBody = 1024 * 1024;        // -> BodyTooLarge       (413)

constexpr char CR = '\r';
constexpr char LF = '\n';
constexpr char SP = ' ';

// Headers whose values may contain (comments), so they go through CommentPda.
bool has_comments(const std::string& name) { return name == "user-agent" || name == "via"; }

} // namespace

int status_for(ParseError e) {
    // Every value is listed (no `default:`), so the compiler warns if a new
    // error is added without a status code.
    switch (e) {
        case ParseError::None:  // not an error; 400 is just a safe answer
        case ParseError::BadMethod:
        case ParseError::BadTarget:
        case ParseError::BadVersion:
        case ParseError::BadLineEnding:
        case ParseError::BadHeaderName:
        case ParseError::BadHeaderValue:
        case ParseError::UnbalancedComment:
        case ParseError::CommentTooDeep:
        case ParseError::MissingHost:
        case ParseError::DuplicateHost:
        case ParseError::BadContentLength:
        case ParseError::ConflictingContentLength:
            return 400;
        case ParseError::RequestLineTooLong:
            return 414;
        case ParseError::HeaderLineTooLong:
        case ParseError::TooManyHeaders:
        case ParseError::HeadersTooLarge:
            return 431;
        case ParseError::BodyTooLarge:
            return 413;
        case ParseError::TransferEncodingNotSupported:
            return 501;
        case ParseError::UnsupportedVersion:
            return 505;
    }
    return 400;  // unreachable, but some compilers want a return here
}

// Moore output: the status depends only on the current state.
RequestParser::Status RequestParser::status() const {
    switch (state_) {
        case State::Done:
            return Status::Complete;
        case State::Error:
            return Status::Error;
        default:
            return Status::NeedMore;
    }
}

std::size_t RequestParser::feed(std::string_view bytes) {
    std::size_t used = 0;
    while (used < bytes.size() && state_ != State::Done && state_ != State::Error) {
        if (state_ == State::Body) {
            // The body has no grammar, only a length, so copy as much as we
            // can in one go instead of stepping byte by byte.
            const std::size_t n = std::min(body_remaining_, bytes.size() - used);
            body_.append(bytes.substr(used, n));
            used += n;
            body_remaining_ -= n;
            if (body_remaining_ == 0) {
                state_ = State::Done;
            }
            continue;
        }

        step(bytes[used]);
        if (state_ == State::Error) {
            break;  // the bad byte is not counted as consumed
        }
        ++used;
    }
    return used;
}

Request RequestParser::take_request() {
    return Request(std::move(method_), std::move(target_), std::move(version_),
                   std::move(headers_), std::move(commented_), std::move(body_));
}

void RequestParser::fail(ParseError e) {
    state_ = State::Error;
    error_ = e;
}

// Counts the byte that's about to be processed against the size limits.
// Returns false (and fails the parser) if a limit is exceeded.
bool RequestParser::count_byte_against_limits() {
    switch (state_) {
        case State::Method:
        case State::Target:
        case State::Version:
        case State::RequestLineLF:
            if (++request_line_bytes_ > kMaxRequestLine) {
                fail(ParseError::RequestLineTooLong);
                return false;
            }
            return true;
        default:  // a header state
            if (++header_line_bytes_ > kMaxHeaderLine) {
                fail(ParseError::HeaderLineTooLong);
                return false;
            }
            if (++header_bytes_ > kMaxHeaderBytes) {
                fail(ParseError::HeadersTooLarge);
                return false;
            }
            return true;
    }
}

// One transition of the state machine. Each `case` is one state: it looks at
// the byte, runs that transition's action (the Mealy part), and picks the next
// state. Any byte a state doesn't expect is an error.
void RequestParser::step(char ch) {
    const auto c = static_cast<unsigned char>(ch);
    if (!count_byte_against_limits()) {
        return;
    }

    switch (state_) {
        // ---- Request line: METHOD SP TARGET SP VERSION CRLF ----------------
        case State::Method:
            if (chars::is_tchar(c)) {
                method_ += ch;
            } else if (c == SP && !method_.empty()) {
                state_ = State::Target;
            } else if (c == LF || c == CR) {
                fail(ParseError::BadLineEnding);  // the line ended too early
            } else {
                fail(ParseError::BadMethod);
            }
            break;

        case State::Target:
            if (target_.empty() && c != '/') {
                fail(ParseError::BadTarget);  // only origin-form ("/path") is supported
            } else if (c == SP) {
                state_ = State::Version;
            } else if (chars::is_vchar(c)) {
                target_ += ch;
            } else if (c == LF || c == CR) {
                fail(ParseError::BadLineEnding);  // the line ended too early
            } else {
                fail(ParseError::BadTarget);
            }
            break;

        case State::Version:
            if (c == CR) {
                validate_version();
                if (state_ != State::Error) {
                    state_ = State::RequestLineLF;
                }
            } else if (c == LF) {
                fail(ParseError::BadLineEnding);
            } else if (chars::is_vchar(c)) {
                version_ += ch;
            } else {
                fail(ParseError::BadVersion);
            }
            break;

        case State::RequestLineLF:
            if (c == LF) {
                state_ = State::HeaderStart;
            } else {
                fail(ParseError::BadLineEnding);
            }
            break;

        // ---- Headers: NAME ":" OWS VALUE OWS CRLF, then an empty line ------
        case State::HeaderStart:
            if (c == CR) {
                state_ = State::HeadersEndLF;  // empty line: end of headers
            } else if (chars::is_tchar(c)) {
                name_ += chars::to_lower(c);
                state_ = State::HeaderName;
            } else if (c == LF) {
                fail(ParseError::BadLineEnding);
            } else {
                // Includes a line starting with SP/HTAB (obsolete line folding).
                fail(ParseError::BadHeaderName);
            }
            break;

        case State::HeaderName:
            if (chars::is_tchar(c)) {
                name_ += chars::to_lower(c);
            } else if (c == ':') {
                pda_active_ = has_comments(name_);
                state_ = State::BeforeValue;
            } else if (chars::is_space_or_tab(c)) {
                fail(ParseError::BadLineEnding);  // "Host : x" is not allowed
            } else {
                fail(ParseError::BadHeaderName);
            }
            break;

        case State::BeforeValue:
        case State::Value:
            if (c == CR) {
                state_ = State::HeaderLineLF;
            } else if (c == LF) {
                fail(ParseError::BadLineEnding);
            } else if (chars::is_space_or_tab(c) && state_ == State::BeforeValue) {
                // Skip leading whitespace: it's never appended.
            } else if (chars::is_field_vchar(c) || chars::is_space_or_tab(c)) {
                value_ += ch;
                state_ = State::Value;
                if (pda_active_ && !pda_.feed(ch)) {
                    fail(pda_.error());
                }
            } else {
                fail(ParseError::BadHeaderValue);  // NUL or another control byte
            }
            break;

        case State::HeaderLineLF:
            if (c == LF) {
                commit_header();
                if (state_ != State::Error) {
                    state_ = State::HeaderStart;
                }
            } else {
                fail(ParseError::BadLineEnding);
            }
            break;

        case State::HeadersEndLF:
            if (c == LF) {
                finish_headers();  // moves on to Body, Done or Error
            } else {
                fail(ParseError::BadLineEnding);
            }
            break;

        // feed() handles Body itself, and never calls step() once finished.
        case State::Body:
        case State::Done:
        case State::Error:
            break;
    }
}

// The version must look like HTTP/D.D, and only 1.0 and 1.1 are supported.
void RequestParser::validate_version() {
    const std::string& v = version_;
    const bool well_formed = v.size() == 8 && v.starts_with("HTTP/") &&
                             chars::is_digit(v[5]) && v[6] == '.' && chars::is_digit(v[7]);
    if (!well_formed) {
        fail(ParseError::BadVersion);
    } else if (v != "HTTP/1.0" && v != "HTTP/1.1") {
        fail(ParseError::UnsupportedVersion);
    }
}

// Called on the LF that ends a header line: stores name_ / value_.
void RequestParser::commit_header() {
    // Trailing whitespace was appended (we couldn't know it was trailing yet),
    // so trim it now. Leading whitespace was never appended.
    while (!value_.empty() && chars::is_space_or_tab(value_.back())) {
        value_.pop_back();
    }

    if (pda_active_ && !pda_.finish()) {
        fail(pda_.error());
        return;
    }

    auto existing = headers_.find(name_);
    if (existing == headers_.end()) {
        headers_.emplace(name_, value_);
    } else if (name_ == "content-length") {
        // Repeating the same length is harmless; two different ones are a
        // classic request-smuggling trick.
        if (existing->second != value_) {
            fail(ParseError::ConflictingContentLength);
            return;
        }
    } else if (name_ == "host") {
        fail(ParseError::DuplicateHost);
        return;
    } else {
        existing->second += ", " + value_;  // RFC 9110 §5.3: combine with a comma
    }

    if (pda_active_) {
        CommentedValue& cv = commented_[name_];
        cv.words.insert(cv.words.end(), pda_.words().begin(), pda_.words().end());
        cv.comments.insert(cv.comments.end(), pda_.comments().begin(), pda_.comments().end());
        pda_.reset();
        pda_active_ = false;
    }

    if (++header_count_ > kMaxHeaderCount) {
        fail(ParseError::TooManyHeaders);
        return;
    }

    name_.clear();
    value_.clear();
    header_line_bytes_ = 0;
}

// Called on the LF of the empty line: decides whether a body follows.
void RequestParser::finish_headers() {
    if (version_ == "HTTP/1.1" && !headers_.contains("host")) {
        fail(ParseError::MissingHost);
        return;
    }
    if (headers_.contains("transfer-encoding")) {
        fail(ParseError::TransferEncodingNotSupported);
        return;
    }

    std::uint64_t length = 0;
    if (auto it = headers_.find("content-length"); it != headers_.end()) {
        const std::string& digits = it->second;
        // Exactly 1-19 digits: no sign, no spaces, no commas. 19 decimal
        // digits always fit in a uint64_t, so the loop below can't overflow
        // (std::size_t isn't 64 bits on every platform, hence uint64_t).
        if (digits.empty() || digits.size() > 19 ||
            !std::all_of(digits.begin(), digits.end(),
                         [](unsigned char d) { return chars::is_digit(d); })) {
            fail(ParseError::BadContentLength);
            return;
        }
        for (char d : digits) {
            length = length * 10 + static_cast<std::uint64_t>(d - '0');
        }
        if (length > kMaxBody) {
            fail(ParseError::BodyTooLarge);
            return;
        }
    }

    // Safe to narrow: length is at most kMaxBody (1 MiB) here.
    body_remaining_ = static_cast<std::size_t>(length);
    state_ = length > 0 ? State::Body : State::Done;
}

} // namespace atlas
