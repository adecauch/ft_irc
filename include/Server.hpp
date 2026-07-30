#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include <set>
# include <cstring>
// Pas de <cerrno> : aucune decision du serveur ne repose sur errno, c'est
// poll() seul qui dit quand accept/recv/send peuvent etre appeles.
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
/*  Coeur reseau : socket d'ecoute, multiplexage par poll(), cycle de vie des */
/*  clients et des channels, dispatch des commandes.                          */
/* ========================================================================== */

class Server {

    private:
        int                         _port;
        std::string                 _password;

        // Socket d'ecoute, toujours en premiere position de _pollFds.
        int                         _serverSocketFd;

        // Passe a false par le handler SIGINT/SIGQUIT pour sortir de la boucle.
        bool                        _isRunning;

        // Tableau surveille par poll() : un pollfd par socket ouverte.
        std::vector<struct pollfd>  _pollFds;

        // Registres indexes par fd (clients) et par nom (channels).
        std::map<int, Client>       _clients;
        std::map<std::string, Channel> _channels;

        // Horodatage releve une seule fois au demarrage, pour RPL_CREATED (003).
        std::string                 _createdAt;

        // -----------------------------------------------------------------------------
        //                         Private Helper Functions
        // -----------------------------------------------------------------------------

        // socket, setsockopt, fcntl, bind, listen, puis ajout a _pollFds.
        void _initServerSocket();

        // accept() une connexion en attente et enregistre le nouveau client.
        void _acceptNewConnection();

        // Lit les octets dispos, recolle les lignes completes et les execute.
        // Renvoie false si le client a ete deconnecte pendant l'appel.
        bool _readClientData(int fd);

        // Envoie ce que send() accepte du buffer de sortie, desarme POLLOUT
        // quand il est vide.
        void _writeClientData(int fd);

        // Point de sortie unique : diffuse un QUIT, nettoie les trois registres.
        void _disconnectClient(int fd, const std::string& reason = "Client exited");

        // Retrouve l'entree pollfd d'un fd donne.
        std::vector<struct pollfd>::iterator _findPollFd(int fd);

        // Aiguillage central. PASS/NICK/USER/QUIT/CAP/PING/PONG passent avant la
        // barriere d'enregistrement ; tout le reste apres. false si QUIT a ferme.
        bool _processCommand(int fd, Client& client, const IrcMessage& message, e_command type);

        // Handlers, un par commande, definis dans srcs/commands/.
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

        // Recherches insensibles a la casse dans les deux registres.
        Client* _findClientByNickname(const std::string& nickname);
        Channel* _findChannel(const std::string& name);

        // Envoie a tous les membres d'un channel, sauf excludeFd (-1 = personne).
        void _broadcastToChannel(const Channel& channel, const std::string& message, int excludeFd);

        // Envoie a tous ceux qui partagent un channel avec fd, une seule fois
        // chacun. Pour QUIT et NICK, qui ne visent pas un channel precis.
        void _broadcastToPeers(int fd, const std::string& message, bool includeSelf);

        // Le pseudo est-il deja pris ? excludeFd laisse un client garder le sien.
        bool _isNicknameInUse(const std::string& nickname, int excludeFd) const;

        // Envoie 001-004 et passe isRegistered a true, une seule fois, quand
        // PASS + NICK + USER ont tous les trois abouti.
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
        Server(int port, const std::string& password);

        // -----------------------------------------------------------------------------
        //                         Core Execution Methods
        // -----------------------------------------------------------------------------

        // Ouvre la socket d'ecoute puis tourne dans la boucle poll().
        void start();

        // Ferme toutes les sockets et vide les registres.
        void stop();

        // Ne fait que baisser le drapeau : seule chose sure dans un handler de
        // signal, le vrai nettoyage a lieu plus tard dans stop().
        void requestStop();

        // Seule facon pour un handler de repondre : met en file et arme POLLOUT.
        // Le send() reel n'a lieu que dans la boucle poll(), jamais ici.
        void sendToClient(int fd, const std::string& message);

        // -----------------------------------------------------------------------------
        //                         Getters
        // -----------------------------------------------------------------------------
        int                getPort() const;
        const std::string& getPassword() const;
};

#endif
