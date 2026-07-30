/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Parser.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 15:05:39 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:07:49 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Parser.hpp"

#include <cctype>

// Grammaire visee : [:prefixe] COMMANDE p1 p2 :dernier param avec des espaces.
// Un ':' en tete d'un parametre en fait le dernier, espaces compris.
IrcMessage Parser::parse(const std::string& rawLine) {
    IrcMessage message;
    std::string line = rawLine;

    std::string::size_type pos = line.find_first_not_of(' ');
    if (pos == std::string::npos) {
        return message; // blank/whitespace-only line
    }
    line.erase(0, pos);

    // Un client ne devrait pas envoyer de prefixe, mais la grammaire l'autorise :
    // on le saute plutot que de le prendre pour la commande.
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

    // Mise en majuscules : les commandes IRC sont insensibles a la casse.
    std::string::size_type spacePos = line.find(' ');
    message.command = line.substr(0, spacePos);
    for (std::string::size_type i = 0; i < message.command.size(); ++i) {
        message.command[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(message.command[i])));
    }
    if (spacePos == std::string::npos) {
        return message; // command only, no params
    }
    line.erase(0, spacePos);

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
