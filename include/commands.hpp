/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   commands.hpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 14:56:32 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:00:27 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef COMMANDS_HPP
# define COMMANDS_HPP

# include <string>

//IRC COMMAND IDENTIFIERS


// Poignee de main et fin de session.
# define CMD_PASS     "PASS"     // Sets or validates the server password
# define CMD_NICK     "NICK"     // Sets or changes the client's nickname
# define CMD_USER     "USER"     // Specifies username, hostname, and real name
# define CMD_QUIT     "QUIT"     // Terminates the client session cleanly

// Operations sur les channels.
# define CMD_JOIN     "JOIN"     // Joins or creates a channel
# define CMD_PART     "PART"     // Leaves one or more channels
# define CMD_TOPIC    "TOPIC"    // Views or changes a channel's topic
# define CMD_KICK     "KICK"     // Expels a user from a channel (Operator only)
# define CMD_INVITE   "INVITE"   // Invites a user to a restricted channel
# define CMD_MODE     "MODE"     // Alters channel/user modes (+i, +t, +k, +o, +l)

// Messages et utilitaires reseau.
# define CMD_PRIVMSG  "PRIVMSG"  // Sends a private message to a user or channel
# define CMD_NOTICE   "NOTICE"   // Sends a notification (must not generate automated replies)
# define CMD_PING     "PING"     // Tests connection responsiveness from peer
# define CMD_PONG     "PONG"     // Responds to incoming PING requests
# define CMD_CAP      "CAP"      // Capability negotiation sent by modern clients (Irssi)
# define CMD_WHO      "WHO"      // Queries information about matching users
# define CMD_WHOIS    "WHOIS"    // Queries detailed user profile information


// Une valeur par commande, pour pouvoir faire un switch au dispatch plutot que
// d'eparpiller des comparaisons de chaines dans tout le code.
enum e_command
{
    CMD_UNKNOWN = 0,

    // Auth
    CMD_ENUM_PASS,
    CMD_ENUM_NICK,
    CMD_ENUM_USER,
    CMD_ENUM_QUIT,

    // Channels
    CMD_ENUM_JOIN,
    CMD_ENUM_PART,
    CMD_ENUM_TOPIC,
    CMD_ENUM_KICK,
    CMD_ENUM_INVITE,
    CMD_ENUM_MODE,

    // Messages & Utils
    CMD_ENUM_PRIVMSG,
    CMD_ENUM_NOTICE,
    CMD_ENUM_PING,
    CMD_ENUM_PONG,
    CMD_ENUM_CAP,
    CMD_ENUM_WHO,
    CMD_ENUM_WHOIS
};


// Traduit un mot de commande deja en majuscules en sa valeur d'enumeration.
// Renvoie CMD_UNKNOWN si le mot ne correspond a aucune commande connue.
e_command resolveCommand(const std::string& command);

#endif
