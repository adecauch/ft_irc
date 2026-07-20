#ifndef NUMERICREPLIES_HPP
# define NUMERICREPLIES_HPP

/* ========================================================================== */
/*                     IRC NUMERIC REPLIES (RFC 1459)                         */
/*                                                                            */
/*  This file contains all formatted responses that the server SENDS BACK     */
/*  to connected clients (Irssi/HexChat/nc). IRC uses 3-digit numeric codes.  */
/* ========================================================================== */

// -----------------------------------------------------------------------------
//  1. SUCCESS & HANDSHAKE RESPONSES (Codes 001 to 399)
// -----------------------------------------------------------------------------

// [ROLE] : Official welcome message.
// [WHEN TO USE] : Sent immediately after validating PASS + NICK + USER.
// [WHY] : Without code 001, IRC clients (e.g., Irssi) assume the connection failed.
# define RPL_WELCOME(nick)              ("001 " + nick + " :Welcome to the IRC Network " + nick + "\r\n")

// [ROLE] : Server version and host information.
// [WHEN TO USE] : Sent right after RPL_WELCOME (001) to complete the handshake.
# define RPL_YOURHOST(nick)             ("002 " + nick + " :Your host is ft_irc, running version 1.0\r\n")
# define RPL_CREATED(nick, date)        ("003 " + nick + " :This server was created " + date + "\r\n")
# define RPL_MYINFO(nick)               ("004 " + nick + " ft_irc 1.0 i t k o l\r\n")

// [ROLE] : Displays active modes for a user or a channel.
// [WHEN TO USE] : Sent in response to the 'MODE' command.
# define RPL_UMODEIS(nick, modes)       ("221 " + nick + " " + modes + "\r\n")
# define RPL_CHANNELMODEIS(nick, chan, modes) ("324 " + nick + " " + chan + " " + modes + "\r\n")

// [ROLE] : Channel topic management.
// [WHEN TO USE] : When a client joins a channel ('JOIN #chan') or runs '/topic'.
# define RPL_NOTOPIC(nick, chan)        ("331 " + nick + " " + chan + " :No topic is set\r\n")
# define RPL_TOPIC(nick, chan, topic)   ("332 " + nick + " " + chan + " :" + topic + "\r\n")

// [ROLE] : Confirms to the inviter that the invite was sent.
// [WHEN TO USE] : After a successful 'INVITE nick #chan' command.
# define RPL_INVITING(nick, target, chan) ("341 " + nick + " " + target + " " + chan + "\r\n")

// [ROLE] : Transmits the list of channel members (+ signals end of list).
// [WHEN TO USE] : Sent automatically upon joining a channel ('JOIN #chan').
// [WHY] : Populates the channel member list on the client's UI side panel.
# define RPL_NAMREPLY(nick, chan, users) ("353 " + nick + " = " + chan + " :" + users + "\r\n")
# define RPL_ENDOFNAMES(nick, chan)     ("366 " + nick + " " + chan + " :End of /NAMES list\r\n")

// -----------------------------------------------------------------------------
//  2. ERROR & REJECTION RESPONSES (Codes 400 to 599)
// -----------------------------------------------------------------------------

// [ROLE] : Target entity not found errors.
// [WHEN TO USE] : When the target of PRIVMSG/INVITE/KICK does not exist.
# define ERR_NOSUCHNICK(nick, target)   ("401 " + nick + " " + target + " :No such nick/channel\r\n")
# define ERR_NOSUCHCHANNEL(nick, chan)  ("403 " + nick + " " + chan + " :No such channel\r\n")
# define ERR_CANNOTSENDTOCHAN(nick, chan) ("404 " + nick + " " + chan + " :Cannot send to channel\r\n")

// [ROLE] : Invalid message syntax errors.
// [WHEN TO USE] : 'PRIVMSG' sent without a target recipient or message text.
# define ERR_NORECIPIENT(nick)          ("411 " + nick + " :No recipient given\r\n")
# define ERR_NOTEXTTOSEND(nick)         ("412 " + nick + " :No text to send\r\n")

// [ROLE] : Unrecognized command error.
// [WHEN TO USE] : When a user types an unhandled command string (e.g., 'FOOBAR').
# define ERR_UNKNOWNCOMMAND(nick, cmd)  ("421 " + nick + " " + cmd + " :Unknown command\r\n")

// [ROLE] : Nickname validation errors.
// [WHEN TO USE] : 431 (Missing nick), 432 (Invalid chars), 433 (Nick already taken).
# define ERR_NONICKNAMEGIVEN(nick)      ("431 " + nick + " :No nickname given\r\n")
# define ERR_ERRONEUSNICKNAME(nick, bad) ("432 " + nick + " " + bad + " :Erroneous nickname\r\n")
# define ERR_NICKNAMEINUSE(nick, bad)   ("433 " + nick + " " + bad + " :Nickname is already in use\r\n")

// [ROLE] : Channel membership state errors.
// [WHEN TO USE] : 441 (Kicking an absent user), 442 (Acting without being in channel), 443 (Inviting an existing member).
# define ERR_USERNOTINCHANNEL(nick, target, chan) ("441 " + nick + " " + target + " " + chan + " :They aren't on that channel\r\n")
# define ERR_NOTONCHANNEL(nick, chan)   ("442 " + nick + " " + chan + " :You're not on that channel\r\n")
# define ERR_USERONCHANNEL(nick, target, chan) ("443 " + nick + " " + target + " " + chan + " :is already on channel\r\n")

// [ROLE] : Authentication & Access Security.
// [WHEN TO USE] : 
// - 451: Client attempts actions BEFORE completing registration.
// - 461: Missing parameters for a given command (e.g., 'JOIN' alone).
// - 462: Client attempts sending USER command again when already registered.
// - 464: Password sent via PASS command is incorrect.
# define ERR_NOTREGISTERED(nick)        ("451 " + nick + " :You have not registered\r\n")
# define ERR_NEEDMOREPARAMS(nick, cmd)  ("461 " + nick + " " + cmd + " :Not enough parameters\r\n")
# define ERR_ALREADYREGISTRED(nick)     ("462 " + nick + " :Unauthorized command expected raw registration\r\n")
# define ERR_PASSWDMISMATCH(nick)       ("464 " + nick + " :Password incorrect\r\n")

// [ROLE] : Channel Access Restriction Errors (Channel Modes).
// [WHEN TO USE] : 
// - 471: Channel capacity reached (Mode +l).
// - 473: Channel is invite-only (Mode +i).
// - 475: Wrong channel key/password (Mode +k).
// - 482: Command restricted to channel operators (Mode +o / KICK / TOPIC).
# define ERR_CHANNELISFULL(nick, chan)  ("471 " + nick + " " + chan + " :Cannot join channel (+l)\r\n")
# define ERR_INVITEONLYCHAN(nick, chan) ("473 " + nick + " " + chan + " :Cannot join channel (+i)\r\n")
# define ERR_BADCHANNELKEY(nick, chan)  ("475 " + nick + " " + chan + " :Cannot join channel (+k)\r\n")
# define ERR_CHANOPRIVSNEEDED(nick, chan) ("482 " + nick + " " + chan + " :You're not channel operator\r\n")

#endif