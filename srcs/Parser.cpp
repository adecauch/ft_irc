#include "../include/Parser.hpp"

#include <cctype>

/* ========================================================================== */
/*                             IRC MESSAGE PARSING                            */
/* ========================================================================== */

// [ROLE] : Parses one raw IRC line into a command + ordered parameter list.
IrcMessage Parser::parse(const std::string& rawLine) {
    IrcMessage message;
    std::string line = rawLine;

    std::string::size_type pos = line.find_first_not_of(' ');
    if (pos == std::string::npos) {
        return message; // blank/whitespace-only line
    }
    line.erase(0, pos);

    // Optional prefix: client-to-server messages should not carry one, but
    // skip it gracefully rather than mistaking it for the command word.
    if (line[0] == ':') {
        std::string::size_type spacePos = line.find(' ');
        if (spacePos == std::string::npos) {
            return message; // only a prefix, nothing else
        }
        line.erase(0, spacePos);
        pos = line.find_first_not_of(' ');
        if (pos == std::string::npos) {
            return message;
        }
        line.erase(0, pos);
    }

    // Command word, normalized to uppercase (IRC commands are case-insensitive).
    std::string::size_type spacePos = line.find(' ');
    message.command = line.substr(0, spacePos);
    for (std::string::size_type i = 0; i < message.command.size(); ++i) {
        message.command[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(message.command[i])));
    }
    if (spacePos == std::string::npos) {
        return message; // command only, no params
    }
    line.erase(0, spacePos);

    // Params: any number of space-separated "middle" tokens, optionally
    // followed by a single ':'-prefixed trailing token that runs to the end.
    while (true) {
        pos = line.find_first_not_of(' ');
        if (pos == std::string::npos) {
            break;
        }
        line.erase(0, pos);

        if (line[0] == ':') {
            message.params.push_back(line.substr(1));
            break;
        }

        spacePos = line.find(' ');
        message.params.push_back(line.substr(0, spacePos));
        if (spacePos == std::string::npos) {
            break;
        }
        line.erase(0, spacePos);
    }

    return message;
}
