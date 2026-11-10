#include <atlas/atlas.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace std::string_literals;  // "..."s keeps embedded NUL bytes
using atlas::ParseError;
using atlas::Request;
using Status = atlas::RequestParser::Status;

namespace {

int failures = 0;

// Like CHECK in test_basic.cpp, but keeps going so one run shows every failure.
#define EXPECT(expr, context)                                                   \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": [" << (context)      \
                      << "] EXPECT failed: " #expr "\n";                        \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

// ----------------------------------------------------------------------------
// Running one input three ways
// ----------------------------------------------------------------------------

// Everything we can observe after feeding some input.
struct Outcome {
    Status status = Status::NeedMore;
    ParseError error = ParseError::None;
    std::size_t consumed = 0;        // total of all feed() return values
    std::optional<Request> request;  // set when status == Complete
};

// Feeds `input` cut at each offset in `cuts` (which must be increasing).
Outcome run(const std::string& input, const std::vector<std::size_t>& cuts) {
    atlas::RequestParser parser;
    Outcome out;
    std::size_t from = 0;
    auto feed_up_to = [&](std::size_t to) {
        out.consumed += parser.feed(std::string_view(input).substr(from, to - from));
        from = to;
    };
    for (std::size_t cut : cuts) {
        feed_up_to(cut);
    }
    feed_up_to(input.size());

    out.status = parser.status();
    out.error = parser.error();
    if (out.status == Status::Complete) {
        out.request = parser.take_request();
    }
    return out;
}

// The streaming check: these ways of feeding must all give the same result.
//   1. the whole input in one feed()
//   2. one byte per feed()
//   3. for every split point i, feed(input[0..i]) then feed(input[i..])
// Returns the whole-input outcome, after checking that the others match it.
Outcome run_all_ways(const std::string& name, const std::string& input) {
    const Outcome whole = run(input, {});

    auto same_as_whole = [&](const Outcome& other, const std::string& how) {
        const std::string context = name + ", " + how;
        EXPECT(other.status == whole.status, context);
        EXPECT(other.error == whole.error, context);
        EXPECT(other.consumed == whole.consumed, context);
        if (whole.request && other.request) {
            EXPECT(other.request->method() == whole.request->method(), context);
            EXPECT(other.request->path() == whole.request->path(), context);
            EXPECT(other.request->version() == whole.request->version(), context);
            EXPECT(other.request->headers() == whole.request->headers(), context);
            EXPECT(other.request->body() == whole.request->body(), context);
            for (const char* name : {"user-agent", "via"}) {
                const atlas::CommentedValue* a = whole.request->commented(name);
                const atlas::CommentedValue* b = other.request->commented(name);
                EXPECT((a == nullptr) == (b == nullptr), context);
                if (a && b) {
                    EXPECT(a->words == b->words && a->comments == b->comments, context);
                }
            }
        }
    };

    std::vector<std::size_t> every_byte;
    for (std::size_t i = 1; i < input.size(); ++i) {
        every_byte.push_back(i);
    }
    same_as_whole(run(input, every_byte), "byte by byte");

    const int failures_before = failures;
    for (std::size_t i = 1; i < input.size(); ++i) {
        const Outcome split = run(input, {i});
        same_as_whole(split, "split at " + std::to_string(i));
        if (failures > failures_before) {
            break;  // one failing split point is enough to show the bug
        }
    }
    return whole;
}

// ----------------------------------------------------------------------------
// Table-driven cases
// ----------------------------------------------------------------------------

struct Case {
    std::string name;
    std::string input;
    Status status;
    ParseError error = ParseError::None;
    // Extra checks on the finished Request (only when status == Complete).
    std::function<void(const Request&, const std::string& name)> check = nullptr;
};

void run_cases(const std::vector<Case>& cases) {
    for (const Case& c : cases) {
        const Outcome out = run_all_ways(c.name, c.input);
        EXPECT(out.status == c.status, c.name);
        EXPECT(out.error == c.error, c.name);
        if (c.check && out.request) {
            c.check(*out.request, c.name);
        }
    }
}

void test_happy_path() {
    run_cases({
        {"GET 1.0 no headers", "GET / HTTP/1.0\r\n\r\n", Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.method() == "GET", n);
             EXPECT(r.path() == "/", n);
             EXPECT(r.version() == "HTTP/1.0", n);
             EXPECT(r.headers().empty(), n);
             EXPECT(r.body().empty(), n);
         }},
        {"GET 1.1 with Host", "GET /hello?x=1 HTTP/1.1\r\nHost: example.com\r\n\r\n",
         Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.path() == "/hello?x=1", n);  // raw target, query included
             EXPECT(r.version() == "HTTP/1.1", n);
             EXPECT(r.header("host") == "example.com", n);
         }},
        {"1.0 without Host is fine", "GET / HTTP/1.0\r\nAccept: */*\r\n\r\n", Status::Complete},
    });
}

