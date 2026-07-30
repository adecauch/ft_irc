#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"

#include <cctype>
#include <cstddef>

/* ========================================================================== */
/*                      REGISTRATION HANDSHAKE: PASS / NICK / USER            */
/*                                                                            */
/*  Methodes de Server, declarees dans include/Server.hpp, sorties ici pour   */
/*  garder Server.cpp concentre sur le coeur reseau.                          */
/* ========================================================================== */

namespace {

// Les pseudos IRC sont insensibles a la casse : "Bob" et "bob" collisionnent.
bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Lettre, puis jusqu'a 8 caracteres alphanumeriques ou speciaux autorises.
// Tolerant a dessein : refuser un pseudo propose par un vrai client casserait tout.
bool isValidNickname(const std::string& nick) {
    if (nick.empty() || nick.size() > 9) {
        return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(nick[0]))) {
        return false;
    }
    for (std::size_t i = 1; i < nick.size(); ++i) {
        char c = nick[i];
        bool ok = std::isalnum(static_cast<unsigned char>(c)) ||
                  c == '-' || c == '[' || c == ']' || c == '\\' ||
                  c == '`' || c == '_' || c == '^' || c == '{' || c == '}' || c == '|';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}

// PASS <password> : compare au mot de passe passe en argument du programme.
void Server::_handlePass(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty()) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }
    if (client.getIsRegistered()) {
        this->sendToClient(fd, ERR_ALREADYREGISTRED(client.getNickname()));
        return;
    }

    client.setHasGivenPassword(message.params[0] == this->_password);
    if (!client.getHasGivenPassword()) {
        this->sendToClient(fd, ERR_PASSWDMISMATCH(client.getNickname()));
        return;
    }

    this->_tryCompleteRegistration(fd, client);
}

// NICK <nickname> : pose ou change le pseudo.
void Server::_handleNick(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty() || message.params[0].empty()) {
        this->sendToClient(fd, ERR_NONICKNAMEGIVEN(client.getNickname()));
        return;
    }

    const std::string& newNick = message.params[0];

    if (!isValidNickname(newNick)) {
        this->sendToClient(fd, ERR_ERRONEUSNICKNAME(client.getNickname(), newNick));
        return;
    }

    if (this->_isNicknameInUse(newNick, fd)) {
        this->sendToClient(fd, ERR_NICKNAMEINUSE(client.getNickname(), newNick));
        return;
    }

    if (client.getNickname() == newNick) {
        return;
    }

    // Construit AVANT le renommage : un NICK s'annonce avec l'ANCIENNE identite
    // en prefixe, sinon le destinataire ne peut plus faire le lien.
    std::string nickMsg = ":" + client.getPrefix() + " NICK :" + newNick + "\r\n";

    client.setNickname(newNick);

    // Avant l'enregistrement personne ne connait ce client : rien a annoncer, le
    // pseudo fera partie de la salve 001-004.
    if (client.getIsRegistered()) {
        this->_broadcastToPeers(fd, nickMsg, true);
        return;
    }

    this->_tryCompleteRegistration(fd, client);
}

// USER <username> <mode> <unused> :<realname>. <mode> et <unused> sont ignores,
// comme le font les vrais serveurs : ce sont des vestiges historiques.
void Server::_handleUser(int fd, Client& client, const IrcMessage& message) {
    if (message.params.size() < 4) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }
    if (client.getIsRegistered()) {
        this->sendToClient(fd, ERR_ALREADYREGISTRED(client.getNickname()));
        return;
    }

    client.setUsername(message.params[0]);
    client.setRealname(message.params[3]);

    this->_tryCompleteRegistration(fd, client);
}

// excludeFd laisse un client garder ou reprendre son propre pseudo sans se
// heurter a lui-meme.
bool Server::_isNicknameInUse(const std::string& nickname, int excludeFd) const {
    for (std::map<int, Client>::const_iterator it = this->_clients.begin(); it != this->_clients.end(); ++it) {
        if (it->first == excludeFd) {
            continue;
        }
        if (equalsIgnoreCase(it->second.getNickname(), nickname)) {
            return true;
        }
    }
    return false;
}

// Appelee apres chaque succes de PASS/NICK/USER. Idempotente : ne fait rien
// tant que les trois ne sont pas la, et rien de plus une fois la salve envoyee.
void Server::_tryCompleteRegistration(int fd, Client& client) {
    if (client.getIsRegistered()) {
        return;
    }
    if (!client.getHasGivenPassword()) {
        return;
    }
    if (client.getNickname() == "*") {
        return;
    }
    if (client.getUsername().empty()) {
        return;
    }

    client.setIsRegistered(true);

    const std::string& nick = client.getNickname();
    this->sendToClient(fd, RPL_WELCOME(nick));
    this->sendToClient(fd, RPL_YOURHOST(nick));
    this->sendToClient(fd, RPL_CREATED(nick, this->_createdAt));
    this->sendToClient(fd, RPL_MYINFO(nick));

    std::cout << "[INFO] FD " << fd << " fully registered as \"" << nick << "\"" << std::endl;
}
