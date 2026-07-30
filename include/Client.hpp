/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Client.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 14:54:18 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:00:25 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>

class Client {

    private:
        int         _fd;
        std::string _nickname;
        std::string _username;
        std::string _hostname;
        std::string _realname;

        bool        _hasGivenPassword;
        bool        _isRegistered;

        // Octets recus pas encore traites / a envoyer pas encore partis.
        std::string _inputBuffer;
        std::string _outputBuffer;

        // true tant qu'on jette les octets d'une ligne trop longue.
        bool        _skippingLine;

    public:
        Client();
        Client(const Client& src);
        Client& operator=(const Client& rhs);
        ~Client();

        Client(int fd, const std::string& hostname);

        int                getFd() const;
        const std::string& getNickname() const;

        // Renvoie "nick!user@host", le prefixe des messages relayes aux autres.
        std::string        getPrefix() const;

        const std::string& getUsername() const;
        const std::string& getHostname() const;
        const std::string& getRealname() const;
        bool               getHasGivenPassword() const;
        bool               getIsRegistered() const;
        const std::string& getInputBuffer() const;
        const std::string& getOutputBuffer() const;

        void setNickname(const std::string& nickname);
        void setUsername(const std::string& username);
        void setRealname(const std::string& realname);
        void setHasGivenPassword(bool status);
        void setIsRegistered(bool status);

        void appendInputBuffer(const std::string& data);
        void appendOutputBuffer(const std::string& data);
        void clearInputBuffer(size_t pos);
        void clearOutputBuffer(size_t pos);

        // Sort la prochaine ligne complete du buffer d'entree et la retire.
        // Renvoie false s'il n'y a pas encore de fin de ligne (line intact).
        bool extractNextLine(std::string& line);

        // Reste-t-il des octets a envoyer ? Decide si POLLOUT doit rester arme.
        bool hasOutputData() const;

        // Abandonne la ligne en cours, trop longue, et arme la resynchronisation.
        void markLineOversized();

        bool isSkippingLine() const;

        // Jette les octets restants de la ligne trop longue jusqu'au saut de
        // ligne. Renvoie true quand la resynchronisation a eu lieu.
        bool resyncAfterOversizedLine();

};

#endif
