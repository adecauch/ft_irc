#ifndef COMMANDS_HPP
# define COMMANDS_HPP

/* ========================================================================== */
/*                      STRING DEFINITIONS (IRC COMMANDS)                     */
/* ========================================================================== */

// Authentification & Connexion
# define CMD_PASS     "PASS"
# define CMD_NICK     "NICK"
# define CMD_USER     "USER"
# define CMD_QUIT     "QUIT"

// Gestion des Canaux
# define CMD_JOIN     "JOIN"
# define CMD_PART     "PART"
# define CMD_TOPIC    "TOPIC"
# define CMD_KICK     "KICK"
# define CMD_INVITE   "INVITE"
# define CMD_MODE     "MODE"

// Communication & Divers
# define CMD_PRIVMSG  "PRIVMSG"
# define CMD_NOTICE   "NOTICE"
# define CMD_PING     "PING"
# define CMD_PONG     "PONG"
# define CMD_CAP      "CAP"
# define CMD_WHO      "WHO"
# define CMD_WHOIS    "WHOIS"

/* ========================================================================== */
/*                           ENUM FOR LOGIC & EXEC                            */
/* ========================================================================== */

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

#endif