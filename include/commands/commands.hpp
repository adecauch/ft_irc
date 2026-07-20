#ifndef COMMANDS_HPP
# define COMMANDS_HPP

/* ========================================================================== */
/*                      IRC COMMAND IDENTIFIERS                               */
/*                                                                            */
/*  This file centralizes all string definitions and enum values for IRC      */
/*  commands used across the server's parser and command execution modules.   */
/* ========================================================================== */

// -----------------------------------------------------------------------------
//  1. STRING DEFINITIONS (IRC COMMAND STRINGS)
// -----------------------------------------------------------------------------

// [ROLE] : Authentication & Connection Handshake
// [WHEN TO USE] : Used by the parser to identify client registration commands.
# define CMD_PASS     "PASS"     // Sets or validates the server password
# define CMD_NICK     "NICK"     // Sets or changes the client's nickname
# define CMD_USER     "USER"     // Specifies username, hostname, and real name
# define CMD_QUIT     "QUIT"     // Terminates the client session cleanly

// [ROLE] : Channel Operations & Administration
// [WHEN TO USE] : Used when clients attempt to join, leave, or modify channels.
# define CMD_JOIN     "JOIN"     // Joins or creates a channel
# define CMD_PART     "PART"     // Leaves one or more channels
# define CMD_TOPIC    "TOPIC"    // Views or changes a channel's topic
# define CMD_KICK     "KICK"     // Expels a user from a channel (Operator only)
# define CMD_INVITE   "INVITE"   // Invites a user to a restricted channel
# define CMD_MODE     "MODE"     // Alters channel/user modes (+i, +t, +k, +o, +l)

// [ROLE] : Communication & Network Utilities
// [WHEN TO USE] : Handled during messaging and heartbeats.
# define CMD_PRIVMSG  "PRIVMSG"  // Sends a private message to a user or channel
# define CMD_NOTICE   "NOTICE"   // Sends a notification (must not generate automated replies)
# define CMD_PING     "PING"     // Tests connection responsiveness from peer
# define CMD_PONG     "PONG"     // Responds to incoming PING requests
# define CMD_CAP      "CAP"      // Capability negotiation sent by modern clients (Irssi)
# define CMD_WHO      "WHO"      // Queries information about matching users
# define CMD_WHOIS    "WHOIS"    // Queries detailed user profile information

// -----------------------------------------------------------------------------
//  2. ENUM FOR LOGIC & EXECUTION ROUTING
// -----------------------------------------------------------------------------

// [ROLE] : Fast command identification using numerical representations.
// [WHY] : Comparing enums in switch statements is significantly faster than
//         performing repeated string comparisons during execution.
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