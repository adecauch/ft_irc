#ifndef PARSER_HPP
# define PARSER_HPP

# include <string>
# include <vector>

/* ========================================================================== */
/*                             IRC MESSAGE PARSING                            */
/*                                                                            */
/*  Decoupe une ligne brute (deja privee de son \r\n par                      */
/*  Client::extractNextLine) en un mot de commande et une liste ordonnee de   */
/*  parametres. Purement mecanique : ne sait pas quelles commandes existent.  */
/* ========================================================================== */

// Resultat du parsing d'une ligne. Le parametre "trailing" (apres " :") est
// range dans le meme params que les autres : l'appelant veut juste une liste.
struct IrcMessage {
    std::string              command;
    std::vector<std::string> params;
};

namespace Parser {

    // Decoupe une ligne en commande (mise en majuscules) + parametres.
    // Ne leve jamais d'exception : une ligne absurde donne juste un resultat vide.
    IrcMessage parse(const std::string& rawLine);

}

#endif
