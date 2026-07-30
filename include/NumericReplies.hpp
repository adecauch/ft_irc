#ifndef NUMERICREPLIES_HPP
# define NUMERICREPLIES_HPP

/* ========================================================================== */
/*                     IRC NUMERIC REPLIES (RFC 1459)                         */
/*                                                                            */
/*  Reponses codees que le serveur RENVOIE aux clients. Chaque macro construit */
/*  la ligne complete, \r\n inclus : l'appelant n'a jamais a y penser.         */
/* ========================================================================== */

// -----------------------------------------------------------------------------
//  1. SUCCESS & HANDSHAKE RESPONSES (Codes 001 to 399)
// -----------------------------------------------------------------------------

// Salve envoyee une seule fois, des que PASS + NICK + USER ont tous abouti.
// Sans le 001, un client considere que la connexion a echoue.
# define RPL_WELCOME(nick)              ("001 " + nick + " :Welcome to the IRC Network " + nick + "\r\n")
# define RPL_YOURHOST(nick)             ("002 " + nick + " :Your host is ft_irc, running version 1.0\r\n")
# define RPL_CREATED(nick, date)        ("003 " + nick + " :This server was created " + date + "\r\n")
# define RPL_MYINFO(nick)               ("004 " + nick + " ft_irc 1.0 i t k o l\r\n")

// Modes actifs, en reponse a un MODE sans modestring.
# define RPL_UMODEIS(nick, modes)       ("221 " + nick + " " + modes + "\r\n")
# define RPL_CHANNELMODEIS(nick, chan, modes) ("324 " + nick + " " + chan + " " + modes + "\r\n")

// Sujet du channel, a l'entree ou sur un TOPIC en lecture.
# define RPL_NOTOPIC(nick, chan)        ("331 " + nick + " " + chan + " :No topic is set\r\n")
# define RPL_TOPIC(nick, chan, topic)   ("332 " + nick + " " + chan + " :" + topic + "\r\n")

// Confirme a l'inviteur que son INVITE est parti.
# define RPL_INVITING(nick, target, chan) ("341 " + nick + " " + target + " " + chan + "\r\n")

// Liste des membres, envoyee automatiquement a l'entree dans un channel.
// Attend une liste deja concatenee, "@op1 membre2 ...".
# define RPL_NAMREPLY(nick, chan, users) ("353 " + nick + " = " + chan + " :" + users + "\r\n")
# define RPL_ENDOFNAMES(nick, chan)     ("366 " + nick + " " + chan + " :End of /NAMES list\r\n")

// -----------------------------------------------------------------------------
//  2. ERROR & REJECTION RESPONSES (Codes 400 to 599)
// -----------------------------------------------------------------------------

// Cible introuvable ou nom de channel invalide.
# define ERR_NOSUCHNICK(nick, target)   ("401 " + nick + " " + target + " :No such nick/channel\r\n")
# define ERR_NOSUCHCHANNEL(nick, chan)  ("403 " + nick + " " + chan + " :No such channel\r\n")
# define ERR_BADCHANMASK(nick, chan)    ("476 " + nick + " " + chan + " :Bad Channel Mask\r\n")
# define ERR_CANNOTSENDTOCHAN(nick, chan) ("404 " + nick + " " + chan + " :Cannot send to channel\r\n")

// PRIVMSG sans destinataire (411) ou sans texte (412).
# define ERR_NORECIPIENT(nick)          ("411 " + nick + " :No recipient given\r\n")
# define ERR_NOTEXTTOSEND(nick)         ("412 " + nick + " :No text to send\r\n")

// Commande non reconnue.
# define ERR_UNKNOWNCOMMAND(nick, cmd)  ("421 " + nick + " " + cmd + " :Unknown command\r\n")

// Ligne accumulee sans jamais de \r\n : le fragment en attente est jete.
# define ERR_INPUTTOOLONG(nick)         ("417 " + nick + " :Input line was too long\r\n")

// Pseudo : absent (431), format invalide (432), deja pris (433).
# define ERR_NONICKNAMEGIVEN(nick)      ("431 " + nick + " :No nickname given\r\n")
# define ERR_ERRONEUSNICKNAME(nick, bad) ("432 " + nick + " " + bad + " :Erroneous nickname\r\n")
# define ERR_NICKNAMEINUSE(nick, bad)   ("433 " + nick + " " + bad + " :Nickname is already in use\r\n")

// Appartenance au channel : cible absente (441), soi-meme absent (442),
// cible deja membre (443).
# define ERR_USERNOTINCHANNEL(nick, target, chan) ("441 " + nick + " " + target + " " + chan + " :They aren't on that channel\r\n")
# define ERR_NOTONCHANNEL(nick, chan)   ("442 " + nick + " " + chan + " :You're not on that channel\r\n")
# define ERR_USERONCHANNEL(nick, target, chan) ("443 " + nick + " " + target + " " + chan + " :is already on channel\r\n")

// Enregistrement : pas encore fait (451), parametres manquants (461), deja
// fait (462), mot de passe faux (464).
# define ERR_NOTREGISTERED(nick)        ("451 " + nick + " :You have not registered\r\n")
# define ERR_NEEDMOREPARAMS(nick, cmd)  ("461 " + nick + " " + cmd + " :Not enough parameters\r\n")
# define ERR_ALREADYREGISTRED(nick)     ("462 " + nick + " :Unauthorized command expected raw registration\r\n")
# define ERR_PASSWDMISMATCH(nick)       ("464 " + nick + " :Password incorrect\r\n")

// Acces au channel refuse : plein (471), sur invitation (473), mauvaise cle
// (475), reserve aux operateurs (482).
# define ERR_CHANNELISFULL(nick, chan)  ("471 " + nick + " " + chan + " :Cannot join channel (+l)\r\n")
# define ERR_INVITEONLYCHAN(nick, chan) ("473 " + nick + " " + chan + " :Cannot join channel (+i)\r\n")
# define ERR_BADCHANNELKEY(nick, chan)  ("475 " + nick + " " + chan + " :Cannot join channel (+k)\r\n")
# define ERR_CHANOPRIVSNEEDED(nick, chan) ("482 " + nick + " " + chan + " :You're not channel operator\r\n")

#endif
