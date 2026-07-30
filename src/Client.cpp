/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Client.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 15:03:54 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:07:40 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Client.hpp"

// Le pseudo part a "*" : c'est ce que _tryCompleteRegistration teste pour savoir
// si un NICK est deja passe.
Client::Client()
    : _fd(-1),
      _nickname("*"),
      _username(""),
      _hostname("127.0.0.1"),
      _realname(""),
      _hasGivenPassword(false),
      _isRegistered(false),
      _inputBuffer(""),
      _outputBuffer(""),
      _skippingLine(false) {}

Client::Client(const Client& src) {
    *this = src;
}

Client& Client::operator=(const Client& rhs) {
    if (this != &rhs) {
        this->_fd = rhs._fd;
        this->_nickname = rhs._nickname;
        this->_username = rhs._username;
        this->_hostname = rhs._hostname;
        this->_realname = rhs._realname;
        this->_hasGivenPassword = rhs._hasGivenPassword;
        this->_isRegistered = rhs._isRegistered;
        this->_inputBuffer = rhs._inputBuffer;
        this->_outputBuffer = rhs._outputBuffer;
        this->_skippingLine = rhs._skippingLine;
    }
    return *this;
}

Client::~Client() {}

// Appele des que Server accepte une connexion. Le hostname vient de l'adresse
// reelle de la socket, jamais de ce que le client pretendra dans son USER.
Client::Client(int fd, const std::string& hostname)
    : _fd(fd),
      _nickname("*"),
      _username(""),
      _hostname(hostname),
      _realname(""),
      _hasGivenPassword(false),
      _isRegistered(false),
      _inputBuffer(""),
      _outputBuffer(""),
      _skippingLine(false) {}


int Client::getFd() const {
    return this->_fd;
}

const std::string& Client::getNickname() const {
    return this->_nickname;
}

// Prefixe complet "nick!user@host". Sans le !user@host, Irssi ne sait pas que
// l'expediteur est un utilisateur et n'ouvre pas de fenetre sur un prive.
std::string Client::getPrefix() const {
    const std::string& user = this->_username.empty() ? this->_nickname : this->_username;
    return this->_nickname + "!" + user + "@" + this->_hostname;
}

const std::string& Client::getUsername() const {
    return this->_username;
}

const std::string& Client::getHostname() const {
    return this->_hostname;
}

const std::string& Client::getRealname() const {
    return this->_realname;
}

bool Client::getHasGivenPassword() const {
    return this->_hasGivenPassword;
}

bool Client::getIsRegistered() const {
    return this->_isRegistered;
}

const std::string& Client::getInputBuffer() const {
    return this->_inputBuffer;
}

const std::string& Client::getOutputBuffer() const {
    return this->_outputBuffer;
}

void Client::setNickname(const std::string& nickname) {
    this->_nickname = nickname;
}

void Client::setUsername(const std::string& username) {
    this->_username = username;
}

void Client::setRealname(const std::string& realname) {
    this->_realname = realname;
}

void Client::setHasGivenPassword(bool status) {
    this->_hasGivenPassword = status;
}

void Client::setIsRegistered(bool status) {
    this->_isRegistered = status;
}

// Empile les octets bruts sortis de recv(), sans rien interpreter.
void Client::appendInputBuffer(const std::string& data) {
    this->_inputBuffer += data;
}

// Met une reponse en file ; le send() reel aura lieu dans la boucle poll().
void Client::appendOutputBuffer(const std::string& data) {
    this->_outputBuffer += data;
}

void Client::clearInputBuffer(size_t pos) {
    this->_inputBuffer.erase(0, pos);
}

// Retire les octets que send() a reellement acceptes, pas plus.
void Client::clearOutputBuffer(size_t pos) {
    this->_outputBuffer.erase(0, pos);
}



// Renvoie un bool et non la ligne, pour distinguer "pas encore de ligne
// complete" (false) de "ligne recue, peut-etre vide" (true).
bool Client::extractNextLine(std::string& line) {
    // 1. Chercher la fin de ligne IRC standard (\r\n)
    size_t pos = this->_inputBuffer.find("\r\n");
    size_t delimiterLen = 2;

    // Tolérance : si un client ou nc envoie juste \n au lieu de \r\n
    if (pos == std::string::npos) {
        pos = this->_inputBuffer.find("\n");
        delimiterLen = 1;
    }

    // Si aucun délimiteur n'est trouvé, la commande est incomplète dans le buffer
    if (pos == std::string::npos) {
        return false;
    }

    // 2. Extraire la chaîne avant le délimiteur
    line = this->_inputBuffer.substr(0, pos);

    // Supprimer le \r résiduel si jamais la ligne se terminait par \r\n et qu'on a matché sur \n
    if (!line.empty() && line[line.size() - 1] == '\r') {
        line.erase(line.size() - 1);
    }

    // 3. Supprimer la ligne extraite + son délimiteur du _inputBuffer
    this->_inputBuffer.erase(0, pos + delimiterLen);

    // 4. Ligne prête à être parsée/exécutée (peut être vide, et c'est valide)
    return true;
}

// Decide si POLLOUT doit rester arme sur ce client.
bool Client::hasOutputData() const {
    return !this->_outputBuffer.empty();
}

// Vider le buffer ne suffit pas : le reste de la ligne est encore en transit et
// se collerait devant la commande suivante, d'ou le drapeau.
void Client::markLineOversized() {
    this->_inputBuffer.clear();
    this->_skippingLine = true;
}

bool Client::isSkippingLine() const {
    return this->_skippingLine;
}

// On cherche '\n' seul et non "\r\n" : c'est le caractere commun aux deux
// conventions acceptees, donc le seul point de resynchronisation fiable.
bool Client::resyncAfterOversizedLine() {
    size_t pos = this->_inputBuffer.find('\n');

    if (pos == std::string::npos) {
        // Toujours au milieu de la ligne geante : on jette et on attend la suite.
        this->_inputBuffer.clear();
        return false;
    }

    // Fin de la ligne fautive trouvee : tout ce qui suit est de nouveau exploitable.
    this->_inputBuffer.erase(0, pos + 1);
    this->_skippingLine = false;
    return true;
}
