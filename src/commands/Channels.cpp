/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Channels.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 15:09:06 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:11:42 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>

//CHANNEL OPERATIONS

namespace {

// Dupliquee depuis Registration.cpp/Messaging.cpp : choix de l'etape 6 pour ne
// pas toucher aux fichiers existants pendant le travail a deux.
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

// Convertit un entier non signe en chaine (pas de std::to_string en C++98).
std::string sizeToString(std::size_t n) {
    std::ostringstream oss;
    oss << n;
    return oss.str();
}

// Valide l'argument d'un mode +l : un entier strictement positif uniquement.
// Renvoie false (out intact) sur chaine vide, non numerique, nulle, ou trop grande.
bool parsePositiveInt(const std::string& s, std::size_t& out) {
    if (s.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    std::istringstream iss(s);
    unsigned long value = 0;
    iss >> value;
    if (iss.fail() || value == 0) {
        return false;
    }
    out = static_cast<std::size_t>(value);
    return true;
}

// Construit "+modes [args]" pour RPL_CHANNELMODEIS (324), dans l'ordre i,t,k,l.
// La cle et la limite n'apparaissent que si leur mode est actif.
std::string buildModeString(const Channel& channel) {
    std::string modes = "+";
    std::string args;
    if (channel.isInviteOnly()) {
        modes += "i";
    }
    if (channel.isTopicRestricted()) {
        modes += "t";
    }
    if (channel.hasKey()) {
        modes += "k";
        args += " " + channel.getKey();
    }
    if (channel.hasUserLimit()) {
        modes += "l";
        args += " " + sizeToString(channel.getUserLimit());
    }
    return modes + args;
}

}

// Recherche insensible a la casse : la cle stockee garde la casse du premier
// JOIN qui a cree le channel.
Channel* Server::_findChannel(const std::string& name) {
    for (std::map<std::string, Channel>::iterator it = this->_channels.begin();
         it != this->_channels.end(); ++it) {
        if (equalsIgnoreCase(it->second.getName(), name)) {
            return &it->second;
        }
    }
    return NULL;
}

// excludeFd = -1 (jamais un fd valide) quand l'emetteur doit aussi recevoir son
// propre message ; son fd pour PRIVMSG, qui ne s'echoie pas a lui-meme.
void Server::_broadcastToChannel(const Channel& channel, const std::string& message, int excludeFd) {
    const std::set<int>& members = channel.getMembers();
    for (std::set<int>::const_iterator it = members.begin(); it != members.end(); ++it) {
        if (*it != excludeFd) {
            this->sendToClient(*it, message);
        }
    }
}

// Pour QUIT et NICK, qui concernent le client lui-meme et non un channel precis.
// Le set dedoublonne : sinon qui partage deux channels recevrait le message deux fois.
void Server::_broadcastToPeers(int fd, const std::string& message, bool includeSelf) {
    std::set<int> recipients;

    for (std::map<std::string, Channel>::const_iterator it = this->_channels.begin();
         it != this->_channels.end(); ++it) {
        if (!it->second.isMember(fd)) {
            continue;
        }
        const std::set<int>& members = it->second.getMembers();
        recipients.insert(members.begin(), members.end());
    }

    if (includeSelf) {
        recipients.insert(fd);   // meme sans aucun channel en commun
    } else {
        recipients.erase(fd);
    }

    for (std::set<int>::const_iterator it = recipients.begin(); it != recipients.end(); ++it) {
        this->sendToClient(*it, message);
    }
}

// JOIN <#channel>[,<#chan2>...] [<key>[,<key2>...]]. Seuls le premier channel
// et la premiere cle sont traites : le sujet n'exige pas le multi-JOIN.
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
        // Channel inexistant : on le cree, et son createur devient membre ET
        // operateur.
        this->_channels[chanName] = Channel(chanName);
        channel = &this->_channels[chanName];
        channel->addMember(fd);
        channel->addOperator(fd);
    } else {
        if (channel->isMember(fd)) {
            // Pas de code d'erreur IRC pour ce cas : on ne fait rien, ni erreur
            // ni rediffusion, donc un flood de JOIN ne produit aucun doublon.
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

    // L'invitation est consommee des l'entree : elle ne vaut que pour un JOIN.
    channel->removeInvited(fd);

    // JOIN s'envoie aussi a l'emetteur (contrairement a PRIVMSG) : c'est ce qui
    // fait ouvrir sa fenetre de channel cote client.
    std::string joinMsg = ":" + client.getPrefix() + " JOIN " + chanName + "\r\n";
    this->_broadcastToChannel(*channel, joinMsg, -1);

    if (channel->hasTopic()) {
        this->sendToClient(fd, RPL_TOPIC(client.getNickname(), chanName, channel->getTopic()));
    } else {
        this->sendToClient(fd, RPL_NOTOPIC(client.getNickname(), chanName));
    }

    // RPL_NAMREPLY attend une liste deja concatenee, "@op1 membre2 ...".
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

// PART <#channel>[,<#channel2>...] [:<raison>]
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

    // PART s'envoie aussi a l'emetteur, meme logique que JOIN.
    std::string partMsg = ":" + client.getPrefix() + " PART " + chanName;
    if (message.params.size() > 1 && !message.params[1].empty()) {
        partMsg += " :" + message.params[1];
    }
    partMsg += "\r\n";
    this->_broadcastToChannel(*channel, partMsg, -1);

    channel->removeMember(fd);
    channel->removeOperator(fd);
    channel->removeInvited(fd);

    if (channel->isEmpty()) {
        // La cle est COPIEE : passer channel->getName() donnerait a erase() une
        // reference vers une chaine membre de l'objet qu'il va detruire.
        std::string storedKey = channel->getName();
        this->_channels.erase(storedKey);
    }
}

// TOPIC <#channel> [:<topic>]. Une commande, deux comportements : sans argument
// topic elle LIT (331/332), avec un argument elle ECRIT (vide = effacer).
void Server::_handleTopic(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty()) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }

    const std::string& chanName = message.params[0];
    Channel* channel = this->_findChannel(chanName);
    if (channel == NULL) {
        this->sendToClient(fd, ERR_NOSUCHCHANNEL(client.getNickname(), chanName));
        return;
    }
    if (!channel->isMember(fd)) {
        this->sendToClient(fd, ERR_NOTONCHANNEL(client.getNickname(), chanName));
        return;
    }

    // Aucun argument topic (params.size() == 1) -> simple consultation.
    if (message.params.size() < 2) {
        if (channel->hasTopic()) {
            this->sendToClient(fd, RPL_TOPIC(client.getNickname(), chanName, channel->getTopic()));
        } else {
            this->sendToClient(fd, RPL_NOTOPIC(client.getNickname(), chanName));
        }
        return;
    }

    // Ecriture reservee aux operateurs uniquement si le channel est en +t ;
    // sinon n'importe quel membre peut changer le sujet (RFC 2812).
    if (channel->isTopicRestricted() && !channel->isOperator(fd)) {
        this->sendToClient(fd, ERR_CHANOPRIVSNEEDED(client.getNickname(), chanName));
        return;
    }

    const std::string& newTopic = message.params[1];
    if (newTopic.empty()) {
        channel->clearTopic();
    } else {
        channel->setTopic(newTopic);
    }

    // Diffuse a tous, emetteur inclus, pour que chaque client rafraichisse le
    // titre du channel.
    std::string topicMsg = ":" + client.getPrefix() + " TOPIC " + chanName + " :" + newTopic + "\r\n";
    this->_broadcastToChannel(*channel, topicMsg, -1);
}

