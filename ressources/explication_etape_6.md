# Étape 6 — Classe `Channel` + `JOIN`/`PART`/`PRIVMSG` de channel

Ce document explique ce qui a été ajouté pour l'étape 6 du `ROADMAP.md`
(nouvelle classe `Channel`, commandes `JOIN`/`PART`, et extension de
`PRIVMSG` pour cibler un channel). Il est destiné à un collègue qui n'a pas
suivi le développement en direct : chaque fichier touché, chaque fonction, et
ce que ça apporte au serveur — sur le même modèle que
`ressources/explication_etape_5.md`.

**Statut : terminé et testé à 100 %.** `make re` compile sans aucun warning
(`-Wall -Wextra -Werror -std=c++98`). Le plan de tests détaillé dans
`audit.md` (section 7) a été rejoué avec un script Python pilotant plusieurs
vrais clients TCP simultanés, y compris les cas limites (déconnexion brutale,
flood, fragmentation) — résultats au §5.

## 1. Vue d'ensemble : qu'est-ce que ça ajoute concrètement ?

Avant cette étape, deux clients pouvaient s'enregistrer et se parler en
message privé nick-à-nick (étape 5), mais il n'existait aucune notion de
salon : `JOIN`/`PART` tombaient dans le `default: break;` du dispatch central
sans aucune réponse, et `PRIVMSG` ne savait envoyer qu'à un pseudo. L'étape 6
introduit :

- Une classe `Channel` (nom, topic, membres, opérateurs, invités, modes
  `i`/`t`/`k`/`l`) — les modes existent comme *état* dès maintenant mais ne
  sont pas encore modifiables (`MODE` arrive à l'étape 7), donc toujours
  désactivés en pratique.
- `JOIN <#channel>` : crée le channel s'il n'existe pas (créateur = seul
  opérateur), ou rejoint un channel existant en respectant `+i`/`+k`/`+l`.
- `PART <#channel>` : quitte un channel, avec suppression automatique si le
  channel devient vide.
- `PRIVMSG <#channel> :<message>` : diffusion à tous les membres sauf
  l'émetteur.
- Le nettoyage des channels lors d'une déconnexion brutale (`_disconnectClient`).

## 2. Fichiers modifiés ou créés

### `include/Channel.hpp` (nouveau fichier)

Déclaration de la classe `Channel`, en miroir de `Client` (Orthodox Canonical
Form, uniquement `std::string`/`std::set`, pas de STL exotique, C++98).

- **Données membres** : `_name`, `_topic`/`_hasTopic`, `_members`
  (`std::set<int>` de fds), `_operators` (sous-ensemble de `_members`),
  `_invited`, et l'état des 4 modes (`_inviteOnly`, `_topicRestricted`,
  `_hasKey`/`_key`, `_hasUserLimit`/`_userLimit`).
- **Membres/opérateurs** : `addMember`/`removeMember`/`isMember`/
  `memberCount`/`getMembers`/`isEmpty`, `addOperator`/`removeOperator`/
  `isOperator`.
- **Invités (+i)** : `invite`/`isInvited`.
- **Topic** : `getTopic`/`hasTopic`/`setTopic`.
- **Modes** : getters/setters pour chaque mode — les setters ne seront
  appelés qu'à l'étape 7 (`MODE`), mais doivent déjà exister pour que `JOIN`
  puisse *lire* cet état dès maintenant.

### `srcs/Channel.cpp` (nouveau fichier)

Implémentation de tout ce qui précède. Point notable : le constructeur
`Channel(const std::string& name)` remet systématiquement tous les modes et
le topic à leur valeur par défaut — important pour qu'un channel recréé
après avoir été détruit (dernier membre parti) ne récupère jamais l'état
d'une "vie" précédente (voir test "channel recréé proprement" au §5).

### `srcs/commands/Channels.cpp` (nouveau fichier)

Le cœur de l'étape 6, calqué sur le style de `Registration.cpp`/
`Messaging.cpp` (namespace anonyme pour les utilitaires internes, puis les
méthodes de `Server`).

