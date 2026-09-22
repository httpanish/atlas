#include <atlas>
#include <cassert>
#include <iostream>

int main() {
    atlas::App app;

    // 1. Test basic route assignment
    app["/"] = "Hello World!";
    app["/about"] = "About Atlas";

    assert(app.size() == 2);
    assert(app["/"] == "Hello World!");
    assert(app["/about"] == "About Atlas");
    assert(app.contains("/"));
    assert(app.contains("/about"));
    assert(!app.contains("/nonexistent"));

    // 2. Test route overwrite
    app["/"] = "Welcome to Atlas!";
    assert(app.size() == 2);
    assert(app["/"] == "Welcome to Atlas!");

    // 3. Test get() helper
    assert(app.get("/about") == "About Atlas");
    assert(app.get("/missing") == "");

    // 4. Test reading from a const App
    const atlas::App& const_app = app;
    assert(const_app["/"] == "Welcome to Atlas!");
    assert(const_app.contains("/about"));

    // 5. Test Request and Response handling
    atlas::Request req_root("GET", "/");
    assert(req_root.method() == "GET");
    assert(req_root.path() == "/");

    atlas::Response res_root = app.handle(req_root);
    assert(res_root.status_code() == 200);
    assert(res_root.body() == "Welcome to Atlas!");

    atlas::Request req_about("GET", "/about");
    atlas::Response res_about = app.handle(req_about);
    assert(res_about.status_code() == 200);
    assert(res_about.body() == "About Atlas");

    // 6. Test 404 Not Found for unregistered routes
    atlas::Request req_missing("GET", "/not-found");
    atlas::Response res_missing = app.handle(req_missing);
    assert(res_missing.status_code() == 404);
    assert(res_missing.body() == "Not Found");

    std::cout << "All basic Atlas tests passed successfully!\n";
    return 0;
}
