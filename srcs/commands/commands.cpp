#include "../../include/commands/commands.hpp"

#include <cstddef>

/* ========================================================================== */
/*                      STRING -> ENUM RESOLUTION                             */
/* ========================================================================== */

namespace {

struct CommandEntry {
    const char* name;
    e_command   type;
};

// Toutes les commandes reconnues. NOTICE, WHO et WHOIS sont acceptees mais sans
// handler : le sujet ne les demande pas, et les avaler evite une erreur parasite.
const CommandEntry kCommandTable[] = {
    { CMD_PASS,    CMD_ENUM_PASS },
    { CMD_NICK,    CMD_ENUM_NICK },
    { CMD_USER,    CMD_ENUM_USER },
    { CMD_QUIT,    CMD_ENUM_QUIT },
    { CMD_JOIN,    CMD_ENUM_JOIN },
    { CMD_PART,    CMD_ENUM_PART },
    { CMD_TOPIC,   CMD_ENUM_TOPIC },
    { CMD_KICK,    CMD_ENUM_KICK },
    { CMD_INVITE,  CMD_ENUM_INVITE },
    { CMD_MODE,    CMD_ENUM_MODE },
    { CMD_PRIVMSG, CMD_ENUM_PRIVMSG },
    { CMD_NOTICE,  CMD_ENUM_NOTICE },
    { CMD_PING,    CMD_ENUM_PING },
    { CMD_PONG,    CMD_ENUM_PONG },
    { CMD_CAP,     CMD_ENUM_CAP },
    { CMD_WHO,     CMD_ENUM_WHO },
    { CMD_WHOIS,   CMD_ENUM_WHOIS }
};

const std::size_t kCommandTableSize = sizeof(kCommandTable) / sizeof(kCommandTable[0]);

}

// Recherche lineaire : sur 17 entrees, le cout est negligeable.
// Attend un mot deja en majuscules, ce que Parser::parse() garantit.
e_command resolveCommand(const std::string& command) {
    for (std::size_t i = 0; i < kCommandTableSize; ++i) {
        if (command == kCommandTable[i].name) {
            return kCommandTable[i].type;
        }
    }
    return CMD_UNKNOWN;
}
