# Atlas Routing Spec v0.2: Methods and Lambda Handlers

**Status:** Draft
**Scope:** Routing API (`App`, `Route`, `Endpoint`), handler model, and dispatch rules.
**Goal:** Support every HTTP method and dynamic responses, keeping the path-first map syntax that gives Atlas its name.

---

## 1. Design principles

1. **Path first.** The outer key is always the path. An app is a map of paths, and each path is a map of methods.
2. **Assignment is the only way to register.** There is no `app.get(...)` or `app.post(...)`. You register a route with `[]` and `=`.
3. **Values stay small.** A route's value is either a fixed response or a lambda. Larger logic goes in named functions that the map refers to.

## 2. Syntax at a glance

```cpp
atlas::App app;

// Static values (GET shorthand)
app["/"]       = "Hello";                 // 200 text/plain
app["/teapot"] = {418, "I'm a teapot"};   // custom status

// Explicit methods
app["/users"]["GET"]  = "user list";
app["/users"]["POST"] = [](const atlas::Request& req) {
    return req.body();                    // std::string -> 200
};

// Lambda with a custom status (return type must be named, see §4.3)
app["/users"]["DELETE"] = [](const atlas::Request&) -> atlas::Response {
    return {204, ""};
};

// Named function
atlas::Response create_user(const atlas::Request& req);
app["/signup"]["POST"] = create_user;
```

## 3. Types

### 3.1 `Handler`
```cpp
using Handler = std::function<Response(const Request&)>;
```
Every endpoint is stored as a `Handler`, including static values (see §3.2).

### 3.2 `Endpoint`
One method on one path. It's the value you get back from `app[path][method]`.

| Member | Behavior |
|---|---|
| `Endpoint& operator=(Response value)` | Stores a handler that returns a copy of `value`. |
| `Endpoint& operator=(Handler fn)` | Stores `fn`. |
| `Response operator()(const Request&) const` | Calls the stored handler. |
| `explicit operator bool() const` | `true` if a handler has been assigned. |

There are only these two assignment overloads, and that's deliberate. String literals, `std::string`, and `{code, body}` all reach the `Response` overload through `Response`'s implicit constructors (§3.4). Lambdas and functions reach the `Handler` overload. A separate `std::string` overload would make `= "text"` ambiguous.

### 3.3 `Route`
All the methods registered on one path.

| Member | Behavior |
|---|---|
| `Endpoint& operator[](std::string_view method)` | Creates the endpoint if it doesn't exist. Validates `method` (§5.1). |
| `const Endpoint& operator[](std::string_view method) const` | Throws `std::out_of_range` if the method isn't registered. |
| `Route& operator=(Response)` / `Route& operator=(Handler)` | Shorthand for `(*this)["GET"] = ...`. |
| `bool contains(std::string_view method) const` | |
| `std::size_t size() const` | Number of registered methods. |
| `begin()` / `end()` | Iterate `(method, Endpoint)` pairs in sorted order. |
| `std::string allow() const` | Comma-separated method list, e.g. `"GET, POST"`. |

Storage: `std::map<std::string, Endpoint, std::less<>>`. A sorted map keeps the `Allow` header output deterministic, and `std::less<>` allows lookup with a `string_view`.

### 3.4 `Response` changes
| Constructor | Explicit? | Purpose |
|---|---|---|
| `Response(const char* body)` | no | `= "Hello"` and lambdas returning literals |
| `Response(std::string body)` | no | lambdas returning `std::string` |
| `Response(int status, std::string body)` | no | `= {418, "..."}` and `return {204, ""}` |

Add a minimal header map:
```cpp
std::map<std::string, std::string>& headers();
const std::map<std::string, std::string>& headers() const;
```
The serializer writes these headers. It always computes `Content-Length` itself, and it defaults `Content-Type` to `text/plain` unless the response already sets it.

### 3.5 `App` changes
| Member | Behavior |
|---|---|
| `Route& operator[](std::string_view path)` | Creates the route if it doesn't exist. `path` must start with `/`; otherwise throws `std::invalid_argument`. |
| `const Route& operator[](std::string_view path) const` | Throws `std::out_of_range` if the path isn't registered (unchanged). |
| `bool contains(std::string_view path) const` | Unchanged. |
| `std::size_t size() const` | Number of **paths**, not endpoints. |
| `begin()` / `end()` | Iterate `(path, Route)` pairs. |
| `Response handle(const Request&) const` | See §5.2. |
| ~~`std::string get(path)`~~ | **Removed.** A route no longer has a single string value. |

## 4. Handler rules

### 4.1 Accepted signature
`Response(const Request&)`, or anything convertible to it. That includes:
- `[](const atlas::Request& r) { return r.body(); }`, which returns a `std::string`
- `[](auto& r) { ... }`, a generic lambda
- free functions with the matching signature

Zero-argument lambdas are **not** supported in v0.2 (see §7).

