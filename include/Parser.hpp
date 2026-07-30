/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Parser.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 14:58:15 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:00:32 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef PARSER_HPP
# define PARSER_HPP

# include <string>
# include <vector>

//IRC MESSAGE PARSING

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
