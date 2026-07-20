#include "../include/Server.hpp"

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

    // Register new socket to pollfd monitoring list
    struct pollfd clientPollFd;
    clientPollFd.fd = clientFd;
    clientPollFd.events = POLLIN | POLLOUT; // Monitor both reading and writing
    clientPollFd.revents = 0;
    this->_pollFds.push_back(clientPollFd);

    // Instantiate and register new Client in map registry
    this->_clients.insert(std::make_pair(clientFd, Client(clientFd, ipHost)));
    
    std::cout << "[INFO] New connection accepted on FD " << clientFd 
              << " from " << ipHost << std::endl;
}

// [ROLE] : Reads data chunk from client socket and buffers it inside Client instance.
// [WHEN TO USE] : Called when poll() triggers POLLIN on a client socket descriptor.
void Server::_readClientData(int fd) {
    char buffer[1024];
    std::memset(buffer, 0, sizeof(buffer));

    ssize_t bytesRead = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead <= 0) {
        // Connection closed by client or error occurred
        if (bytesRead == 0) {
            std::cout << "[INFO] Client FD " << fd << " disconnected gracefully." << std::endl;
        } else {
            std::cerr << "[ERROR] Recv error on FD " << fd << std::endl;
        }
        this->_disconnectClient(fd);
    } else {
        // Append raw received bytes to client input buffer
        this->_clients[fd].appendInputBuffer(std::string(buffer, bytesRead));
    }
}

// [ROLE] : Closes client socket and removes it from poll array and client map.
void Server::_disconnectClient(int fd) {
    close(fd);

    // Remove socket entry from pollfd array
    for (std::vector<struct pollfd>::iterator it = this->_pollFds.begin(); it != this->_pollFds.end(); ++it) {
        if (it->fd == fd) {
            this->_pollFds.erase(it);
            break;
        }
    }

    // Remove client entry from map registry
    this->_clients.erase(fd);
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
            if (this->_isRunning == false) {
                break; // Graceful shutdown via signal interrupt
            }
            throw std::runtime_error("Poll system call failure");
        }

        // Iterate over monitored socket descriptors
        for (size_t i = 0; i < this->_pollFds.size(); ++i) {
            if (this->_pollFds[i].revents & POLLIN) {
                if (this->_pollFds[i].fd == this->_serverSocketFd) {
                    // Event on master listening socket -> Accept new client
                    this->_acceptNewConnection();
                } else {
                    // Event on established client socket -> Read incoming data
                    this->_readClientData(this->_pollFds[i].fd);
                }
            }
        }
    }
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
