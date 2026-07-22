#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"

#include <cctype>
#include <cstddef>

/* ========================================================================== */
/*                      CHANNEL OPERATIONS: JOIN / PART                       */
/*                                                                            */
/*  Implements Server's channel-related private methods, declared in         */
/*  include/Server.hpp — split out here purely to keep Server.cpp focused    */
/*  on the network core, same convention as Registration.cpp/Messaging.cpp.  */
/* ========================================================================== */

namespace {

// Dupliquée depuis Registration.cpp/Messaging.cpp plutôt que partagée via un
// header commun : décision volontaire de l'étape 6 pour ne pas toucher aux
// fichiers existants (voir audit.md section 3).
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

}

// [ROLE] : Recherche un channel par nom (insensible à la casse), sur le
//          modèle exact de Server::_findClientByNickname.
// [WHY] : La clé stockée dans _channels garde la casse du premier JOIN qui a
//         créé le channel ; toute comparaison ultérieure doit donc être
//         insensible à la casse plutôt que de normaliser la clé elle-même.
Channel* Server::_findChannel(const std::string& name) {
    for (std::map<std::string, Channel>::iterator it = this->_channels.begin();
         it != this->_channels.end(); ++it) {
        if (equalsIgnoreCase(it->second.getName(), name)) {
            return &it->second;
        }
    }
    return NULL;
}

// [ROLE] : Envoie message à tous les membres de channel, sauf excludeFd.
// [WHEN TO USE] : excludeFd = -1 (jamais un fd valide) pour JOIN/PART, où
//                 l'émetteur doit aussi recevoir sa propre annonce ; le fd de
//                 l'émetteur pour PRIVMSG, qui ne s'échoie pas à lui-même.
void Server::_broadcastToChannel(const Channel& channel, const std::string& message, int excludeFd) {
    const std::set<int>& members = channel.getMembers();
    for (std::set<int>::const_iterator it = members.begin(); it != members.end(); ++it) {
        if (*it != excludeFd) {
            this->sendToClient(*it, message);
        }
    }
}

// [ROLE] : JOIN <#channel>[,<#channel2>...] [<key>[,<key2>...]]
// [WHY] : Ne traite que le premier channel/clé de la liste — le sujet
//         n'exige pas le multi-JOIN ; garder la signature ouverte à cette
//         extension plus tard reste possible sans la casser (audit 5.1).
void Server::_handleJoin(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty()) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }

    std::string chanName = message.params[0];
    std::string::size_type nameComma = chanName.find(',');
    if (nameComma != std::string::npos) {
        chanName = chanName.substr(0, nameComma);
    }

    if (chanName.empty() || chanName[0] != '#') {
        this->sendToClient(fd, ERR_BADCHANMASK(client.getNickname(), chanName));
        return;
    }

    std::string key;
    if (message.params.size() > 1) {
        std::string::size_type keyComma = message.params[1].find(',');
        key = (keyComma != std::string::npos) ? message.params[1].substr(0, keyComma) : message.params[1];
    }

    Channel* channel = this->_findChannel(chanName);
    bool isNewChannel = (channel == NULL);

    if (isNewChannel) {
        // Le channel n'existe pas encore : le créer, le créateur devient
        // membre ET opérateur (roadmap section 2).
        this->_channels[chanName] = Channel(chanName);
        channel = &this->_channels[chanName];
        channel->addMember(fd);
        channel->addOperator(fd);
    } else {
        if (channel->isMember(fd)) {
            // Cas limite sans code d'erreur dédié en IRC : on ne fait rien,
            // pas de re-broadcast, pas de doublon (audit 5.1 point 3).
            return;
        }
        if (channel->isInviteOnly() && !channel->isInvited(fd)) {
            this->sendToClient(fd, ERR_INVITEONLYCHAN(client.getNickname(), chanName));
            return;
        }
        if (channel->hasKey() && key != channel->getKey()) {
            this->sendToClient(fd, ERR_BADCHANNELKEY(client.getNickname(), chanName));
            return;
        }
        if (channel->hasUserLimit() && channel->memberCount() >= channel->getUserLimit()) {
            this->sendToClient(fd, ERR_CHANNELISFULL(client.getNickname(), chanName));
            return;
        }
        channel->addMember(fd);
    }

    // JOIN s'envoie aussi à l'émetteur (contrairement à PRIVMSG) : c'est ce
    // qui permet au client de référence d'ouvrir sa fenêtre de channel.
    std::string joinMsg = ":" + client.getNickname() + " JOIN " + chanName + "\r\n";
    this->_broadcastToChannel(*channel, joinMsg, -1);

    if (channel->hasTopic()) {
        this->sendToClient(fd, RPL_TOPIC(client.getNickname(), chanName, channel->getTopic()));
    } else {
        this->sendToClient(fd, RPL_NOTOPIC(client.getNickname(), chanName));
    }

    // RPL_NAMREPLY attend une liste déjà concaténée, "@op1 member2 ..."
    // (NumericReplies.hpp ne le fait pas à notre place).
    std::string names;
    const std::set<int>& members = channel->getMembers();
    for (std::set<int>::const_iterator it = members.begin(); it != members.end(); ++it) {
        std::map<int, Client>::iterator memberIt = this->_clients.find(*it);
        if (memberIt == this->_clients.end()) {
            continue;
        }
        if (!names.empty()) {
            names += " ";
        }
        if (channel->isOperator(*it)) {
            names += "@";
        }
        names += memberIt->second.getNickname();
    }
    this->sendToClient(fd, RPL_NAMREPLY(client.getNickname(), chanName, names));
    this->sendToClient(fd, RPL_ENDOFNAMES(client.getNickname(), chanName));
}

// [ROLE] : PART <#channel>[,<#channel2>...] [:<raison>]
void Server::_handlePart(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty()) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }

    std::string chanName = message.params[0];
    std::string::size_type nameComma = chanName.find(',');
    if (nameComma != std::string::npos) {
        chanName = chanName.substr(0, nameComma);
    }

    Channel* channel = this->_findChannel(chanName);
    if (channel == NULL) {
        this->sendToClient(fd, ERR_NOSUCHCHANNEL(client.getNickname(), chanName));
        return;
    }
    if (!channel->isMember(fd)) {
        this->sendToClient(fd, ERR_NOTONCHANNEL(client.getNickname(), chanName));
        return;
    }

    // PART s'envoie aussi à l'émetteur, même logique que JOIN.
    std::string partMsg = ":" + client.getNickname() + " PART " + chanName;
    if (message.params.size() > 1 && !message.params[1].empty()) {
        partMsg += " :" + message.params[1];
    }
    partMsg += "\r\n";
    this->_broadcastToChannel(*channel, partMsg, -1);

    channel->removeMember(fd);
    channel->removeOperator(fd);

    if (channel->isEmpty()) {
        // erase() par la clé exacte stockée (channel->getName()), pas par
        // chanName qui peut différer de casse selon ce que le client a tapé.
        this->_channels.erase(channel->getName());
    }
}
