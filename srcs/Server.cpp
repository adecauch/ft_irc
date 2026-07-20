#include "../include/Server.hpp"
#include "../include/Parser.hpp"
#include "../include/commands/commands.hpp"
#include "../include/NumericReplies.hpp"

/* ========================================================================== */
/*                         ORTHODOX CANONICAL FORM                            */
/* ========================================================================== */

// [ROLE] : Default constructor.
Server::Server() 
    : _port(6667), 
      _password(""), 
      _serverSocketFd(-1), 
      _isRunning(false) {}

// [ROLE] : Copy constructor.
Server::Server(const Server& src) {
    *this = src;
}

// [ROLE] : Assignment operator.
Server& Server::operator=(const Server& rhs) {
    if (this != &rhs) {
        this->_port = rhs._port;
        this->_password = rhs._password;
        this->_serverSocketFd = rhs._serverSocketFd;
        this->_isRunning = rhs._isRunning;
        this->_pollFds = rhs._pollFds;
        this->_clients = rhs._clients;
    }
    return *this;
}

// [ROLE] : Destructor. Ensures cleanup of all resources upon object destruction.
Server::~Server() {
    this->stop();
}


/* ========================================================================== */
/*                         PARAMETERIZED CONSTRUCTOR                          */
/* ========================================================================== */

// [ROLE] : Custom constructor bound to user-defined port and server password.
Server::Server(int port, const std::string& password) 
    : _port(port), 
      _password(password), 
      _serverSocketFd(-1), 
      _isRunning(false) {}


/* ========================================================================== */
/*                         PRIVATE NETWORK INITIALIZATION                      */
/* ========================================================================== */

// [ROLE] : Creates, configures, binds, and listens on the main server socket.
// [WHY] : SO_REUSEADDR allows instant server restarts. O_NONBLOCK prevents I/O stalling.
void Server::_initServerSocket() {
    // 1. Create IPv4 stream socket (TCP)
    this->_serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->_serverSocketFd < 0) {
        throw std::runtime_error("Failed to create server socket");
    }

    // 2. Allow immediate socket address reuse upon restart
    int optval = 1;
    if (setsockopt(this->_serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        close(this->_serverSocketFd);
        throw std::runtime_error("Failed to set SO_REUSEADDR option");
    }

    // 3. Set master listening socket to non-blocking mode
    if (fcntl(this->_serverSocketFd, F_SETFL, O_NONBLOCK) < 0) {
        close(this->_serverSocketFd);
        throw std::runtime_error("Failed to set non-blocking mode on server socket");
    }

    // 4. Bind socket to target IP and Port
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(this->_port);

    if (bind(this->_serverSocketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(this->_serverSocketFd);
        throw std::runtime_error("Failed to bind server socket to port");
    }

    // 5. Start listening for incoming connections
    if (listen(this->_serverSocketFd, SOMAXCONN) < 0) {
        close(this->_serverSocketFd);
        throw std::runtime_error("Failed to listen on server socket");
    }

    // 6. Register master listening socket into poll monitoring array
    struct pollfd serverPollFd;
    serverPollFd.fd = this->_serverSocketFd;
    serverPollFd.events = POLLIN; // Monitor incoming connection requests
    serverPollFd.revents = 0;
    this->_pollFds.push_back(serverPollFd);
}


/* ========================================================================== */
/*                         EVENT HANDLING ROUTINES                             */
/* ========================================================================== */

// [ROLE] : Accepts a new client connection attempt from the listening socket.
// [WHEN TO USE] : Called by start() when POLLIN triggers on _serverSocketFd.
void Server::_acceptNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    int clientFd = accept(this->_serverSocketFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientFd < 0) {
        std::cerr << "[ERROR] Failed to accept client connection" << std::endl;
        return;
    }

    // Set client socket to non-blocking mode
    if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << "[ERROR] Failed to set non-blocking on client socket" << std::endl;
        close(clientFd);
        return;
    }

    // Retrieve client IP address string
    std::string ipHost = inet_ntoa(clientAddr.sin_addr);

    // Register new socket to pollfd monitoring list.
    // POLLOUT is deliberately NOT set here: it is only armed once _outputBuffer
    // actually has bytes to send (see sendToClient()), otherwise poll() would
    // wake up on every loop iteration for a socket that has nothing to write.
    struct pollfd clientPollFd;
    clientPollFd.fd = clientFd;
    clientPollFd.events = POLLIN;
    clientPollFd.revents = 0;
    this->_pollFds.push_back(clientPollFd);

    // Instantiate and register new Client in map registry
    this->_clients.insert(std::make_pair(clientFd, Client(clientFd, ipHost)));
    
    std::cout << "[INFO] New connection accepted on FD " << clientFd 
              << " from " << ipHost << std::endl;
}

