#ifndef PARSER_HPP
# define PARSER_HPP

# include <string>
# include <vector>

/* ========================================================================== */
/*                             IRC MESSAGE PARSING                            */
/*                                                                            */
/*  Turns one raw line (already stripped of its \r\n by                      */
/*  Client::extractNextLine) into a command word and an ordered list of      */
/*  parameters. Purely mechanical: it does not know which commands exist —   */
/*  that is the dispatcher's job, added on top of this.                      */
/* ========================================================================== */

// [ROLE] : Result of parsing one IRC line.
// [WHY] : The trailing parameter (after " :") is folded into the same params
//         list as the "middle" ones — callers just want an ordered list of
//         values, not the wire-level distinction between the two forms.
struct IrcMessage {
    std::string              command;
    std::vector<std::string> params;
};

namespace Parser {

    // [ROLE] : Parses one raw IRC line into a command + ordered parameter list.
    // [BEHAVIOR] : Skips an optional leading ':prefix' (clients should not send
    //              one, but the grammar allows it), uppercases the command word
    //              for case-insensitive matching, splits "middle" params on
    //              runs of spaces, and treats everything after a lone ':' as a
    //              single trailing parameter that may itself contain spaces.
    // [WHY] : Never throws and never crashes on malformed input — an empty or
    //         garbage line just yields an empty command and/or params, which
    //         is for the dispatcher to reject (421), not this function.
    IrcMessage parse(const std::string& rawLine);

}

#endif
