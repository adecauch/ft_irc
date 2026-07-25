#include "../../include/Server.hpp"
#include "../../include/NumericReplies.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>

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

// Convertit un entier non signe en chaine (pas de std::to_string en C++98).
std::string sizeToString(std::size_t n) {
    std::ostringstream oss;
    oss << n;
    return oss.str();
}

// [ROLE] : Valide et convertit l'argument d'un mode +l : un entier strictement
//          positif uniquement.
// [RETURN] : false (out inchange) sur chaine vide, non entierement numerique,
//            valeur nulle, ou depassement (istringstream met alors failbit).
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

// [ROLE] : Construit la representation "+modes [args]" des modes actifs d'un
//          channel, pour RPL_CHANNELMODEIS (324).
// [WHY] : La cle (+k) et la limite (+l) sont ajoutees en argument seulement si
//         le mode correspondant est actif ; l'appelant ne montre ce 324 qu'aux
//         membres (verifie en amont), donc exposer la cle ici est acceptable.
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

    // Une invitation en attente est consommée dès l'entrée : elle ne vaut que
    // pour un seul JOIN (no-op si le client n'était pas invité, notamment le
    // créateur d'un channel neuf).
    channel->removeInvited(fd);

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

// [ROLE] : TOPIC <#channel> [:<topic>]
// [WHY] : Une seule commande, deux comportements. Avec le seul nom de channel
//         (params.size() == 1) elle *lit* le topic (331/332). Dès qu'un
//         argument topic est présent — même vide via "TOPIC #chan :" — elle
//         l'*écrit* : une valeur non vide pose le topic, une valeur vide
//         l'efface. C'est le parser qui permet cette distinction : "pas
//         d'argument" donne 1 param, "argument vide" en donne 2 (le 2e vide).
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

    // Aucun argument topic (params.size() == 1) → simple consultation.
    if (message.params.size() < 2) {
        if (channel->hasTopic()) {
            this->sendToClient(fd, RPL_TOPIC(client.getNickname(), chanName, channel->getTopic()));
        } else {
            this->sendToClient(fd, RPL_NOTOPIC(client.getNickname(), chanName));
        }
        return;
    }

    // Un argument topic est fourni → écriture. Réservée aux opérateurs
    // uniquement si le channel est en +t ; sinon n'importe quel membre peut
    // le changer (RFC 2812).
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

    // Diffusé à tous les membres, émetteur inclus (même logique que JOIN/PART),
    // pour que chaque client rafraîchisse le titre du channel.
    std::string topicMsg = ":" + client.getNickname() + " TOPIC " + chanName + " :" + newTopic + "\r\n";
    this->_broadcastToChannel(*channel, topicMsg, -1);
}

// [ROLE] : MODE <#channel> [<modestring> [<arg>...]]
// [WHY] : Sans modestring, c'est une consultation (324). Avec, c'est une
//         modification reservee aux operateurs (482 sinon). Le sujet ne demande
//         que les modes de channel : une cible qui n'est pas un channel (mode
//         utilisateur, ex. "MODE <nick> +i" envoye par certains clients au
//         demarrage) est ignoree en silence plutot que de generer une erreur
//         parasite qui polluerait l'affichage du client de reference.
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

// [ROLE] : Applique un modestring deja autorise (appelant a verifie op + membre).
// [WHY] : Le modestring se lit de gauche a droite. Un '+'/'-' fixe le sens
//         courant ; chaque lettre suivante s'applique dans ce sens. k/o/l
//         consomment un argument (params[2], params[3], ... dans l'ordre) ;
//         i/t n'en consomment pas. On accumule au passage la liste des
//         changements REELLEMENT appliques pour ne diffuser au channel que
//         ceux-la (un mode ignore/en erreur n'apparait pas dans l'echo).
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
                    // -k : la RFC passe la cle en argument ; on la consomme si
                    // elle est presente (valeur ignoree) pour ne pas decaler les
                    // arguments suivants, mais le retrait a lieu dans tous les cas.
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
                // Mode non supporte (ex. 'b' liste de bans envoyee par un client)
                // : ignore, jamais de crash. Hors perimetre du sujet.
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

    std::string modeMsg = ":" + client.getNickname() + " MODE " + channel.getName() + " " + appliedModes;
    for (std::size_t i = 0; i < appliedArgs.size(); ++i) {
        modeMsg += " " + appliedArgs[i];
    }
    modeMsg += "\r\n";
    this->_broadcastToChannel(channel, modeMsg, -1);
}

// [ROLE] : KICK <#channel> <user> [:<comment>]
// [WHY] : Ejection d'un membre par un operateur. Comme JOIN/PART, seul le
//         premier channel/pseudo avant une virgule est traite (pas de
//         multi-KICK, hors perimetre). Le message est diffuse a tous AVANT le
//         retrait effectif, pour que le client kicke recoive bien sa propre
//         ejection (c'est ce KICK qui ferme sa fenetre de channel).
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

    // Commentaire optionnel ; a defaut, le pseudo du kickeur (convention usuelle).
    std::string comment = (message.params.size() > 2 && !message.params[2].empty())
                          ? message.params[2] : client.getNickname();

    // Pseudo canonique du kicke (target->getNickname()), pas ce que le kickeur a
    // tape : c'est ce que son propre client doit reconnaitre pour se retirer.
    std::string kickMsg = ":" + client.getNickname() + " KICK " + chanName + " "
                          + target->getNickname() + " :" + comment + "\r\n";
    this->_broadcastToChannel(*channel, kickMsg, -1);

    channel->removeMember(target->getFd());
    channel->removeOperator(target->getFd());

    if (channel->isEmpty()) {
        // Meme logique que PART : clé exacte stockée, pas ce qui a été tapé.
        this->_channels.erase(channel->getName());
    }
}

// [ROLE] : INVITE <nick> <#channel>
// [WHY] : Attention, l'ordre des arguments est l'inverse de KICK (pseudo
//         d'abord, channel ensuite — RFC 2812). Remplit la liste _invited que
//         _handleJoin consulte deja sous +i, ce qui permet a l'invite de
//         contourner l'invite-only le temps d'un JOIN.
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
    std::string inviteMsg = ":" + client.getNickname() + " INVITE "
                            + target->getNickname() + " " + chanName + "\r\n";
    this->sendToClient(target->getFd(), inviteMsg);
}
