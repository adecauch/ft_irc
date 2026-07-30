/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 15:05:20 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:07:46 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <signal.h>
#include <string>

namespace {

// Un handler de signal est un simple pointeur de fonction, sans contexte : il
// lui faut donc un global pour atteindre le serveur en cours.
Server* g_server = NULL;

// Ne fait que baisser un drapeau : seule chose sure dans un handler de signal.
// Le vrai nettoyage a lieu dans Server::stop(), hors du handler.
void handleShutdownSignal(int) {
    if (g_server) {
        g_server->requestStop();
    }
}

// Refuse tout ce qui n'est pas un entier dans la plage des ports TCP, avant que
// la valeur n'atteigne socket()/bind().
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

    // SIGPIPE doit etre ignore : son action par defaut est de TUER le process,
    // donc un send() vers un client parti en RST suffirait a couper le serveur.
    struct sigaction ignore;
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    ignore.sa_flags = 0;
    sigaction(SIGPIPE, &ignore, NULL);

    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
