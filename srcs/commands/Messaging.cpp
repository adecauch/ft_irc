#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"
#include <cctype>


// compare les caractere pas de diff entre minuscule majuscule
namespace {
bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}
}

// cherce dans la map stockant tout les cleint l'adresse du client et le renvoie
Client* Server::_findClientByNickname(const std::string& nickname) {
    for (std::map<int, Client>::iterator it = this->_clients.begin();
         it != this->_clients.end(); ++it) {
        if (equalsIgnoreCase(it->second.getNickname(), nickname)) {
            return &it->second;
        }
    }
    return NULL;
}

// [ROLE] : PRIVMSG <nick> :<message> — message privé entre deux clients.
void Server::_handlePrivmsg(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty()) {
        this->sendToClient(fd, ERR_NORECIPIENT(client.getNickname()));   // 411
        return;
    }
    if (message.params.size() < 2 || message.params[1].empty()) {
        this->sendToClient(fd, ERR_NOTEXTTOSEND(client.getNickname()));  // 412
        return;
    }

    const std::string& targetNick = message.params[0];
    Client* target = this->_findClientByNickname(targetNick);
    if (target == NULL) {
        this->sendToClient(fd, ERR_NOSUCHNICK(client.getNickname(), targetNick)); // 401
        return;
    }
// construction puis envoie du bon messages
    std::string out = ":" + client.getNickname() + " PRIVMSG " + targetNick
                     + " :" + message.params[1] + "\r\n";
    this->sendToClient(target->getFd(), out); 
}