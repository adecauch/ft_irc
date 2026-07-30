#include "../include/Server.hpp"
#include "../include/Parser.hpp"
#include "../include/commands/commands.hpp"
#include "../include/NumericReplies.hpp"

#include <ctime>

namespace {

// Plafond d'octets acceptes pour UNE ligne pas encore terminee. Sans lui, un
// client qui n'envoie jamais de \r\n ferait grossir _inputBuffer sans fin.
const std::size_t MAX_PENDING_LINE = 8192;

// Plafond de clients. Sans lui, un flood de connexions epuise les descripteurs
// et accept() echoue en boucle alors que poll() resignale POLLIN : 100% de CPU.
const std::size_t MAX_CLIENTS = 512;

}

/* ========================================================================== */
/*                         ORTHODOX CANONICAL FORM                            */
/* ========================================================================== */

Server::Server()
    : _port(6667),
      _password(""),
      _serverSocketFd(-1),
      _isRunning(false) {}

Server::Server(const Server& src) {
    *this = src;
}

Server& Server::operator=(const Server& rhs) {
    if (this != &rhs) {
        this->_port = rhs._port;
        this->_password = rhs._password;
        this->_serverSocketFd = rhs._serverSocketFd;
        this->_isRunning = rhs._isRunning;
        this->_pollFds = rhs._pollFds;
        this->_clients = rhs._clients;
        this->_channels = rhs._channels;
        this->_createdAt = rhs._createdAt;
    }
    return *this;
}

Server::~Server() {
    this->stop();
}


/* ========================================================================== */
/*                         PARAMETERIZED CONSTRUCTOR                          */
/* ========================================================================== */

Server::Server(int port, const std::string& password)
    : _port(port),
      _password(password),
      _serverSocketFd(-1),
      _isRunning(false) {}


/* ========================================================================== */
/*                         PRIVATE NETWORK INITIALIZATION                      */
/* ========================================================================== */

// SO_REUSEADDR permet de relancer le serveur sans attendre la fin du TIME_WAIT,
// O_NONBLOCK evite qu'un appel reseau ne fige tout le process.
void Server::_initServerSocket() {
    // ctime() ajoute toujours un '\n' final : le laisser casserait le decoupage
    // en lignes de la reponse 003 dans laquelle on le concatene.
    std::time_t now = std::time(NULL);
    this->_createdAt = std::ctime(&now);
    if (!this->_createdAt.empty() && this->_createdAt[this->_createdAt.size() - 1] == '\n') {
        this->_createdAt.erase(this->_createdAt.size() - 1);
    }

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

    // 4. Bind socket to target IP and Port. INADDR_ANY = toutes les interfaces.
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

    // 6. La socket d'ecoute prend la premiere place de _pollFds, definitivement.
    struct pollfd serverPollFd;
    serverPollFd.fd = this->_serverSocketFd;
    serverPollFd.events = POLLIN; // Monitor incoming connection requests
    serverPollFd.revents = 0;
    this->_pollFds.push_back(serverPollFd);
}


/* ========================================================================== */
/*                         EVENT HANDLING ROUTINES                             */
/* ========================================================================== */

