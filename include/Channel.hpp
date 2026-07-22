# ifndef CHANNEL_HPP
# define CHANNEL_HPP

# include <string>
#include <set>

class Channel {

    private:
        std::string      _name;          // "#foo", tel que fourni au premier JOIN
        std::string      _topic;
        bool              _hasTopic;

        std::set<int>     _members;      // fds
        std::set<int>     _operators;     // sous-ensemble de _members
        std::set<int>     _invited;       // fds invités (mode +i)

        bool              _inviteOnly;    // +i
        bool              _topicRestricted; // +t (TOPIC réservé aux ops)
        bool              _hasKey;        // +k
        std::string       _key;
        bool              _hasUserLimit;  // +l
        size_t            _userLimit;

    public:
        // -----------------------------------------------------------------------------
        //                         Orthodox Canonical Form
        // -----------------------------------------------------------------------------
        Channel();
        Channel(const Channel& src);
        Channel(const std::string& name);
        Channel& operator=(const Channel& rhs);
        ~Channel();

        // -----------------------------------------------------------------------------
        //                         Getters & Setters
        // -----------------------------------------------------------------------------
        const std::string& getName() const;
        bool        isInviteOnly() const;
        void        setInviteOnly(bool v);
        bool        isTopicRestricted() const;
        void        setTopicRestricted(bool v);
        bool        hasKey() const;
        const std::string& getKey() const;
        void        setKey(const std::string& key);
        void        removeKey();
        bool        hasUserLimit() const;
        size_t      getUserLimit() const;
        void        setUserLimit(size_t limit);
        void        removeUserLimit();
        // -----------------------------------------------------------------------------
        //                         Member Functions
        // -----------------------------------------------------------------------------
        void        addMember(int fd);
        void        removeMember(int fd);
        bool        isMember(int fd) const;
        size_t      memberCount() const;
        const std::set<int>& getMembers() const;
        bool        isEmpty() const;
        // -----------------------------------------------------------------------------
        //                         Operateurs
        // -----------------------------------------------------------------------------
        void        addOperator(int fd);
        void        removeOperator(int fd);
        bool        isOperator(int fd) const;

        // -----------------------------------------------------------------------------
        //                         Divers
        // -----------------------------------------------------------------------------
        // Invités (+i)
        void        invite(int fd);
        bool        isInvited(int fd) const;

        // Topic
        const std::string& getTopic() const;
        bool        hasTopic() const;
        void        setTopic(const std::string& topic);
};

#endif