### 4.2 Return values
Anything implicitly convertible to `Response`: `std::string`, `const char*`, or `Response`.

### 4.3 Known gotcha: braces in lambdas
C++ can't work out a lambda's return type from `return {…};`. If a lambda returns a custom status, write it in one of these ways:
```cpp
[](const atlas::Request&) -> atlas::Response { return {201, "ok"}; }
[](const atlas::Request&) { return atlas::Response{201, "ok"}; }
```
Named functions with a declared `Response` return type can use plain `return {201, "ok"};`. Document this in the README.

### 4.4 Exceptions
If a handler throws, `handle()` catches it and returns `500 Internal Server Error`. The exception message is **not** sent to the client.

## 5. Dispatch

### 5.1 Method keys
- Case-sensitive. HTTP methods are case-sensitive on the wire.
- Must be non-empty and contain only `A`–`Z`. Anything else (`"post"`, `"GET "`, `""`) throws `std::invalid_argument` at registration, so typos fail loudly at startup.
- Any well-formed token is accepted, so custom methods like `PURGE` are allowed.

### 5.2 `App::handle(req)`
1. Look up `req.path()` exactly (no trailing-slash normalization, no query stripping in v0.2).
2. Path not found: **404** `Not Found`.
3. Path found, method not registered: **405** `Method Not Allowed`, with the header `Allow: <route.allow()>`. The HTTP spec (RFC 9110 §15.5.6) requires the `Allow` header on a 405.
4. Otherwise call the endpoint. If it throws, apply §4.4.

### 5.3 Reason phrases
The serializer needs a lookup for at least: 200 OK, 201 Created, 204 No Content, 400 Bad Request, 404 Not Found, 405 Method Not Allowed, 418 I'm a teapot, 500 Internal Server Error. Unknown codes get an empty reason phrase (`HTTP/1.1 299 \r\n`), which HTTP/1.1 allows.

## 6. Required changes outside routing

These are needed for the new routes to be reachable in practice:
1. **Parser:** remove the `method_ != "GET"` check and validate the method as a token instead.
2. **Parser:** read the request body using `Content-Length`. Without it, `POST` handlers always see an empty body. (This can be a separate PR; routing works without it.)
3. **Serializer:** add the reason-phrase table (§5.3) and write `Response::headers()`.

## 7. Out of scope for v0.2 (explicitly deferred)
- Path parameters (`/users/:id`) and `req["id"]`
- Template values (`"Hello {id}"`)
- Value helpers (`atlas::file`, `atlas::redirect`)
- Defining the whole app as one nested map literal
- Nested sub-routes (`app["/api"]["/users"]`)
- Error-page map (`app.errors[404]`)
- Zero-argument lambdas and handlers that assign to a `res` parameter
- Automatic `HEAD` from `GET`, automatic `OPTIONS`
- An enum for method keys

## 8. Test plan
| # | Case | Expect |
|---|---|---|
| 1 | `app["/"] = "Hi"`, then GET `/` | 200, body `Hi` |
| 2 | `app["/t"] = {418, "x"}` | 418, body `x` |
| 3 | Lambda returning `std::string` | 200 with that body |
| 4 | Lambda returning `Response{201, ...}` | 201 |
| 5 | Named function handler | Called, response returned |
| 6 | Shorthand `app["/a"] = ...` is the same as `app["/a"]["GET"] = ...` | `app["/a"].contains("GET")` |
| 7 | GET `/missing` | 404 |
| 8 | DELETE on a path with only GET and POST | 405, `Allow: GET, POST` |
| 9 | Handler throws `std::runtime_error("secret")` | 500, body doesn't contain `secret` |
| 10 | `app["/x"]["post"]`, `["GET "]`, `[""]` | `std::invalid_argument` |
| 11 | `app["nope"]` | `std::invalid_argument` |
| 12 | Const `app["/missing"]` / `route["PUT"]` | `std::out_of_range` |
| 13 | Re-assigning an endpoint | Last assignment wins; `size()` unchanged |
| 14 | Iterating an app / route | Visits every path / method; route order is sorted |
| 15 | Lambda sees `req.method()` and `req.path()` | Values match the request |

Existing tests that compare `app["/"] == "Hello World!"` must be rewritten to go through `app.handle(...)`, since a route no longer compares equal to a string.

## 9. Suggested implementation order
1. `Response` constructors and headers, plus the serializer reason table and header output.
2. The `Endpoint` and `Route` types, and switching `App`'s storage to `std::unordered_map<std::string, Route>`.
3. The new `handle()` dispatch (404 / 405 / 500).
4. Parser: accept any token method.
5. Rewrite the tests to match §8.
6. Parser: `Content-Length` body (separate PR).

## 10. Open questions
- **Should registration take a method enum instead of strings?** That would catch `"POTS"` at compile time, but it gives up the "keys are what's on the wire" feel.
- **Should `HEAD` fall back to `GET` automatically?** Most servers do this, and it's cheap to add once headers exist.
