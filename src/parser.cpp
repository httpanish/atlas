#include "atlas"
#include <array>

namespace atlas {

// ============================================================================
// 1. Character-Class Lookup Table (LUT)
// ============================================================================
enum CharClass {
    CHAR_OTHER = 0,  // Regular characters (letters, digits, '/', '.', etc.)
    CHAR_SPACE = 1,  // Space ' '
    CHAR_CR    = 2,  // Carriage return '\r'
    CHAR_LF    = 3   // Line feed '\n'
};

// 256-entry lookup table mapping every possible byte (0..255) to a CharClass
static const std::array<unsigned char, 256> char_classes = [] {
    std::array<unsigned char, 256> table{};
    table.fill(CHAR_OTHER);
    table[static_cast<unsigned char>(' ')]  = CHAR_SPACE;
    table[static_cast<unsigned char>('\r')] = CHAR_CR;
    table[static_cast<unsigned char>('\n')] = CHAR_LF;
    return table;
}();

// ============================================================================
// 2. Finite State Machine Transitions (LUT)
// ============================================================================
// transitions[current_state][char_class] -> next_state
static const int transitions[10][4] = {
    //                               CHAR_OTHER                     CHAR_SPACE                     CHAR_CR                                CHAR_LF
    /* 0: STATE_METHOD          */ { HttpParser::STATE_METHOD,      HttpParser::STATE_PATH,        HttpParser::STATE_ERROR,               HttpParser::STATE_ERROR },
    /* 1: STATE_PATH            */ { HttpParser::STATE_PATH,        HttpParser::STATE_VERSION,     HttpParser::STATE_ERROR,               HttpParser::STATE_ERROR },
    /* 2: STATE_VERSION         */ { HttpParser::STATE_VERSION,     HttpParser::STATE_ERROR,       HttpParser::STATE_REQ_LINE_CR,         HttpParser::STATE_HEADER_START },
    /* 3: STATE_REQ_LINE_CR     */ { HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,               HttpParser::STATE_HEADER_START },
    /* 4: STATE_HEADER_START    */ { HttpParser::STATE_HEADER_LINE, HttpParser::STATE_HEADER_LINE, HttpParser::STATE_EMPTY_LINE_CR,       HttpParser::STATE_COMPLETE },
    /* 5: STATE_HEADER_LINE     */ { HttpParser::STATE_HEADER_LINE, HttpParser::STATE_HEADER_LINE, HttpParser::STATE_HEADER_LINE_CR,      HttpParser::STATE_HEADER_START },
    /* 6: STATE_HEADER_LINE_CR  */ { HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,               HttpParser::STATE_HEADER_START },
    /* 7: STATE_EMPTY_LINE_CR   */ { HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,               HttpParser::STATE_COMPLETE },
    /* 8: STATE_COMPLETE        */ { HttpParser::STATE_COMPLETE,    HttpParser::STATE_COMPLETE,    HttpParser::STATE_COMPLETE,            HttpParser::STATE_COMPLETE },
    /* 9: STATE_ERROR           */ { HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,       HttpParser::STATE_ERROR,               HttpParser::STATE_ERROR }
};

// ============================================================================
// 3. Streaming HttpParser Implementation
// ============================================================================

void HttpParser::consume_char(char c) {
    if (state_ == STATE_COMPLETE || state_ == STATE_ERROR) {
        return;
    }

    // Step 1: Character classification via 256-entry LUT
    unsigned char char_class = char_classes[static_cast<unsigned char>(c)];

    // Step 2: State transition via 2D Transition LUT
    int next_state = transitions[state_][char_class];

    // Step 3: Accumulate tokens during valid states
    if (state_ == STATE_METHOD && char_class == CHAR_OTHER) {
        method_ += c;
    } else if (state_ == STATE_PATH && char_class == CHAR_OTHER) {
        path_ += c;
    } else if (state_ == STATE_VERSION && char_class == CHAR_OTHER) {
        version_ += c;
    }

    // Step 4: Semantic validation on state transitions
    if (state_ == STATE_METHOD && next_state == STATE_PATH) {
        if (method_ != "GET") {
            next_state = STATE_ERROR;
        }
    } else if (state_ == STATE_PATH && next_state == STATE_VERSION) {
        if (path_.empty() || path_.front() != '/') {
            next_state = STATE_ERROR;
        }
    } else if (state_ == STATE_VERSION && (next_state == STATE_REQ_LINE_CR || next_state == STATE_HEADER_START)) {
        if (version_ != "HTTP/1.1" && version_ != "HTTP/1.0") {
            next_state = STATE_ERROR;
        }
    }

    state_ = static_cast<State>(next_state);
}

bool HttpParser::feed(const char* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        consume_char(data[i]);
        if (state_ == STATE_ERROR) {
            return false;
        }
    }
    return true;
}

bool HttpParser::feed(std::string_view chunk) {
    return feed(chunk.data(), chunk.size());
}

bool HttpParser::is_complete() const {
    return state_ == STATE_COMPLETE;
}

bool HttpParser::has_error() const {
    return state_ == STATE_ERROR;
}

HttpParser::State HttpParser::state() const {
    return state_;
}

std::optional<Request> HttpParser::get_request() const {
    if (state_ != STATE_COMPLETE) {
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