void test_request_line() {
    run_cases({
        {"lowercase method is a valid token", "get / HTTP/1.0\r\n\r\n", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) { EXPECT(r.method() == "get", n); }},
        {"empty method", " / HTTP/1.0\r\n\r\n", Status::Error, ParseError::BadMethod},
        {"double space", "GET  / HTTP/1.0\r\n\r\n", Status::Error, ParseError::BadTarget},
        {"target without /", "GET hello HTTP/1.0\r\n\r\n", Status::Error, ParseError::BadTarget},
        {"HTTP/2.0", "GET / HTTP/2.0\r\n\r\n", Status::Error, ParseError::UnsupportedVersion},
        {"HTTP/1.x", "GET / HTTP/1.x\r\n\r\n", Status::Error, ParseError::BadVersion},
        {"bare LF", "GET / HTTP/1.0\n\n", Status::Error, ParseError::BadLineEnding},
        {"bare LF after method", "GET\n", Status::Error, ParseError::BadLineEnding},
        {"bare LF after target", "GET /\n", Status::Error, ParseError::BadLineEnding},
        {"CR without LF", "GET / HTTP/1.0\rX", Status::Error, ParseError::BadLineEnding},
        {"request line over 8 KiB", "GET /" + std::string(9000, 'a') + " HTTP/1.0\r\n\r\n",
         Status::Error, ParseError::RequestLineTooLong},
        {"incomplete", "GET / HTTP/1.1\r\nHost: x\r\n", Status::NeedMore},
    });
}

void test_headers() {
    std::string many_headers = "GET / HTTP/1.0\r\n";
    for (int i = 0; i < 101; ++i) {
        many_headers += "X-" + std::to_string(i) + ": v\r\n";
    }
    many_headers += "\r\n";

    run_cases({
        {"case-insensitive lookup", "GET / HTTP/1.0\r\nContent-TYPE: text/plain\r\n\r\n",
         Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.header("content-type") == "text/plain", n);
             EXPECT(r.header("CONTENT-TYPE") == "text/plain", n);
             EXPECT(r.headers().contains("content-type"), n);  // stored lowercase
             EXPECT(!r.header("missing").has_value(), n);
         }},
        {"OWS trimmed on both sides", "GET / HTTP/1.0\r\nX-A: \t a b \t \r\n\r\n",
         Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) { EXPECT(r.header("x-a") == "a b", n); }},
        {"empty value", "GET / HTTP/1.0\r\nX-A:\r\nX-B:   \r\n\r\n", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.header("x-a") == "", n);
             EXPECT(r.header("x-b") == "", n);
         }},
        {"space before colon", "GET / HTTP/1.0\r\nX-A : v\r\n\r\n", Status::Error,
         ParseError::BadLineEnding},
        {"obs-fold", "GET / HTTP/1.0\r\nX-A: v\r\n  more\r\n\r\n", Status::Error,
         ParseError::BadHeaderName},
        {"NUL in value", "GET / HTTP/1.0\r\nX-A: a\0b\r\n\r\n"s, Status::Error,
         ParseError::BadHeaderValue},
        {"obs-text in value", "GET / HTTP/1.0\r\nX-A: caf\xC3\xA9\r\n\r\n", Status::Complete},
        {"duplicates merged", "GET / HTTP/1.0\r\nX-A: 1\r\nx-a: 2\r\n\r\n", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) { EXPECT(r.header("x-a") == "1, 2", n); }},
        {"101 headers", many_headers, Status::Error, ParseError::TooManyHeaders},
        {"header line over 8 KiB", "GET / HTTP/1.0\r\nX-A: " + std::string(9000, 'v') + "\r\n\r\n",
         Status::Error, ParseError::HeaderLineTooLong},
        {"Host missing on 1.1", "GET / HTTP/1.1\r\n\r\n", Status::Error, ParseError::MissingHost},
        {"duplicate Host", "GET / HTTP/1.1\r\nHost: a\r\nHost: a\r\n\r\n", Status::Error,
         ParseError::DuplicateHost},
    });
}

void test_headers_too_large() {
    // 9 lines of ~7.5 KiB each: every line is under the 8 KiB line limit, but
    // together they pass the 64 KiB total.
    std::string input = "GET / HTTP/1.0\r\n";
    for (int i = 0; i < 9; ++i) {
        input += "X-" + std::to_string(i) + ": " + std::string(7500, 'v') + "\r\n";
    }
    input += "\r\n";
    // Whole input and byte by byte only: the every-split-point check would be
    // ~70,000 runs of a 70 KB input, which is slow for little extra coverage.
    std::vector<std::size_t> every_byte;
    for (std::size_t i = 1; i < input.size(); ++i) {
        every_byte.push_back(i);
    }
    for (const Outcome& out : {run(input, {}), run(input, every_byte)}) {
        EXPECT(out.status == Status::Error, "headers over 64 KiB");
        EXPECT(out.error == ParseError::HeadersTooLarge, "headers over 64 KiB");
    }
}