// Appelee par start() quand poll() signale POLLIN sur la socket d'ecoute.
void Server::_acceptNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    int clientFd = accept(this->_serverSocketFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientFd < 0) {
        // Pas de test d'errno ici non plus : poll() a signale POLLIN, donc une
        // connexion attendait. Si accept() echoue, on n'en fait rien de plus.
        std::cerr << "[ERROR] Failed to accept client connection" << std::endl;
        return;
    }

    // On accepte puis on ferme aussitot : ca vide le backlog, alors que refuser
    // en laissant la connexion en attente ferait resignaler POLLIN sans fin.
    if (this->_clients.size() >= MAX_CLIENTS) {
        std::cerr << "[WARN] Client limit (" << MAX_CLIENTS
                  << ") reached, connection refused" << std::endl;
        close(clientFd);
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

    // POLLOUT n'est deliberement PAS arme ici : il ne l'est qu'une fois qu'il y a
    // vraiment des octets a envoyer (voir sendToClient), sinon poll() reviendrait
    // a chaque tour pour une socket qui n'a rien a ecrire.
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

// Appelee quand poll() signale POLLIN sur une socket cliente.
// Renvoie false si le client a ete deconnecte pendant l'appel.
bool Server::_readClientData(int fd) {
    // On passe par find() plutot que _clients[fd], qui creerait par defaut un
    // Client fantome (fd -1) si les deux registres se desynchronisaient.
    std::map<int, Client>::iterator clientIt = this->_clients.find(fd);
    if (clientIt == this->_clients.end()) {
        this->_disconnectClient(fd);
        return false;
    }
    Client& client = clientIt->second;

    char buffer[1024];
    ssize_t bytesRead = recv(fd, buffer, sizeof(buffer), 0);

    // poll() a signale POLLIN : soit il y a des octets, soit le pair a ferme.
    // 0 = fermeture propre, < 0 = vraie erreur -> on deconnecte dans les deux
    // cas. errno n'est JAMAIS consulte, le sujet l'interdit.
    if (bytesRead <= 0) {
        std::cout << "[INFO] Client FD " << fd << " disconnected." << std::endl;
        this->_disconnectClient(fd);
        return false;
    }

    client.appendInputBuffer(std::string(buffer, bytesRead));

    // Reste d'une ligne trop longue deja abandonnee : on l'ignore jusqu'a son
    // saut de ligne, sinon elle se collerait devant la commande suivante.
    if (client.isSkippingLine() && !client.resyncAfterOversizedLine()) {
        return true; // fin de la ligne fautive pas encore vue
    }

    // TCP est un flux d'octets, pas de messages : on traite toutes les lignes
    // completes disponibles, ce qui recolle au passage les envois fragmentes.
    std::string commandLine;
    while (client.extractNextLine(commandLine)) {
        IrcMessage message = Parser::parse(commandLine);
        e_command  type = resolveCommand(message.command);

        if (type == CMD_UNKNOWN) {
            // Une ligne vide n'est pas une tentative de commande, juste du bruit
            // de protocole : rien a repondre.
            if (!message.command.empty()) {
                this->sendToClient(fd, ERR_UNKNOWNCOMMAND(client.getNickname(), message.command));
            }
            continue;
        }

        if (!this->_processCommand(fd, client, message, type)) {
            // QUIT vient de fermer : `client` est une reference pendante dans une
            // entree effacee et _pollFds a bouge. On ne touche plus a rien.
            return false;
        }
    }

    // Ce qui reste est une ligne incomplete. Au-dela du plafond ce n'est plus un
    // paquet fragmente legitime : on la jette et on arme la resynchronisation,
    // pour n'envoyer qu'un seul 417 par ligne fautive.
    if (client.getInputBuffer().size() > MAX_PENDING_LINE) {
        std::cerr << "[WARN] FD " << fd << " pending line over "
                  << MAX_PENDING_LINE << " bytes: line dropped" << std::endl;
        client.markLineOversized();
        this->sendToClient(fd, ERR_INPUTTOOLONG(client.getNickname()));
    }
    return true;
}

// Aiguillage central. PASS/NICK/USER/QUIT/CAP/PING/PONG sont traites AVANT la
// barriere isRegistered, qui filtre tout le reste une fois pour toutes.
bool Server::_processCommand(int fd, Client& client, const IrcMessage& message, e_command type) {
    switch (type) {
        case CMD_ENUM_PASS:
            this->_handlePass(fd, client, message);
            return true;
        case CMD_ENUM_NICK:
            this->_handleNick(fd, client, message);
            return true;
        case CMD_ENUM_USER:
            this->_handleUser(fd, client, message);
            return true;
        case CMD_ENUM_QUIT: {
            // "QUIT :raison" -> la raison est reprise dans le QUIT diffuse aux
            // channels ; "QUIT" tout court retombe sur le libelle par defaut.
            std::string reason = message.params.empty() ? std::string("Client exited")
                                                        : message.params[0];
            this->_disconnectClient(fd, reason);
            return false;
        }
        case CMD_ENUM_CAP:
            // Negociation IRCv3, qu'Irssi envoie en toute premiere ligne. Non
            // implementee, ignoree en silence pour ne pas polluer son affichage.
            return true;
        case CMD_ENUM_PING:
            // Traite ICI, avant la barriere : un client peut pinguer pendant la
            // poignee de main, et un PING ne doit jamais recevoir 451.
            this->_handlePing(fd, message);
            return true;
        case CMD_ENUM_PONG:
            // Reponse a un PING : rien a renvoyer, juste ne pas tomber sur 451.
            return true;
        default:
            break;
    }

    if (!client.getIsRegistered()) {
        this->sendToClient(fd, ERR_NOTREGISTERED(client.getNickname()));
        return true;
    }
    if (type == CMD_ENUM_PRIVMSG) {
        this->_handlePrivmsg(fd, client, message);
        return true;
    }
    if (type == CMD_ENUM_JOIN) {
        this->_handleJoin(fd, client, message);
        return true;
    }
    if (type == CMD_ENUM_PART) {
        this->_handlePart(fd, client, message);
        return true;
    }
    if (type == CMD_ENUM_TOPIC) {
        this->_handleTopic(fd, client, message);
        return true;
    }
    if (type == CMD_ENUM_MODE) {
        this->_handleMode(fd, client, message);
        return true;
    }
    if (type == CMD_ENUM_KICK) {
        this->_handleKick(fd, client, message);
        return true;
    }
    if (type == CMD_ENUM_INVITE) {
        this->_handleInvite(fd, client, message);
        return true;
    }
    return true;
}

// PING <token> -> PONG ft_irc :<token>. Le token doit etre renvoye tel quel,
// sinon le client juge la connexion morte et coupe sur timeout.
void Server::_handlePing(int fd, const IrcMessage& message) {
    std::string token = message.params.empty() ? std::string() : message.params[0];
    this->sendToClient(fd, "PONG ft_irc :" + token + "\r\n");
}

// Appelee quand poll() signale POLLOUT sur une socket cliente.
void Server::_writeClientData(int fd) {
    std::map<int, Client>::iterator clientIt = this->_clients.find(fd);
    if (clientIt == this->_clients.end()) {
        return;
    }
    Client& client = clientIt->second;

    if (!client.hasOutputData()) {
        // Plus rien a ecrire : on desarme POLLOUT pour que poll() cesse de se
        // reveiller sur un evenement dont on n'a plus rien a faire.
        std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
        if (pollIt != this->_pollFds.end()) {
            pollIt->events &= ~POLLOUT;
        }
        return;
    }

    const std::string& outputBuffer = client.getOutputBuffer();
    ssize_t bytesSent = send(fd, outputBuffer.c_str(), outputBuffer.size(), 0);

    // poll() a signale POLLOUT : la socket a de la place, donc send() ecrit au
    // moins un octet ou echoue pour de bon. Comme pour recv(), pas d'errno : un
    // echec vaut deconnexion, on ne reessaie pas sur la base d'un EAGAIN.
    if (bytesSent < 0) {
        std::cerr << "[ERROR] Send failure on FD " << fd << std::endl;
        this->_disconnectClient(fd);
        return;
    }

    // Seuls les octets reellement acceptes par le noyau sont retires : sur une
    // socket non bloquante, send() peut n'en prendre qu'une partie.
    client.clearOutputBuffer(static_cast<size_t>(bytesSent));

    if (!client.hasOutputData()) {
        std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
        if (pollIt != this->_pollFds.end()) {
            pollIt->events &= ~POLLOUT;
        }
    }
}

// Point de sortie unique, quelle que soit la cause : QUIT, fermeture propre,
// socket tuee, erreur. Nettoie les trois registres ensemble.
void Server::_disconnectClient(int fd, const std::string& reason) {
    // Le QUIT part AVANT tout retrait, pour que _broadcastToPeers voie encore
    // les channels du partant. Rien pour un client jamais enregistre : personne
    // ne connait son pseudo.
    std::map<int, Client>::iterator clientIt = this->_clients.find(fd);
    if (clientIt != this->_clients.end() && clientIt->second.getIsRegistered()) {
        std::string quitMsg = ":" + clientIt->second.getPrefix() + " QUIT :" + reason + "\r\n";
        this->_broadcastToPeers(fd, quitMsg, false);
    }

    // Retrait de tous les channels, puis purge de ceux devenus vides.
    for (std::map<std::string, Channel>::iterator it = this->_channels.begin();
         it != this->_channels.end(); ) {
        it->second.removeMember(fd);
        it->second.removeOperator(fd);
        // _invited aussi : les fds sont recycles par le noyau, et un nouveau
        // client heriterait sinon de l'invitation de l'ancien sous +i.
        it->second.removeInvited(fd);

        if (it->second.isEmpty()) {
            this->_channels.erase(it++);   // pattern d'incrémentation avant erase — obligatoire en C++98
        } else {
            ++it;
        }
    }
    close(fd);

    std::vector<struct pollfd>::iterator pollIt = this->_findPollFd(fd);
    if (pollIt != this->_pollFds.end()) {
        this->_pollFds.erase(pollIt);
    }

    // Remove client entry from map registry
    this->_clients.erase(fd);
}

// Recherche lineaire de l'entree pollfd d'un fd, centralisee ici parce que tous
// les endroits qui basculent POLLOUT en ont besoin.
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

// Boucle principale, et unique poll() du projet.
void Server::start() {
    this->_initServerSocket();
    this->_isRunning = true;

    std::cout << "[SUCCESS] IRC Server listening on port " << this->_port << "..." << std::endl;

    while (this->_isRunning) {
        // Le -1 = pas de delai maximum : le serveur dort a 0% de CPU tant que
        // rien n'arrive sur aucune des sockets surveillees.
        int pollCount = poll(&this->_pollFds[0], this->_pollFds.size(), -1);

        if (pollCount < 0) {
            // Cas normal : le handler SIGINT/SIGQUIT a mis _isRunning a false.
            // Sinon on rappelle simplement poll(), sans lire errno, pour que tout
            // le serveur reste exempt de logique pilotee par errno.
            if (!this->_isRunning) {
                break;
            }
            continue;
        }

        // Parcours par indice : _disconnectClient() efface dans _pollFds, ce qui
        // fait glisser d'une case tous les elements suivants (d'ou les --i).
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

            // Filet de securite : une exception remontant d'un handler coute la
            // connexion fautive, dont le retrait libere la memoire, jamais le
            // process entier.
            try {
                // Un raccrochage ou une erreur passe avant tout le reste : le
                // client est parti, on n'y touche plus.
                if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    this->_disconnectClient(fd);
                    --i;
                    continue;
                }

                if (revents & POLLIN) {
                    if (!this->_readClientData(fd)) {
                        --i; // fd deconnecte : _pollFds a glisse, on reexamine i
                        continue;
                    }
                }

                if (revents & POLLOUT) {
                    this->_writeClientData(fd);
                }
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] FD " << fd << " dropped after exception: "
                          << e.what() << std::endl;
                this->_disconnectClient(fd);
                --i;
            }
        }
    }
}

// Seule facon pour un handler de repondre a un client. Le send() reel n'a lieu
// que dans la boucle poll(), via _writeClientData(), jamais ici.
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

// Ne fait que baisser le drapeau : stop() ferme des sockets et vide des
// conteneurs STL, ce qui n'est pas sur depuis un handler de signal.
void Server::requestStop() {
    this->_isRunning = false;
}

// Ferme toutes les sockets et vide les registres.
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