// MODE <#channel> [<modestring> [<arg>...]]. Sans modestring c'est une
// consultation (324) ; avec, une modification reservee aux operateurs (482).
void Server::_handleMode(int fd, Client& client, const IrcMessage& message) {
    if (message.params.empty()) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }

    const std::string& target = message.params[0];
    if (target.empty() || target[0] != '#') {
        return; // mode utilisateur : hors perimetre, ignore sans erreur
    }

    Channel* channel = this->_findChannel(target);
    if (channel == NULL) {
        this->sendToClient(fd, ERR_NOSUCHCHANNEL(client.getNickname(), target));
        return;
    }
    if (!channel->isMember(fd)) {
        this->sendToClient(fd, ERR_NOTONCHANNEL(client.getNickname(), target));
        return;
    }

    // Pas de modestring -> simple consultation des modes actifs (324).
    if (message.params.size() < 2) {
        this->sendToClient(fd, RPL_CHANNELMODEIS(client.getNickname(), target, buildModeString(*channel)));
        return;
    }

    // Toute modification exige le statut d'operateur du channel.
    if (!channel->isOperator(fd)) {
        this->sendToClient(fd, ERR_CHANOPRIVSNEEDED(client.getNickname(), target));
        return;
    }

    this->_applyModeChanges(fd, client, *channel, message);
}