- **`equalsIgnoreCase()`** (namespace anonyme) : copie de la fonction du même
  nom dans `Messaging.cpp`/`Registration.cpp`. Duplication volontaire pour
  ne pas toucher aux fichiers existants pendant l'étape 6 (voir `audit.md`
  §3) — à extraire dans un header commun si une 4ᵉ copie devenait
  nécessaire.

- **`Server::_findChannel(const std::string& name)`**
  Parcourt `_channels` (`std::map<std::string, Channel>`) et renvoie un
  pointeur vers le channel dont le nom correspond, comparaison insensible à
  la casse — exactement le même principe que `_findClientByNickname`. La clé
  stockée dans la map garde la casse du tout premier `JOIN` qui a créé le
  channel ; c'est la recherche qui est insensible à la casse, pas la clé
  elle-même.

- **`Server::_broadcastToChannel(const Channel&, const std::string& message, int excludeFd)`**
  Envoie `message` à tous les membres du channel sauf `excludeFd`, uniquement
  via `sendToClient` (jamais de `send()` direct — règle absolue du projet
  depuis l'étape 2). `excludeFd = -1` (jamais un fd valide) pour `JOIN`/
  `PART`, où l'émetteur doit aussi recevoir sa propre annonce ; le fd de
  l'émetteur pour `PRIVMSG`, qui ne s'échoie pas à lui-même.

- **`Server::_handleJoin(int fd, Client& client, const IrcMessage& message)`**
  1. `message.params.empty()` → `461 ERR_NEEDMOREPARAMS`.
  2. Ne traite que le premier channel/clé avant une éventuelle virgule (pas
     de multi-JOIN, hors périmètre obligatoire).
  3. Nom ne commençant pas par `#` → `476 ERR_BADCHANMASK`.
  4. Channel introuvable → **création** : `_channels[chanName] = Channel(...)`,
     le créateur devient membre **et** opérateur.
  5. Channel existant :
     - déjà membre → ne rien faire (pas de re-broadcast, `std::set` évite le
       doublon) ;
     - `+i` actif et pas invité → `473 ERR_INVITEONLYCHAN` ;
     - `+k` actif et clé fournie ≠ clé du channel → `475 ERR_BADCHANNELKEY` ;
     - `+l` actif et complet → `471 ERR_CHANNELISFULL` ;
     - sinon → ajouté comme membre simple (pas opérateur).
  6. Une fois membre : broadcast `:<nick> JOIN <#channel>` à **tous les
     membres, émetteur inclus** (contrairement à `PRIVMSG`), puis
     `331`/`332` (topic) et `353`/`366` (liste des membres, `@` devant les
     opérateurs) envoyés **uniquement** au nouvel arrivant.

- **`Server::_handlePart(int fd, Client& client, const IrcMessage& message)`**
  1. Params vides → `461`.
  2. Channel introuvable → `403 ERR_NOSUCHCHANNEL`.
  3. Pas membre → `442 ERR_NOTONCHANNEL`.
  4. Sinon : broadcast `:<nick> PART <#channel>[ :<raison>]` à tous les
     membres **y compris l'émetteur** (même logique que `JOIN`), puis retrait
     effectif du membre/opérateur. Si le channel devient vide, il est détruit
     de `_channels` (recherche par `channel->getName()`, la clé exacte
     stockée, pas par ce que le client a tapé — évite un problème de casse).

### `srcs/commands/Messaging.cpp` (modifié)

`_handlePrivmsg` distinguait déjà `411`/`412` avant de chercher un
destinataire ; il branche maintenant sur le premier caractère de la cible :

```cpp
if (!target.empty() && target[0] == '#') {
    Channel* channel = this->_findChannel(target);
    if (channel == NULL) { ... ERR_NOSUCHCHANNEL (403) ... }
    if (!channel->isMember(fd)) { ... ERR_NOTONCHANNEL (442) ... }
    // broadcast à tous les membres sauf l'émetteur
}
// sinon : comportement nick-à-nick inchangé (étape 5)
```

Décision documentée dans `audit.md` §5.3 : `403` (channel inexistant) plutôt
que `404 ERR_CANNOTSENDTOCHAN` (réservé à un futur mode qui bloquerait
l'écriture à un membre, ex. `+n`/`+m`, hors périmètre ici) — le roadmap
mentionnait "404/442" de façon générique, ce choix précise lequel s'applique
à quel cas. Le reste de la fonction (nick-à-nick, `401`/`411`/`412`) est
inchangé.

### `include/Server.hpp` (modifié)

- `# include "Channel.hpp"`.
- Nouveau membre privé : `std::map<std::string, Channel> _channels;` (registre
  des channels par nom, même principe que `_clients` par fd).
- Nouvelles déclarations privées :
  ```cpp
  void _handleJoin(int fd, Client& client, const IrcMessage& message);
  void _handlePart(int fd, Client& client, const IrcMessage& message);
  Channel* _findChannel(const std::string& name);
  void _broadcastToChannel(const Channel& channel, const std::string& message, int excludeFd);
  ```

### `srcs/Server.cpp` (modifié)

- **`Server::operator=`** : copie désormais aussi `_channels` (sinon une
  copie de `Server` perdrait silencieusement tous les channels — n'arrive pas
  en pratique puisque `Server` n'est jamais copié après `start()`, mais
  l'Orthodox Canonical Form doit rester cohérente).
- **`_processCommand`** : deux nouveaux `case` après le bloc `isRegistered`,
  au même niveau que `PRIVMSG` :
  ```cpp
  if (type == CMD_ENUM_JOIN) {
      this->_handleJoin(fd, client, message);
      return true;
  }
  if (type == CMD_ENUM_PART) {
      this->_handlePart(fd, client, message);
      return true;
  }
  ```
- **`_disconnectClient`** : c'était le piège principal de cette étape. Avant
  de fermer le socket, la fonction parcourt maintenant tous les channels,
  retire le fd de `_members`/`_operators` partout où il est membre, et
  supprime tout channel devenu vide — avec le pattern C++98 obligatoire
  `_channels.erase(it++)` (pas `it = map.erase(it)`, API C++11 uniquement).
  Sans ça, un client tué brutalement (`kill -9`, terminal fermé sans `QUIT`)
  laisserait son fd périmé dans `_members`, et le prochain `PRIVMSG`/`JOIN`
  broadcasté sur ce channel appellerait `sendToClient` sur un fd fermé.

### `Makefile` (modifié)

Ajout de `srcs/Channel.cpp` et `srcs/commands/Channels.cpp` à `SRCS`, et de
`include/Channel.hpp` à `HEADERS` — sans ça les nouveaux fichiers existeraient
mais seraient ignorés par `make`.

### `include/NumericReplies.hpp` (nettoyage, pas de nouveau code)

Tous les codes utilisés par cette étape existaient déjà (`331`/`332`,
`353`/`366`, `403`/`442`/`461`/`471`/`473`/`475`/`476`). Une macro
`ERR_BADCHANMASK` (476) était définie **deux fois** avec le même corps
(doublon introduit pendant le développement de l'étape 6) — supprimé, une
seule définition reste.

### Nettoyage de style (pas de changement de comportement)

En relisant le code de l'étape 6 avant de le documenter, quelques
incohérences de forme ont été corrigées, sans toucher à la logique :

- `include/Server.hpp` : style d'include mélangé (`#include` vs
  `# include`), indentation à tabulations mélangée avec des espaces sur les
  déclarations `_handlePrivmsg`/`_findClientByNickname`, commentaire "chanelle"
  reformulé.
- `include/Channel.hpp` : mêmes tabulations parasites sur `isInviteOnly`,
  `addOperator`, et le bloc de commentaires "Divers".
- `srcs/Server.cpp` : le bloc `if (type == CMD_ENUM_PRIVMSG)` dans
  `_processCommand` avait une accolade mal indentée (mélange tabs/espaces) —
  réaligné avec le reste du switch.

### `ROADMAP.md` (modifié)

Étapes 5 et 6 cochées (`[x]`) avec une note "Vérifié" résumant les scénarios
testés, dans le même style que les étapes 1 à 4 déjà validées.

## 3. Ce que ça apporte, en une phrase

Plusieurs clients peuvent désormais créer/rejoindre un même salon, voir qui
en fait partie et qui est opérateur, s'y envoyer des messages diffusés à
tout le monde sauf l'émetteur, le quitter proprement, et un client qui
disparaît brutalement (crash, `kill -9`, fermeture de terminal) est nettoyé
de tous ses channels sans jamais faire planter le serveur ni laisser un fd
mort dans une liste de diffusion.

## 4. Tests effectués (résultats réels)

Script Python pilotant plusieurs vrais clients TCP enregistrés simultanément
(Alice, Bob, Carol, Dave, Eve, Frank, Grace, Heidi), reproduisant le plan de
tests `audit.md` §7 :

| Scénario | Commande envoyée | Résultat obtenu |
|---|---|---|
| JOIN — création | Alice : `JOIN #test` | Alice reçoit `:Alice JOIN #test`, `331` (pas de topic), `353` avec `@Alice`, `366` |
| JOIN — channel existant | Bob : `JOIN #test` | Bob reçoit son propre `353` (`@Alice Bob`, Bob sans `@`) ; Alice reçoit `:Bob JOIN #test` |
| PRIVMSG channel | Alice : `PRIVMSG #test :salut` | Bob reçoit `:Alice PRIVMSG #test :salut` ; Alice ne reçoit rien (pas d'écho) |
| PRIVMSG channel, non-membre | Carol (non membre) : `PRIVMSG #test :coucou` | Carol reçoit `442` ; ni Alice ni Bob ne reçoivent rien |
| PRIVMSG channel inexistant | `PRIVMSG #inexistant :yo` | `403` |
| PART | Bob : `PART #test` | Bob et Alice reçoivent `:Bob PART #test` ; un `PRIVMSG #test` suivant n'atteint plus Bob |
| PART — dernier membre | Alice : `PART #test` | Alice reçoit son PART ; channel détruit |
| Recréation propre | Dave : `JOIN #test` (après destruction) | Dave redevient seul opérateur, topic vide (pas d'héritage de l'ancienne vie du channel) |
| PART sur channel existant, non-membre | Dave : `PART #autrechan` (existant, créé par Heidi) | `442` |
| PART sur channel inexistant | Dave : `PART #inexistant2` | `403` |
| JOIN/PART sans argument | `JOIN` / `PART` seuls | `461` dans les deux cas |
| Déconnexion brutale | Eve ferme sa socket sans `QUIT` pendant qu'elle est dans `#brutal` avec Frank | Serveur toujours vivant, Frank peut toujours écrire dans `#brutal` ; channel détruit proprement une fois vide, recréation ultérieure saine (Grace seule opératrice) |
| Fragmentation | `JOIN #frag` envoyé en 5 paquets TCP séparés (`JO`/`IN `/`#fr`/`ag\r`/`\n`) | Recollé et traité comme une seule commande, `353` avec `@Frag` reçu |
| Flood | 500× `JOIN #flood` d'un client déjà membre, envoyés d'un coup | Aucun broadcast dupliqué (0 message `JOIN #flood` renvoyé), serveur toujours vivant et acceptant de nouvelles connexions ensuite |
| Non-régression | commande inconnue `FOOBAR` | `421`, comme avant l'étape 6 |

Compilation : `make re` → zéro warning avec `-Wall -Wextra -Werror -std=c++98`.

## 5. Ce qui reste hors périmètre de l'étape 6

- Pas de commandes opérateur (`TOPIC`, `KICK`, `INVITE`, `MODE`) — étape 7.
  Les modes `+i`/`+t`/`+k`/`+l` existent comme état sur `Channel` et sont
  déjà *lus* par `JOIN`, mais rien ne permet encore de les *activer*.
- Pas de multi-JOIN/multi-PART (`JOIN #a,#b`) : seul le premier channel de la
  liste est traité, volontairement (hors périmètre obligatoire).
- Le durcissement final combiné (relecture de tous les `recv`/`send`, tests
  de charge avec toutes les commandes ensemble) reste à faire à l'étape 8,
  une fois les étapes 6 et 7 intégrées.

Pour plus de détail (design détaillé de `Channel`, pièges C++98 spécifiques,
plan de tests complet), voir `audit.md` à la racine du projet — ce document
en est le résumé "post-implémentation".
