#pragma once

// Character classes from RFC 9110 §5.6.2 and §5.5, shared by the parser and
// the comment PDA. Private to src/ (not part of the public headers).
//
// They take `unsigned char` so bytes 0x80-0xFF compare as 128-255 rather
// than as negative numbers (plain `char` is signed on most platforms).

namespace atlas::chars {

inline bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

inline bool is_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Characters allowed in a token, such as a method or a header name.
inline bool is_tchar(unsigned char c) {
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return is_digit(c) || is_alpha(c);
    }
}

// Visible ASCII: everything printable except space.
inline bool is_vchar(unsigned char c) { return c >= 0x21 && c <= 0x7E; }

// Bytes 0x80-0xFF, allowed in header values for historical reasons.
inline bool is_obs_text(unsigned char c) { return c >= 0x80; }

inline bool is_field_vchar(unsigned char c) { return is_vchar(c) || is_obs_text(c); }

inline bool is_space_or_tab(unsigned char c) { return c == ' ' || c == '\t'; }

inline char to_lower(unsigned char c) {
    return is_alpha(c) && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
}

} // namespace atlas::chars
