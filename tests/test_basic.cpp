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

    std::cout << "All basic Atlas tests passed successfully!\n";
    return 0;
}
