#include "Server.hpp"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <signal.h>
#include <string>

/* ========================================================================== */
/*                         SIGNAL HANDLING                                    */
/* ========================================================================== */

namespace {

// [ROLE] : Global pointer to the running server.
// [WHY] : A signal handler can only be a plain function pointer (no context,
//         no capture), so it needs some way to reach the running instance.
Server* g_server = NULL;

// [ROLE] : Handler for SIGINT/SIGQUIT (Ctrl+C / Ctrl+\).
// [WHY] : Only flips a flag (async-signal-safe). Server::stop() itself closes
//         sockets and touches STL containers, which is not safe to run from
//         inside a signal handler, so it stays out of this function entirely.
void handleShutdownSignal(int) {
    if (g_server) {
        g_server->requestStop();
    }
}

// [ROLE] : Parses and validates the port argument.
// [WHY] : Must reject anything that isn't a plain positive integer in the
//         valid TCP port range before it ever reaches socket()/bind().
bool parsePort(const std::string& arg, int& port) {
    if (arg.empty() || arg.size() > 5) {
        return false;
    }
    for (std::size_t i = 0; i < arg.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(arg[i]))) {
            return false;
        }
    }
    port = std::atoi(arg.c_str());
    return port > 0 && port <= 65535;
}

}


/* ========================================================================== */
/*                                   MAIN                                     */
/* ========================================================================== */

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
        return 1;
    }

    int port = 0;
    if (!parsePort(argv[1], port)) {
        std::cerr << "Error: invalid port '" << argv[1] << "' (expected 1-65535)" << std::endl;
        return 1;
    }

    Server server(port, std::string(argv[2]));
    g_server = &server;

    struct sigaction sa;
    sa.sa_handler = handleShutdownSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