void test_body() {
    const std::string head = "POST /u HTTP/1.0\r\n";
    run_cases({
        {"POST with body", head + "Content-Length: 5\r\n\r\nhello", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.method() == "POST", n);
             EXPECT(r.body() == "hello", n);
         }},
        {"body still arriving", head + "Content-Length: 5\r\n\r\nhel", Status::NeedMore},
        {"Content-Length: 0", head + "Content-Length: 0\r\n\r\n", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) { EXPECT(r.body().empty(), n); }},
        {"CL with sign", head + "Content-Length: +5\r\n\r\nhello", Status::Error,
         ParseError::BadContentLength},
        {"CL list", head + "Content-Length: 5 ,5\r\n\r\nhello", Status::Error,
         ParseError::BadContentLength},
        {"CL not a number", head + "Content-Length: abc\r\n\r\n", Status::Error,
         ParseError::BadContentLength},
        {"CL 20 digits", head + "Content-Length: 00000000000000000005\r\n\r\nhello",
         Status::Error, ParseError::BadContentLength},
        {"two equal CLs", head + "Content-Length: 2\r\nContent-Length: 2\r\n\r\nhi",
         Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.header("content-length") == "2", n);
             EXPECT(r.body() == "hi", n);
         }},
        {"two different CLs", head + "Content-Length: 2\r\nContent-Length: 3\r\n\r\nhi!",
         Status::Error, ParseError::ConflictingContentLength},
        {"CL over 1 MiB", head + "Content-Length: 1048577\r\n\r\n", Status::Error,
         ParseError::BodyTooLarge},
        {"chunked", head + "Transfer-Encoding: chunked\r\n\r\n", Status::Error,
         ParseError::TransferEncodingNotSupported},
    });
}

void test_leftover() {
    const std::string first = "POST / HTTP/1.0\r\nContent-Length: 3\r\n\r\nabc";
    const std::string second = "GET / HTTP/1.0\r\n\r\n";
    const Outcome out = run_all_ways("pipelined", first + second);
    EXPECT(out.status == Status::Complete, "pipelined");
    EXPECT(out.consumed == first.size(), "pipelined");
    EXPECT(out.request && out.request->body() == "abc", "pipelined");

    // An error stops *at* the bad byte: "GET" is consumed, the '\n' is not.
    const Outcome bad = run_all_ways("error offset", "GET\n");
    EXPECT(bad.consumed == 3, "error offset");

    // After Complete, feed() takes nothing until reset().
    atlas::RequestParser parser;
    parser.feed(second);
    EXPECT(parser.feed(second) == 0, "feed after complete");
    parser.reset();
    EXPECT(parser.status() == Status::NeedMore, "reset");
    EXPECT(parser.feed(second) == second.size(), "reset");
    EXPECT(parser.status() == Status::Complete, "reset");
}

// ----------------------------------------------------------------------------
// CommentPda on its own
// ----------------------------------------------------------------------------

struct PdaCase {
    std::string name;
    std::string value;
    bool accepted;
    ParseError error = ParseError::None;
    std::vector<std::string> words = {};
    std::vector<std::string> comments = {};
};

