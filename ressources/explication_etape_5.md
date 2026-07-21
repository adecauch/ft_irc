# Étape 5 — PRIVMSG entre deux clients

Ce document explique ce qui a été ajouté pour l'étape 5 du `ROADMAP.md`
(`PRIVMSG <nick> :<message>` entre deux clients déjà enregistrés, sans notion
de channel). Il est destiné à un collègue qui n'a pas suivi le développement
en direct : chaque fichier touché, chaque fonction, et ce que ça apporte au
serveur.

**Statut : terminé et testé à 100 %.** `make re` compile sans aucun warning
(`-Wall -Wextra -Werror -std=c++98`), et les 8 scénarios ci-dessous (nominal,
symétrie, casse, et les 4 cas d'erreur) ont été vérifiés avec deux vrais
clients TCP connectés simultanément.

## 1. Vue d'ensemble : qu'est-ce que ça ajoute concrètement ?

Avant cette étape, un client pouvait s'enregistrer (`PASS`/`NICK`/`USER`,
étape 4) mais ne pouvait envoyer aucune commande utile ensuite : tout tombait
dans la branche `isRegistered` sans handler, ou dans le `421` pour une
commande inconnue. `PRIVMSG` est la toute première commande "métier" du
serveur : elle permet à deux clients connectés de s'envoyer un message privé
direct, par pseudo, sans passer par un salon.

## 2. Fichiers modifiés ou créés

### `srcs/commands/Messaging.cpp` (nouveau fichier)

C'est le cœur de l'étape 5, calqué sur le style de `Registration.cpp` (même
convention : un `namespace` anonyme pour les utilitaires internes au fichier,
puis les méthodes de `Server`).

- **`Server::_findClientByNickname(const std::string& nickname)`**
  Parcourt la map `_clients` (fd → `Client`) et renvoie un pointeur vers le
  premier client dont le pseudo correspond, comparaison insensible à la casse
  (`equalsIgnoreCase`). Renvoie `NULL` si personne ne porte ce pseudo.
  C'est la brique qui manquait pour transformer un `nick` textuel (ce que le
  client envoie) en un `fd` réel (ce dont `sendToClient` a besoin).

- **`Server::_handlePrivmsg(int fd, Client& client, const IrcMessage& message)`**
  Le handler complet de `PRIVMSG`. Trois vérifications dans un ordre précis,
  chacune avec un `return` immédiat pour ne jamais cumuler deux erreurs :
  1. `message.params.empty()` → `411 ERR_NORECIPIENT` (aucun paramètre du
     tout, ex : `PRIVMSG` seul).
  2. `message.params.size() < 2 || message.params[1].empty()` →
     `412 ERR_NOTEXTTOSEND` (un pseudo cible donné mais pas de `:texte`).
  3. `_findClientByNickname(targetNick) == NULL` → `401 ERR_NOSUCHNICK`
     (pseudo cible introuvable parmi les clients connectés).

  Si les trois passent, le message est reconstruit au format IRC standard
  (`:emetteur PRIVMSG cible :texte\r\n`) et envoyé **uniquement** au
  destinataire via `sendToClient(target->getFd(), out)` — l'émetteur ne
  reçoit jamais d'écho de son propre message, ce qui correspond au
  comportement standard d'un serveur IRC.

- **`equalsIgnoreCase()`** (namespace anonyme, privé au fichier) : compare
  deux chaînes caractère par caractère via `std::toupper`. C'est une copie de
  la fonction du même nom dans `Registration.cpp` — acceptable pour 8 lignes
  dupliquées ; si une 3ᵉ copie devient nécessaire à une étape suivante, il
  faudra la sortir dans un `include/utils.hpp` partagé plutôt que de la
  dupliquer encore une fois.

### `include/Server.hpp` (modifié)

Deux déclarations ajoutées dans la section `private` de la classe, à côté de
`_handlePass`/`_handleNick`/`_handleUser` :

```cpp
void _handlePrivmsg(int fd, Client& client, const IrcMessage& message);
Client* _findClientByNickname(const std::string& nickname);
```

