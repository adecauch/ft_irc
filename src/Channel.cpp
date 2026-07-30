/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Channel.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/30 15:03:03 by adecauch          #+#    #+#             */
/*   Updated: 2026/07/30 15:07:38 by adecauch         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Channel.hpp"

// Simple valeur de remplissage (map operator[] avant affectation) : jamais
// laissee telle quelle dans le registre des channels.
Channel::Channel()
    : _name(""),
      _topic(""),
      _hasTopic(false),
      _inviteOnly(false),
      _topicRestricted(false),
      _hasKey(false),
      _key(""),
      _hasUserLimit(false),
      _userLimit(0) {}

Channel::Channel(const Channel& src) {
    *this = src;
}

Channel& Channel::operator=(const Channel& rhs) {
    if (this != &rhs) {
        this->_name = rhs._name;
        this->_topic = rhs._topic;
        this->_hasTopic = rhs._hasTopic;
        this->_members = rhs._members;
        this->_operators = rhs._operators;
        this->_invited = rhs._invited;
        this->_inviteOnly = rhs._inviteOnly;
        this->_topicRestricted = rhs._topicRestricted;
        this->_hasKey = rhs._hasKey;
        this->_key = rhs._key;
        this->_hasUserLimit = rhs._hasUserLimit;
        this->_userLimit = rhs._userLimit;
    }
    return *this;
}

// Rien a liberer a la main : uniquement des conteneurs STL.
Channel::~Channel() {}

// Modes et sujet partent tous a leur valeur par defaut : un channel recree
// apres etre devenu vide ne doit rien heriter de sa vie precedente.
Channel::Channel(const std::string& name)
    : _name(name),
      _topic(""),
      _hasTopic(false),
      _inviteOnly(false),
      _topicRestricted(false),
      _hasKey(false),
      _key(""),
      _hasUserLimit(false),
      _userLimit(0) {}

const std::string& Channel::getName() const {
    return this->_name;
}

bool Channel::isInviteOnly() const {
    return this->_inviteOnly;
}

void Channel::setInviteOnly(bool v) {
    this->_inviteOnly = v;
}

bool Channel::isTopicRestricted() const {
    return this->_topicRestricted;
}

void Channel::setTopicRestricted(bool v) {
    this->_topicRestricted = v;
}

bool Channel::hasKey() const {
    return this->_hasKey;
}

const std::string& Channel::getKey() const {
    return this->_key;
}

void Channel::setKey(const std::string& key) {
    this->_key = key;
    this->_hasKey = true;
}

void Channel::removeKey() {
    this->_key.clear();
    this->_hasKey = false;
}

bool Channel::hasUserLimit() const {
    return this->_hasUserLimit;
}

size_t Channel::getUserLimit() const {
    return this->_userLimit;
}

void Channel::setUserLimit(size_t limit) {
    this->_userLimit = limit;
    this->_hasUserLimit = true;
}

void Channel::removeUserLimit() {
    this->_userLimit = 0;
    this->_hasUserLimit = false;
}

void Channel::addMember(int fd) {
    this->_members.insert(fd);
}

void Channel::removeMember(int fd) {
    this->_members.erase(fd);
}

bool Channel::isMember(int fd) const {
    return this->_members.find(fd) != this->_members.end();
}

size_t Channel::memberCount() const {
    return this->_members.size();
}

const std::set<int>& Channel::getMembers() const {
    return this->_members;
}

bool Channel::isEmpty() const {
    return this->_members.empty();
}

void Channel::addOperator(int fd) {
    this->_operators.insert(fd);
}

void Channel::removeOperator(int fd) {
    this->_operators.erase(fd);
}

bool Channel::isOperator(int fd) const {
    return this->_operators.find(fd) != this->_operators.end();
}

void Channel::invite(int fd) {
    this->_invited.insert(fd);
}

bool Channel::isInvited(int fd) const {
    return this->_invited.find(fd) != this->_invited.end();
}

// Appelee apres l'entree effective du client : une invitation ne vaut que pour
// un seul JOIN. No-op si le client n'etait pas invite.
void Channel::removeInvited(int fd) {
    this->_invited.erase(fd);
}

const std::string& Channel::getTopic() const {
    return this->_topic;
}

bool Channel::hasTopic() const {
    return this->_hasTopic;
}

void Channel::setTopic(const std::string& topic) {
    this->_topic = topic;
    this->_hasTopic = true;
}

// "TOPIC #chan :" veut dire "effacer", pas "mettre une chaine vide" : d'ou la
// remise a false, qui fait repondre 331 a une relecture plutot qu'un 332 vide.
void Channel::clearTopic() {
    this->_topic.clear();
    this->_hasTopic = false;
}
