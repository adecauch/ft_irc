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

    const std::string& target = message.params[0];

    // Une cible commençant par '#' est un channel, pas un nick (audit 5.3) :
    // même préfixe ":<nick>", mais diffusion à tous les membres sauf
    // l'émetteur au lieu d'un envoi à un seul destinataire.
    if (!target.empty() && target[0] == '#') {
        Channel* channel = this->_findChannel(target);
        if (channel == NULL) {
            this->sendToClient(fd, ERR_NOSUCHCHANNEL(client.getNickname(), target)); // 403
            return;
        }
        if (!channel->isMember(fd)) {
            this->sendToClient(fd, ERR_NOTONCHANNEL(client.getNickname(), target));  // 442
            return;
        }

        std::string out = ":" + client.getNickname() + " PRIVMSG " + target
                         + " :" + message.params[1] + "\r\n";
        this->_broadcastToChannel(*channel, out, fd);
        return;
    }

    Client* targetClient = this->_findClientByNickname(target);
    if (targetClient == NULL) {
        this->sendToClient(fd, ERR_NOSUCHNICK(client.getNickname(), target)); // 401
        return;
    }
// construction puis envoie du bon messages
    std::string out = ":" + client.getNickname() + " PRIVMSG " + target
                     + " :" + message.params[1] + "\r\n";
    this->sendToClient(targetClient->getFd(), out);
}