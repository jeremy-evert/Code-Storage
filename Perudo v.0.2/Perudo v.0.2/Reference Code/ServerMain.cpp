#include "Server.h"
#include <iostream>

int main() {
    std::cout << "Starting Perudo server on port 54000...\n";
    Server server;
    server.start(54000);
    return 0;
}