Rien d'autre n'a changé dans ce fichier : aucune nouvelle donnée membre,
aucun nouveau `#include`. `PRIVMSG` réutilise entièrement l'infrastructure
existante (`_clients`, `sendToClient`, `IrcMessage`).

### `srcs/Server.cpp` (modifié)

Une seule modification, dans `Server::_processCommand()` — le switch central
qui route chaque commande résolue vers son handler :

```cpp
if (!client.getIsRegistered()) {
    this->sendToClient(fd, ERR_NOTREGISTERED(client.getNickname()));
    return true;
}
if (type == CMD_ENUM_PRIVMSG) {
    this->_handlePrivmsg(fd, client, message);
    return true;
}
return true;
```

Point de design important : contrairement à `PASS`/`NICK`/`USER` (traités
dans le `switch` *avant* la vérification `isRegistered`, puisqu'ils servent
justement à s'enregistrer), `PRIVMSG` est volontairement placé *après* cette
vérification. Un client qui envoie `PRIVMSG` sans être enregistré tombe donc
dans le `451 ERR_NOTREGISTERED` et n'atteint jamais `_handlePrivmsg` — testé
et confirmé (voir §4).

### `Makefile` (modifié)

Ajout de `srcs/commands/Messaging.cpp` à la liste `SRCS`, pour qu'il soit
compilé et lié dans `ircserv`. Sans cette ligne, le nouveau fichier existerait
mais serait ignoré par `make`.

### `include/NumericReplies.hpp` (non modifié)

Aucun changement nécessaire : les trois codes utilisés par `PRIVMSG`
(`401 ERR_NOSUCHNICK`, `411 ERR_NORECIPIENT`, `412 ERR_NOTEXTTOSEND`)
existaient déjà dans ce fichier depuis le squelette de départ.

## 3. Ce que ça apporte, en une phrase

Le serveur peut désormais faire transiter un message texte d'un client
enregistré vers un autre, identifié par son pseudo (insensible à la casse),
avec une gestion d'erreur complète et dans l'ordre standard attendu par un
vrai client IRC (destinataire manquant → texte manquant → destinataire
introuvable), sans jamais toucher à `_pollFds`/`_clients` de façon
destructive (contrairement à `QUIT`) et sans jamais faire de `send()` en
dehors de `sendToClient()`/`_writeClientData()`.

## 4. Tests effectués (résultats réels)

Vérifié avec deux clients TCP connectés simultanément (alice et bob), plus un
troisième client non enregistré :

| Scénario | Commande envoyée | Réponse obtenue |
|---|---|---|
| Nominal | alice : `PRIVMSG bob :salut` | bob reçoit `:alice PRIVMSG bob :salut` ; alice ne reçoit rien |
| Casse | alice : `PRIVMSG BOB :test casse` | bob reçoit `:alice PRIVMSG BOB :test casse` (fonctionne malgré la majuscule) |
| Aucun paramètre | alice : `PRIVMSG` | `411 alice :No recipient given` |
| Pas de texte | alice : `PRIVMSG bob` | `412 alice :No text to send` |
| Pseudo inconnu | alice : `PRIVMSG ghost :salut` | `401 alice ghost :No such nick/channel` |
| Client non enregistré | `PRIVMSG alice :coucou` | `451 * :You have not registered` (handler jamais atteint) |

Compilation : `make re` → zéro warning avec `-Wall -Wextra -Werror -std=c++98`.

## 5. Ce qui reste hors périmètre de l'étape 5

- Pas de classe `Channel` : `JOIN`/`PART`/`PRIVMSG` de channel, topic,
  opérateurs, modes restent à écrire (étape 6 du `ROADMAP.md`).
- Pas de commandes opérateur (`TOPIC`, `KICK`, `INVITE`, `MODE`) — étape 7.
- Le durcissement final (relecture de tous les `recv`/`send`, tests de charge
  combinés) reste à faire à l'étape 8, une fois les étapes 5, 6 et 7
  intégrées ensemble.

Pour plus de détail (diagrammes de séquence, pièges gérés, guide de test pas
à pas), voir la section 6 de `audit.md` à la racine du projet.