// [ROLE] : Reads data chunk from client socket and buffers it inside Client instance.
// [WHEN TO USE] : Called when poll() triggers POLLIN on a client socket descriptor.
bool Server::_readClientData(int fd) {
    char buffer[1024];
    std::memset(buffer, 0, sizeof(buffer));

    ssize_t bytesRead = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead < 0) {
        // A non-blocking socket reporting POLLIN can still race into EAGAIN;
        // that is not an error, just nothing to read yet on this pass.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        std::cerr << "[ERROR] Recv failure on FD " << fd << std::endl;
        this->_disconnectClient(fd);
        return false;
    }

    if (bytesRead == 0) {
        // Peer performed an orderly shutdown (e.g. QUIT, closed terminal).
        std::cout << "[INFO] Client FD " << fd << " disconnected." << std::endl;
        this->_disconnectClient(fd);
        return false;
    }

    Client& client = this->_clients[fd];
    client.appendInputBuffer(std::string(buffer, bytesRead));

    // Extraction loop: process every complete line already sitting in the
    // buffer, so fragmented sends (e.g. "com" + "man" + "d\n" via nc -C)
    // are recombined before being treated as a single command.
    std::string commandLine;
    while (client.extractNextLine(commandLine)) {
        IrcMessage message = Parser::parse(commandLine);
        e_command  type = resolveCommand(message.command);

        std::cout << "[DEBUG] FD " << fd << " command=\"" << message.command
                  << "\" type=" << type;
        for (size_t p = 0; p < message.params.size(); ++p) {
            std::cout << " params[" << p << "]=\"" << message.params[p] << "\"";
        }
        std::cout << std::endl;

        if (type == CMD_UNKNOWN) {
            // A blank line (empty command) is not a command attempt at all —
            // just protocol noise (keepalive, stray CRLF) — nothing to reply to.
            if (!message.command.empty()) {
                this->sendToClient(fd, ERR_UNKNOWNCOMMAND(client.getNickname(), message.command));
            }
            continue;
        }

        // Le dispatch vers un handler par commande arrive à l'Étape 4.
    }
    return true;
}

// [ROLE] : Flushes queued bytes from a client's output buffer via send().
// [WHEN TO USE] : Called when poll() triggers POLLOUT on a client socket descriptor.
void Server::_writeClientData(int fd) {
    std::map<int, Client>::iterator clientIt = this->_clients.find(fd);
    if (clientIt == this->_clients.end()) {
        return;
    }
    Client& client = clientIt->second;

    if (!client.hasOutputData()) {
        // Nothing left to write: stop polling for writability so the loop
        // does not keep waking up for an event we no longer care about.
        std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
        if (pollIt != this->_pollFds.end()) {
            pollIt->events &= ~POLLOUT;
        }
        return;
    }

    const std::string& outputBuffer = client.getOutputBuffer();
    ssize_t bytesSent = send(fd, outputBuffer.c_str(), outputBuffer.size(), 0);

    if (bytesSent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // Socket buffer full for now, retry on the next POLLOUT.
        }
        std::cerr << "[ERROR] Send failure on FD " << fd << std::endl;
        this->_disconnectClient(fd);
        return;
    }

    // Only the bytes the kernel actually accepted are removed: send() on a
    // non-blocking socket may write less than the full buffer in one call.
    client.clearOutputBuffer(static_cast<size_t>(bytesSent));

    if (!client.hasOutputData()) {
        std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
        if (pollIt != this->_pollFds.end()) {
            pollIt->events &= ~POLLOUT;
        }
    }
}

// [ROLE] : Closes client socket and removes it from poll array and client map.
void Server::_disconnectClient(int fd) {
    close(fd);

    std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
    if (pollIt != this->_pollFds.end()) {
        this->_pollFds.erase(pollIt);
    }

    // Remove client entry from map registry
    this->_clients.erase(fd);
}

