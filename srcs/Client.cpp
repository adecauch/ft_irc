#include "../include/Client.hpp"

/* ========================================================================== */
/*                         ORTHODOX CANONICAL FORM                            */
/* ========================================================================== */

// [ROLE] : Default constructor.
// [WHY] : Required by Orthodox Canonical Form. Initializes members to empty/safe defaults.
Client::Client() 
    : _fd(-1), 
      _nickname("*"), 
      _username(""), 
      _hostname("127.0.0.1"), 
      _realname(""), 
      _hasGivenPassword(false), 
      _isRegistered(false), 
      _inputBuffer(""), 
      _outputBuffer("") {}

// [ROLE] : Copy constructor.
// [WHY] : Performs deep copy of all client attributes.
Client::Client(const Client& src) {
    *this = src;
}

// [ROLE] : Assignment operator.
// [WHY] : Allows safe assignment between Client instances.
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
    }
    return *this;
}

// [ROLE] : Destructor.
// [WHY] : Cleans up instance state upon destruction.
Client::~Client() {}


/* ========================================================================== */
/*                         PARAMETERIZED CONSTRUCTOR                          */
/* ========================================================================== */

// [ROLE] : Custom constructor bound to an active socket and host IP.
// [WHEN TO USE] : Called immediately when Server accepts a new incoming connection.
Client::Client(int fd, const std::string& hostname) 
    : _fd(fd), 
      _nickname("*"), 
      _username(""), 
      _hostname(hostname), 
      _realname(""), 
      _hasGivenPassword(false), 
      _isRegistered(false), 
      _inputBuffer(""), 
      _outputBuffer("") {}


/* ========================================================================== */
/*                             GETTERS & SETTERS                              */
/* ========================================================================== */

int Client::getFd() const {
    return this->_fd;
}

const std::string& Client::getNickname() const {
    return this->_nickname;
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


/* ========================================================================== */
/*                             MEMBER FUNCTIONS                               */
/* ========================================================================== */

// [ROLE] : Appends raw data chunk read from socket recv() to client's input queue.
void Client::appendInputBuffer(const std::string& data) {
    this->_inputBuffer += data;
}

// [ROLE] : Appends outgoing formatted responses to client's send queue.
void Client::appendOutputBuffer(const std::string& data) {
    this->_outputBuffer += data;
}

// [ROLE] : Truncates processed bytes from input buffer up to pos index.
// [WHEN TO USE] : Called by the Parser after extracting a complete command line (\r\n).
void Client::clearInputBuffer(size_t pos) {
    this->_inputBuffer.erase(0, pos);
}

// [ROLE] : Truncates sent bytes from output buffer up to pos index.
// [WHEN TO USE] : Called after successful send()/write() system call.
void Client::clearOutputBuffer(size_t pos) {
    this->_outputBuffer.erase(0, pos);
}



std::string Client::extractNextLine() {
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
        return "";
    }

    // 2. Extraire la chaîne avant le délimiteur
    std::string line = this->_inputBuffer.substr(0, pos);

    // Supprimer le \r résiduel si jamais la ligne se terminait par \r\n et qu'on a matché sur \n
    if (!line.empty() && line[line.size() - 1] == '\r') {
        line.erase(line.size() - 1);
    }

    // 3. Supprimer la ligne extraite + son délimiteur du _inputBuffer
    this->_inputBuffer.erase(0, pos + delimiterLen);

    // 4. Renvoyer la commande prête à être parsée/exécutée
    return line;
}