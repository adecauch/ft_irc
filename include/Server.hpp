#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include <cerrno>
# include <cstring>
# include <stdexcept>
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
        // [RETURN] : false if the client was disconnected during the call (fatal
        //            recv() error or orderly shutdown), true otherwise. The caller
        //            (start()) uses this to know its _pollFds indices just shifted.
        bool _readClientData(int fd);

        // [ROLE] : Flushes as much of a client's _outputBuffer as send() accepts.
        // [WHEN TO USE] : Called by start() when poll() reports POLLOUT on a client fd.
        // [BEHAVIOR] : Removes the sent bytes from _outputBuffer; once it is empty,
        //              clears POLLOUT from that pollfd's events so poll() stops
        //              waking up for writability it no longer needs to check.
        void _writeClientData(int fd);

        // [ROLE] : Disconnects a client, closes its socket, and cleans up registry memory.
        void _disconnectClient(int fd);

        // [ROLE] : Finds the pollfd entry tracking a given socket fd.
        // [WHY] : Shared by every place that needs to flip POLLOUT on/off, so the
        //         linear search over _pollFds lives in exactly one function.
        std::vector<struct pollfd>::iterator _findPollFd(int fd);

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

        // [ROLE] : Flips the running flag only, without touching sockets or memory.
        // [WHEN TO USE] : Called from the SIGINT/SIGQUIT handler. The real cleanup
        //                 still happens in stop(), invoked afterwards from main()
        //                 once poll() has returned and execution is back to normal
        //                 (non-signal) flow.
        void requestStop();

        // [ROLE] : Queues a message for a client and arms POLLOUT on its pollfd.
        // [WHEN TO USE] : This is the single entry point command handlers use to
        //                 reply to a client. Nothing is ever written with send()
        //                 outside of the poll() loop (_writeClientData); this only
        //                 buffers the bytes and signals interest in writability.
        void sendToClient(int fd, const std::string& message);

        // -----------------------------------------------------------------------------
        //                         Getters
        // -----------------------------------------------------------------------------
        int                getPort() const;
        const std::string& getPassword() const;
};

#endif