// [ROLE] : Locates the pollfd entry tracking a given socket fd.
std::vector<struct pollfd>::iterator Server::_findPollFd(int fd) {
    for (std::vector<struct pollfd>::iterator it = this->_pollFds.begin(); it != this->_pollFds.end(); ++it) {
        if (it->fd == fd) {
            return it;
        }
    }
    return this->_pollFds.end();
}


/* ========================================================================== */
/*                         PUBLIC CORE METHODS                                */
/* ========================================================================== */

// [ROLE] : Main execution loop using poll multiplexing.
void Server::start() {
    this->_initServerSocket();
    this->_isRunning = true;

    std::cout << "[SUCCESS] IRC Server listening on port " << this->_port << "..." << std::endl;

    while (this->_isRunning) {
        // Poll for ready descriptors with unlimited timeout (-1)
        int pollCount = poll(&this->_pollFds[0], this->_pollFds.size(), -1);

        if (pollCount < 0) {
            if (!this->_isRunning) {
                break; // Graceful shutdown via signal interrupt (EINTR from Ctrl+C)
            }
            if (errno == EINTR) {
                continue; // Interrupted by an unrelated signal, just poll again
            }
            throw std::runtime_error("Poll system call failure");
        }

        // Iterate over monitored socket descriptors. Indices are re-checked
        // rather than advanced whenever a client is disconnected mid-loop,
        // because _disconnectClient() erases from _pollFds and shifts every
        // following element one slot to the left.
        for (size_t i = 0; i < this->_pollFds.size(); ++i) {
            int fd = this->_pollFds[i].fd;
            short revents = this->_pollFds[i].revents;

            if (revents == 0) {
                continue;
            }

            if (fd == this->_serverSocketFd) {
                // Event on master listening socket -> Accept new client.
                if (revents & POLLIN) {
                    this->_acceptNewConnection();
                }
                continue;
            }

            // A hangup or error takes priority over anything else reported
            // for this fd: the client is gone, do not touch it any further.
            if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
                this->_disconnectClient(fd);
                --i;
                continue;
            }

            if (revents & POLLIN) {
                if (!this->_readClientData(fd)) {
                    --i; // fd was disconnected: _pollFds shifted, re-check index i
                    continue;
                }
            }

            if (revents & POLLOUT) {
                this->_writeClientData(fd);
            }
        }
    }
}

// [ROLE] : Queues a message in a client's output buffer and arms POLLOUT.
// [WHEN TO USE] : The single entry point for command handlers to reply to a
//                 client. The actual send() only ever happens inside the
//                 poll() loop, via _writeClientData(), never here.
void Server::sendToClient(int fd, const std::string& message) {
    std::map<int, Client>::iterator clientIt = this->_clients.find(fd);
    if (clientIt == this->_clients.end()) {
        return;
    }
    clientIt->second.appendOutputBuffer(message);

    std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
    if (pollIt != this->_pollFds.end()) {
        pollIt->events |= POLLOUT;
    }
}

// [ROLE] : Async-signal-safe stop request, used by the SIGINT/SIGQUIT handler.
// [WHY] : stop() closes sockets and clears STL containers, which is unsafe to run
//         from inside a signal handler; this only sets the flag that the poll()
//         loop in start() already checks once it is interrupted (EINTR).
void Server::requestStop() {
    this->_isRunning = false;
}

// [ROLE] : Stops main loop and closes all client and listening sockets cleanly.
void Server::stop() {
    if (!this->_isRunning && this->_serverSocketFd == -1) {
        return;
    }

    this->_isRunning = false;

    // Close all connected client sockets
    for (std::map<int, Client>::iterator it = this->_clients.begin(); it != this->_clients.end(); ++it) {
        close(it->first);
    }
    this->_clients.clear();

    // Close listening socket
    if (this->_serverSocketFd != -1) {
        close(this->_serverSocketFd);
        this->_serverSocketFd = -1;
    }

    this->_pollFds.clear();
    std::cout << "[INFO] Server stopped successfully." << std::endl;
}


/* ========================================================================== */
/*                               GETTERS                                      */
/* ========================================================================== */

int Server::getPort() const {
    return this->_port;
}

const std::string& Server::getPassword() const {
    return this->_password;
}
