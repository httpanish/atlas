#include <atlas>
#include <iostream>

int main() {
    atlas::App app;

    app["/"] = "Hello World!";
    app["/about"] = "About Atlas";

    std::cout << "Route '/'     : " << app["/"] << "\n";
    std::cout << "Route '/about': " << app["/about"] << "\n";
    std::cout << "Total routes  : " << app.size() << "\n";

    return 0;
}