void test_comment_pda() {
    const std::vector<PdaCase> cases = {
        {"one comment", "Foo (a)", true, ParseError::None, {"Foo"}, {"a"}},
        {"nested", "Foo (a (b (c)))", true, ParseError::None, {"Foo"}, {"a (b (c))"}},
        {"spec trace", "Foo/1.0 (X11; (n) L)", true, ParseError::None, {"Foo/1.0"}, {"X11; (n) L"}},
        {"escaped paren", "Foo (a \\) b)", true, ParseError::None, {"Foo"}, {"a \\) b"}},
        {"escaped open paren", "Foo (a \\( b)", true, ParseError::None, {"Foo"}, {"a \\( b"}},
        {"words and comments", "A/1 (x) B/2 (y)", true, ParseError::None, {"A/1", "B/2"}, {"x", "y"}},
        {"comma splits words", "1.1 proxy, 1.0 other", true, ParseError::None,
         {"1.1", "proxy", "1.0", "other"}},
        {"comment glued to word", "Foo(a)Bar", true, ParseError::None, {"Foo", "Bar"}, {"a"}},
        {"one close too many", "(((((((()))))))))", false, ParseError::UnbalancedComment},
        {"depth 8 balanced", "(((((((())))))))", true, ParseError::None, {}, {"((((((()))))))"}},
        {"depth 9", "((((((((()))))))))", false, ParseError::CommentTooDeep},
        {"unclosed", "Foo (a", false, ParseError::UnbalancedComment},
        {"stray close", "Foo a)", false, ParseError::UnbalancedComment},
        {"backslash at end", "Foo (a \\", false, ParseError::UnbalancedComment},
        {"control byte in comment", "Foo (a\x01)", false, ParseError::BadHeaderValue},
    };

    for (const PdaCase& c : cases) {
        atlas::CommentPda pda;
        bool ok = true;
        for (char ch : c.value) {
            if (!pda.feed(ch)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            ok = pda.finish();
        }
        EXPECT(ok == c.accepted, c.name);
        EXPECT(pda.error() == c.error, c.name);
        if (c.accepted) {
            EXPECT(pda.words() == c.words, c.name);
            EXPECT(pda.comments() == c.comments, c.name);
        }
    }

    // reset() makes it reusable.
    atlas::CommentPda pda;
    pda.feed(')');
    pda.reset();
    for (char ch : std::string("Ok (fine)")) {
        pda.feed(ch);
    }
    EXPECT(pda.finish(), "pda reset");
    EXPECT(pda.words() == std::vector<std::string>{"Ok"}, "pda reset");
}

void test_comments_in_headers() {
    using Words = std::vector<std::string>;
    const std::string head = "GET / HTTP/1.0\r\n";
    run_cases({
        {"User-Agent", head + "User-Agent: Foo/1.0 (X11; (nested) Linux)\r\n\r\n",
         Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.header("user-agent") == "Foo/1.0 (X11; (nested) Linux)", n);
             const atlas::CommentedValue* ua = r.commented("User-Agent");
             EXPECT(ua != nullptr, n);
             if (ua) {
                 EXPECT(ua->words == Words{"Foo/1.0"}, n);
                 EXPECT(ua->comments == Words{"X11; (nested) Linux"}, n);
             }
         }},
        {"Via", head + "Via: 1.1 proxy (Squid), 1.0 other\r\n\r\n", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) {
             const atlas::CommentedValue* via = r.commented("via");
             EXPECT(via != nullptr, n);
             if (via) {
                 EXPECT((via->words == Words{"1.1", "proxy", "1.0", "other"}), n);
                 EXPECT(via->comments == Words{"Squid"}, n);
             }
         }},
        {"two User-Agent lines", head + "User-Agent: A (x)\r\nUser-Agent: B (y)\r\n\r\n",
         Status::Complete, ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.header("user-agent") == "A (x), B (y)", n);
             const atlas::CommentedValue* ua = r.commented("user-agent");
             EXPECT(ua && ua->words == (Words{"A", "B"}) && ua->comments == (Words{"x", "y"}), n);
         }},
        {"unbalanced User-Agent", head + "User-Agent: Foo (a\r\n\r\n", Status::Error,
         ParseError::UnbalancedComment},
        {"stray ) in User-Agent", head + "User-Agent: Foo a)\r\n\r\n", Status::Error,
         ParseError::UnbalancedComment},
        {"( in X-Other is not checked", head + "X-Other: (a\r\n\r\n", Status::Complete,
         ParseError::None,
         [](const Request& r, const std::string& n) {
             EXPECT(r.header("x-other") == "(a", n);
             EXPECT(r.commented("x-other") == nullptr, n);
         }},
    });
}

void test_status_for() {
    EXPECT(atlas::status_for(ParseError::BadMethod) == 400, "status_for");
    EXPECT(atlas::status_for(ParseError::MissingHost) == 400, "status_for");
    EXPECT(atlas::status_for(ParseError::RequestLineTooLong) == 414, "status_for");
    EXPECT(atlas::status_for(ParseError::TooManyHeaders) == 431, "status_for");
    EXPECT(atlas::status_for(ParseError::HeadersTooLarge) == 431, "status_for");
    EXPECT(atlas::status_for(ParseError::BodyTooLarge) == 413, "status_for");
    EXPECT(atlas::status_for(ParseError::TransferEncodingNotSupported) == 501, "status_for");
    EXPECT(atlas::status_for(ParseError::UnsupportedVersion) == 505, "status_for");
}

} // namespace

int main() {
    test_happy_path();
    test_request_line();
    test_headers();
    test_headers_too_large();
    test_body();
    test_leftover();
    test_comment_pda();
    test_comments_in_headers();
    test_status_for();

    if (failures > 0) {
        std::cerr << failures << " parser check(s) failed.\n";
        return 1;
    }
    std::cout << "All parser tests passed.\n";
    return 0;
}