// Lit le modestring de gauche a droite : un '+'/'-' fixe le sens, k/o/l
// consomment un argument dans l'ordre, i/t non. Seul ce qui a change est diffuse.
void Server::_applyModeChanges(int fd, Client& client, Channel& channel, const IrcMessage& message) {
    const std::string& modestring = message.params[1];
    std::size_t argIndex = 2;                 // params[2..] = arguments des modes
    bool adding = true;                       // sens courant, '+' par defaut
    std::string appliedModes;                 // ce qui a change, ex "+ot-l"
    std::vector<std::string> appliedArgs;     // arguments correspondants, dans l'ordre
    char lastSign = 0;                        // pour ne reecrire +/- qu'au changement

    for (std::size_t i = 0; i < modestring.size(); ++i) {
        char c = modestring[i];
        if (c == '+') { adding = true; continue; }
        if (c == '-') { adding = false; continue; }

        bool applied = false;
        std::string appliedArg;

        switch (c) {
            case 'i':
                channel.setInviteOnly(adding);
                applied = true;
                break;
            case 't':
                channel.setTopicRestricted(adding);
                applied = true;
                break;
            case 'k':
                if (adding) {
                    if (argIndex >= message.params.size() || message.params[argIndex].empty()) {
                        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
                        break;
                    }
                    channel.setKey(message.params[argIndex]);
                    appliedArg = message.params[argIndex];
                    ++argIndex;
                    applied = true;
                } else {
                    // -k : on consomme la cle si elle est presente, pour ne pas
                    // decaler les arguments suivants. Le retrait a lieu quand meme.
                    if (argIndex < message.params.size()) {
                        ++argIndex;
                    }
                    channel.removeKey();
                    applied = true;
                }
                break;
            case 'l':
                if (adding) {
                    if (argIndex >= message.params.size()) {
                        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
                        break;
                    }
                    std::size_t limit = 0;
                    if (!parsePositiveInt(message.params[argIndex], limit)) {
                        ++argIndex; // argument fourni mais invalide : consomme et ignore
                        break;
                    }
                    channel.setUserLimit(limit);
                    appliedArg = message.params[argIndex];
                    ++argIndex;
                    applied = true;
                } else {
                    channel.removeUserLimit();
                    applied = true;
                }
                break;
            case 'o': {
                if (argIndex >= message.params.size()) {
                    this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
                    break;
                }
                const std::string& targetNick = message.params[argIndex];
                ++argIndex;
                Client* member = this->_findClientByNickname(targetNick);
                if (member == NULL || !channel.isMember(member->getFd())) {
                    this->sendToClient(fd, ERR_USERNOTINCHANNEL(client.getNickname(), targetNick, channel.getName()));
                    break;
                }
                if (adding) {
                    channel.addOperator(member->getFd());
                } else {
                    channel.removeOperator(member->getFd());
                }
                appliedArg = targetNick;
                applied = true;
                break;
            }
            default:
                // Mode non supporte (ex. 'b') : ignore, jamais de crash.
                break;
        }

        if (applied) {
            char sign = adding ? '+' : '-';
            if (sign != lastSign) {
                appliedModes += sign;
                lastSign = sign;
            }
            appliedModes += c;
            if (!appliedArg.empty()) {
                appliedArgs.push_back(appliedArg);
            }
        }
    }

    if (appliedModes.empty()) {
        return; // rien n'a change (tout ignore/en erreur) -> pas de diffusion
    }

    std::string modeMsg = ":" + client.getPrefix() + " MODE " + channel.getName() + " " + appliedModes;
    for (std::size_t i = 0; i < appliedArgs.size(); ++i) {
        modeMsg += " " + appliedArgs[i];
    }
    modeMsg += "\r\n";
    this->_broadcastToChannel(channel, modeMsg, -1);
}

