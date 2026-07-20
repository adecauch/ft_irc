#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include <poll.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <fcntl.h>
# include <iostream>
# include "Client.hpp"

/* ========================================================================== */
/*                             SERVER CLASS                                   */
/*                                                                            */
/*  Core network engine. Manages listening sockets, event multiplexing via   */
/*  poll(), incoming client connections, and client lifecycle state.          */
/* ========================================================================== */

class Server {

    private:
        // [ROLE] : Network configuration parameters passed as main() arguments.
        int                         _port;
        std::string                 _password;
        
        // [ROLE] : Master listening socket file descriptor.
        // [WHY] : Set up during start(), listens continuously for incoming connections.
        int                         _serverSocketFd;
        
        // [ROLE] : Server execution loop state flag.
        // [WHY] : Turned to false when SIGINT/SIGQUIT (Ctrl+C) is caught to gracefully exit.
        bool                        _isRunning;

        // [ROLE] : Event monitoring array for poll().
        // [WHY] : Stores socket FDs along with monitored events (POLLIN / POLLOUT).
        std::vector<struct pollfd>  _pollFds;

        // [ROLE] : Client registry mapping socket FD to Client object instances.
        // [WHY] : Provides fast O(log N) lookup to retrieve a client by its socket FD.
        std::map<int, Client>       _clients;

        // -----------------------------------------------------------------------------
        //                         Private Helper Functions
        // -----------------------------------------------------------------------------
        
        // [ROLE] : Initializes the master socket (socket, setsockopt, bind, listen, fcntl).
        void _initServerSocket();
        
        // [ROLE] : Accepts a new incoming connection and registers it in _pollFds and _clients.
        void _acceptNewConnection();
        
        // [ROLE] : Reads raw incoming bytes from a client socket via recv().
        void _readClientData(int fd);
        
        // [ROLE] : Disconnects a client, closes its socket, and cleans up registry memory.
        void _disconnectClient(int fd);

    public:
        // -----------------------------------------------------------------------------
        //                         Orthodox Canonical Form
        // -----------------------------------------------------------------------------
        Server();
        Server(const Server& src);
        Server& operator=(const Server& rhs);
        ~Server();

        // -----------------------------------------------------------------------------
        //                         Parameterized Constructor
        // -----------------------------------------------------------------------------
        // [ROLE] : Constructs the server using parsed port number and password.
        Server(int port, const std::string& password);

        // -----------------------------------------------------------------------------
        //                         Core Execution Methods
        // -----------------------------------------------------------------------------
        
        // [ROLE] : Bootstraps sockets and starts the main event loop with poll().
        void start();
        
        // [ROLE] : Gracefully stops the server loop and closes all open client sockets.
        void stop();

        // -----------------------------------------------------------------------------
        //                         Getters
        // -----------------------------------------------------------------------------
        int                getPort() const;
        const std::string& getPassword() const;
};

#endif