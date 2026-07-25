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
# include "Parser.hpp"
# include "commands/commands.hpp"
# include "Channel.hpp"

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

        // [ROLE] : Channel registry mapping channel name to Channel object instances.
        std::map<std::string, Channel> _channels;
        // [ROLE] : Human-readable timestamp captured once at startup.
        // [WHY] : Fills RPL_CREATED (numeric 003) without touching <ctime> again
        //         on every single client registration.
        std::string                 _createdAt;

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

        // [ROLE] : Central dispatch once a line has been parsed and identified.
        //          PASS/NICK/USER/QUIT are handled directly; anything else is
        //          gated on registration (451) until its own handler exists.
        // [RETURN] : false if this call disconnected the client (QUIT) — the
        //            caller (_readClientData) must stop touching it immediately.
        bool _processCommand(int fd, Client& client, const IrcMessage& message, e_command type);

        // [ROLE] : PASS / NICK / USER handlers — the registration handshake.
        // [WHEN TO USE] : Called from _processCommand for their respective enum.
        void _handlePass(int fd, Client& client, const IrcMessage& message);
        void _handleNick(int fd, Client& client, const IrcMessage& message);
        void _handleUser(int fd, Client& client, const IrcMessage& message);
        void _handlePing(int fd, const IrcMessage& message);
        void _handlePrivmsg(int fd, Client& client, const IrcMessage& message);
        void _handleJoin(int fd, Client& client, const IrcMessage& message);
        void _handlePart(int fd, Client& client, const IrcMessage& message);
        void _handleTopic(int fd, Client& client, const IrcMessage& message);
        void _handleMode(int fd, Client& client, const IrcMessage& message);
        void _applyModeChanges(int fd, Client& client, Channel& channel, const IrcMessage& message);
        void _handleKick(int fd, Client& client, const IrcMessage& message);
        void _handleInvite(int fd, Client& client, const IrcMessage& message);

        // Recherche un client par nickname (insensible à la casse), comme
        // _isNicknameInUse mais renvoie le client trouvé plutôt qu'un bool.
        Client* _findClientByNickname(const std::string& nickname);
        Channel* _findChannel(const std::string& name);
        void _broadcastToChannel(const Channel& channel, const std::string& message, int excludeFd);
        // [ROLE] : Case-insensitive nickname collision check against every other
        //          connected client (IRC nicknames are case-insensitive).
        // [WHY] : excludeFd lets a client keep/reclaim its own current nickname
        //         without tripping over itself.
        bool _isNicknameInUse(const std::string& nickname, int excludeFd) const;

        // [ROLE] : Sends 001-004 and flips isRegistered, but only once PASS, NICK
        //          and USER have all succeeded.
        // [WHEN TO USE] : Called after each of _handlePass/_handleNick/_handleUser
        //                 succeeds; a no-op until every condition is met, and a
        //                 no-op again afterwards (idempotent).
        void _tryCompleteRegistration(int fd, Client& client);

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