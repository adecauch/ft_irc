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
        
        std::string _inputBuffer;
        std::string _outputBuffer;

    public:
        // -----------------------------------------------------------------------------
        //                         Orthodox Canonical Form
        // -----------------------------------------------------------------------------
        Client();
        Client(const Client& src);
        Client& operator=(const Client& rhs);
        ~Client();

        // -----------------------------------------------------------------------------
        //                         Parameterized Constructor
        // -----------------------------------------------------------------------------
        Client(int fd, const std::string& hostname);

        // -----------------------------------------------------------------------------
        //                         Getters & Setters
        // -----------------------------------------------------------------------------
        int                getFd() const;
        const std::string& getNickname() const;
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

        // -----------------------------------------------------------------------------
        //                         Member Functions
        // -----------------------------------------------------------------------------
        void appendInputBuffer(const std::string& data);
        void appendOutputBuffer(const std::string& data);
        void clearInputBuffer(size_t pos);
        void clearOutputBuffer(size_t pos);

        // [ROLE] : Extracts the next complete line from _inputBuffer, if any.
        // [BEHAVIOR] : Finds '\r\n' (or '\n'), removes it (and the delimiter) from
        //              _inputBuffer, and writes it to line. Returns false when the
        //              buffer holds no complete line yet (line is left untouched).
        // [WHY] : A bool return keeps "nothing to extract yet" distinguishable from
        //         "the client sent a genuinely empty line" — returning "" for both
        //         would make the caller's loop stop on an empty line and leave
        //         whatever comes right after it stuck in the buffer until more
        //         data happens to arrive.
        bool extractNextLine(std::string& line);

        // [ROLE] : Tells the Server whether this client still has bytes to send.
        // [WHEN TO USE] : Used to decide whether POLLOUT must stay active for this fd.
        bool hasOutputData() const;

};

#endif