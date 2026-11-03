#include "atlas/parser.hpp"

#include <array>

namespace atlas {

namespace {

using State = HttpParser::State;

// ----------------------------------------------------------------------------
// 1. Character classes: every byte maps to one of these columns.
// ----------------------------------------------------------------------------
enum CharClass : unsigned char {
    CHAR_OTHER,  // letters, digits, '/', '.', ':', ...
    CHAR_SPACE,  // ' '
    CHAR_CR,     // '\r'
    CHAR_LF,     // '\n'
};

constexpr std::size_t kCharClassCount = CHAR_LF + 1;
constexpr std::size_t kStateCount = HttpParser::STATE_ERROR + 1;

constexpr auto kCharClasses = [] {
    std::array<CharClass, 256> table{};
    table.fill(CHAR_OTHER);
    table[' '] = CHAR_SPACE;
    table['\r'] = CHAR_CR;
    table['\n'] = CHAR_LF;
    return table;
}();

// ----------------------------------------------------------------------------
// 2. Transitions: kTransitions[current_state][char_class] -> next_state
// ----------------------------------------------------------------------------
using TransitionTable = std::array<std::array<State, kCharClassCount>, kStateCount>;

constexpr TransitionTable kTransitions = [] {
    using enum HttpParser::State;
    return TransitionTable{{
        //                            OTHER               SPACE               CR                    LF
        /* STATE_METHOD         */ {{ STATE_METHOD,      STATE_PATH,         STATE_ERROR,          STATE_ERROR        }},
        /* STATE_PATH           */ {{ STATE_PATH,        STATE_VERSION,      STATE_ERROR,          STATE_ERROR        }},
        /* STATE_VERSION        */ {{ STATE_VERSION,     STATE_ERROR,        STATE_REQ_LINE_CR,    STATE_HEADER_START }},
        /* STATE_REQ_LINE_CR    */ {{ STATE_ERROR,       STATE_ERROR,        STATE_ERROR,          STATE_HEADER_START }},
        /* STATE_HEADER_START   */ {{ STATE_HEADER_LINE, STATE_HEADER_LINE,  STATE_EMPTY_LINE_CR,  STATE_COMPLETE     }},
        /* STATE_HEADER_LINE    */ {{ STATE_HEADER_LINE, STATE_HEADER_LINE,  STATE_HEADER_LINE_CR, STATE_HEADER_START }},
        /* STATE_HEADER_LINE_CR */ {{ STATE_ERROR,       STATE_ERROR,        STATE_ERROR,          STATE_HEADER_START }},
        /* STATE_EMPTY_LINE_CR  */ {{ STATE_ERROR,       STATE_ERROR,        STATE_ERROR,          STATE_COMPLETE     }},
        /* STATE_COMPLETE       */ {{ STATE_COMPLETE,    STATE_COMPLETE,     STATE_COMPLETE,       STATE_COMPLETE     }},
        /* STATE_ERROR          */ {{ STATE_ERROR,       STATE_ERROR,        STATE_ERROR,          STATE_ERROR        }},
    }};
}();

} // namespace

// ----------------------------------------------------------------------------
// 3. Parser
// ----------------------------------------------------------------------------

void HttpParser::consume_char(char c) {
    if (is_complete() || has_error()) {
        return;
    }

    const CharClass char_class = kCharClasses[static_cast<unsigned char>(c)];
    State next = kTransitions[state_][char_class];

    if (char_class == CHAR_OTHER) {
        append_to_token(c);
    }

    // Moving to a new state means the current token (method, path, version)
    // is finished, so this is the moment to check that it is acceptable.
    const bool leaving_state = next != state_ && next != STATE_ERROR;
    if (leaving_state && !finished_token_is_valid()) {
        next = STATE_ERROR;
    }

    state_ = next;
}

void HttpParser::append_to_token(char c) {
    switch (state_) {
        case STATE_METHOD:  method_ += c;  break;
        case STATE_PATH:    path_ += c;    break;
        case STATE_VERSION: version_ += c; break;
        default: break;  // header bytes are not stored yet
    }
}

bool HttpParser::finished_token_is_valid() const {
    switch (state_) {
        case STATE_METHOD:  return method_ == "GET";
        case STATE_PATH:    return path_.starts_with('/');
        case STATE_VERSION: return version_ == "HTTP/1.1" || version_ == "HTTP/1.0";
        default:            return true;
    }
}

bool HttpParser::feed(const char* data, std::size_t size) {
    for (std::size_t i = 0; i < size && !has_error(); ++i) {
        consume_char(data[i]);
    }
    return !has_error();
}

bool HttpParser::feed(std::string_view chunk) {
    return feed(chunk.data(), chunk.size());
}

std::optional<Request> HttpParser::get_request() const {
    if (!is_complete()) {
        return std::nullopt;
    }
    return Request(method_, path_);
}

void HttpParser::reset() {
    state_ = STATE_METHOD;
    method_.clear();
    path_.clear();
    version_.clear();
}

std::optional<Request> parse_request(const std::string& raw) {
    HttpParser parser;
    parser.feed(raw);
    return parser.get_request();
}

} // namespace atlas
