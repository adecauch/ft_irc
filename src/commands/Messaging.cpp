/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Messaging.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 15:10:34 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:11:46 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"
#include <cctype>


namespace {
// Compare les caracteres sans distinguer minuscules et majuscules.
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

// Cherche dans la map des clients l'adresse du client vise et la renvoie.
Client* Server::_findClientByNickname(const std::string& nickname) {
    for (std::map<int, Client>::iterator it = this->_clients.begin();
         it != this->_clients.end(); ++it) {
        if (equalsIgnoreCase(it->second.getNickname(), nickname)) {
            return &it->second;
        }
    }
    return NULL;
}

// PRIVMSG <cible> :<message>. La cible est un channel si elle commence par '#',
// un pseudo sinon.
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

    // Vers un channel : diffusion a tous les membres sauf l'emetteur, qui ne
    // s'echoie pas a lui-meme (contrairement a JOIN ou PART).
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

        std::string out = ":" + client.getPrefix() + " PRIVMSG " + target
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
    std::string out = ":" + client.getPrefix() + " PRIVMSG " + target
                     + " :" + message.params[1] + "\r\n";
    this->sendToClient(targetClient->getFd(), out);
}