// KICK <#channel> <user> [:<comment>]. Le message part AVANT le retrait, pour que
// le client ejecte recoive sa propre ejection et ferme sa fenetre.
void Server::_handleKick(int fd, Client& client, const IrcMessage& message) {
    if (message.params.size() < 2) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }

    std::string chanName = message.params[0];
    std::string::size_type nameComma = chanName.find(',');
    if (nameComma != std::string::npos) {
        chanName = chanName.substr(0, nameComma);
    }

    std::string targetNick = message.params[1];
    std::string::size_type nickComma = targetNick.find(',');
    if (nickComma != std::string::npos) {
        targetNick = targetNick.substr(0, nickComma);
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
    if (!channel->isOperator(fd)) {
        this->sendToClient(fd, ERR_CHANOPRIVSNEEDED(client.getNickname(), chanName));
        return;
    }

    Client* target = this->_findClientByNickname(targetNick);
    if (target == NULL || !channel->isMember(target->getFd())) {
        this->sendToClient(fd, ERR_USERNOTINCHANNEL(client.getNickname(), targetNick, chanName));
        return;
    }

    // Commentaire optionnel ; a defaut le pseudo du kickeur (convention usuelle).
    std::string comment = (message.params.size() > 2 && !message.params[2].empty())
                          ? message.params[2] : client.getNickname();

    // Pseudo canonique du kicke, pas ce que le kickeur a tape : c'est celui-la que
    // son propre client reconnaitra pour se retirer.
    std::string kickMsg = ":" + client.getPrefix() + " KICK " + chanName + " "
                          + target->getNickname() + " :" + comment + "\r\n";
    this->_broadcastToChannel(*channel, kickMsg, -1);

    channel->removeMember(target->getFd());
    channel->removeOperator(target->getFd());
    channel->removeInvited(target->getFd());

    if (channel->isEmpty()) {
        // Meme logique que PART : cle exacte stockee et copiee avant l'erase.
        std::string storedKey = channel->getName();
        this->_channels.erase(storedKey);
    }
}

// INVITE <nick> <#channel>. Attention, l'ordre des arguments est l'INVERSE de
// KICK (pseudo d'abord, channel ensuite) : c'est la RFC 2812.
void Server::_handleInvite(int fd, Client& client, const IrcMessage& message) {
    if (message.params.size() < 2) {
        this->sendToClient(fd, ERR_NEEDMOREPARAMS(client.getNickname(), message.command));
        return;
    }

    const std::string& targetNick = message.params[0];
    const std::string& chanName = message.params[1];

    Channel* channel = this->_findChannel(chanName);
    if (channel == NULL) {
        this->sendToClient(fd, ERR_NOSUCHCHANNEL(client.getNickname(), chanName));
        return;
    }
    if (!channel->isMember(fd)) {
        this->sendToClient(fd, ERR_NOTONCHANNEL(client.getNickname(), chanName));
        return;
    }
    // Op requis uniquement quand le channel est en +i (RFC 2812) : sur un
    // channel ouvert, n'importe quel membre peut inviter.
    if (channel->isInviteOnly() && !channel->isOperator(fd)) {
        this->sendToClient(fd, ERR_CHANOPRIVSNEEDED(client.getNickname(), chanName));
        return;
    }

    Client* target = this->_findClientByNickname(targetNick);
    if (target == NULL) {
        this->sendToClient(fd, ERR_NOSUCHNICK(client.getNickname(), targetNick));
        return;
    }
    if (channel->isMember(target->getFd())) {
        this->sendToClient(fd, ERR_USERONCHANNEL(client.getNickname(), target->getNickname(), chanName));
        return;
    }

    channel->invite(target->getFd());

    // 341 confirme a l'inviteur, puis l'invite recoit la notification standard.
    this->sendToClient(fd, RPL_INVITING(client.getNickname(), target->getNickname(), chanName));
    std::string inviteMsg = ":" + client.getPrefix() + " INVITE "
                            + target->getNickname() + " " + chanName + "\r\n";
    this->sendToClient(target->getFd(), inviteMsg);
}
