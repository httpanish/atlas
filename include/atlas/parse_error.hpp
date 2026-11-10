#pragma once

namespace atlas {

// Why a request was rejected. Shared by RequestParser and CommentPda.
enum class ParseError {
    None,
    // All of these map to 400 Bad Request.
    BadMethod,
    BadTarget,
    BadVersion,
    BadLineEnding,  // CR without LF, bare LF, or a space before ':' in a header name
    BadHeaderName,
    BadHeaderValue,
    UnbalancedComment,
    CommentTooDeep,
    MissingHost,
    DuplicateHost,
    BadContentLength,
    ConflictingContentLength,
    // 414 URI Too Long
    RequestLineTooLong,
    // 431 Request Header Fields Too Large
    HeaderLineTooLong,
    TooManyHeaders,
    HeadersTooLarge,
    // 413 Content Too Large
    BodyTooLarge,
    // 501 Not Implemented
    TransferEncodingNotSupported,
    // 505 HTTP Version Not Supported
    UnsupportedVersion,
};

// The HTTP status code to answer with for a given error.
int status_for(ParseError e);

} // namespace atlas
