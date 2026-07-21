#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"

#include <cctype>
#include <cstddef>

/* ========================================================================== */
/*                      REGISTRATION HANDSHAKE: PASS / NICK / USER            */
/*                                                                            */
/*  Implements Server's registration-related private methods, declared in    */
/*  include/Server.hpp alongside everything else — split out here purely to  */
/*  keep Server.cpp focused on the network core.                             */
/* ========================================================================== */

namespace {

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

// [WHY] : Loosely RFC 1459 (letter, then up to 8 letters/digits/"-"/specials).
//         Deliberately lenient rather than pedantic: rejecting a nickname a
//         real client tries by default would break "must connect without
//         error" for no benefit — this only needs to keep the protocol sane.
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

// [ROLE] : PASS <password> — validates against the server's own password.
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

// [ROLE] : NICK <nickname> — sets or changes the client's nickname.
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

    client.setNickname(newNick);
    this->_tryCompleteRegistration(fd, client);
}

// [ROLE] : USER <username> <mode> <unused> :<realname> — completes identity.
// [WHY] : <mode> and <unused> are historical/vestigial fields real ircds also
//         ignore; the hostname comes from the actual TCP connection, never
//         from anything the client claims.
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

// [ROLE] : Case-insensitive uniqueness check across every connected client.
// [WHY] : IRC nicknames are case-insensitive ("Bob" and "bob" collide).
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

// [ROLE] : Sends the 001-004 welcome burst once PASS+NICK+USER have all
//          succeeded, exactly once.
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
