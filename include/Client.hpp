#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <iostream>

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
		// [ROLE] : Extracts and returns the next complete line from _inputBuffer.
    // [BEHAVIOR] : Finds '\r\n' (or '\n'), extracts the line without the delimiter,
    //              removes it from _inputBuffer, and returns it. Returns "" if incomplete.
    	std::string extractNextLine();

};

#endif