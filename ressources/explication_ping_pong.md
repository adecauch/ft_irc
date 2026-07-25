# PING / PONG — keepalive IRC (correctif indépendant, hors roadmap)

Ce document explique l'ajout du keepalive `PING`/`PONG`. Ce n'est pas une
étape du `ROADMAP.md` : c'est un trou indépendant, repéré en relisant le code,
qui devait être bouché avant de pouvoir tester sereinement avec un vrai client
IRC. Il est destiné à un collègue qui n'a pas suivi le développement en
direct, sur le même modèle que les `explication_etape_*.md`.

**Statut : terminé et testé.** `make re` compile sans aucun warning
(`-Wall -Wextra -Werror -std=c++98`). Vérifié par un script Python (7
scénarios) plus une simulation de la séquence de connexion réelle d'Irssi
(`CAP LS` → `PASS`/`NICK`/`USER` → `CAP END`) — détails au §4.

## 1. Le problème

`CMD_ENUM_PING` et `CMD_ENUM_PONG` étaient bien **résolus** par le parser
(table de `srcs/commands/commands.cpp`), mais **aucun handler** ne les
traitait : ils tombaient dans le `default: break;` du `switch` de
`_processCommand`. Conséquences concrètes :

- un `PING` reçu **avant** l'enregistrement → `451 ERR_NOTREGISTERED` ;
- un `PING` reçu **après** l'enregistrement → aucune réponse du tout.

Or le keepalive IRC est symétrique : un client (ou un serveur) envoie
périodiquement un `PING` porteur d'un *token*, et l'autre bout doit renvoyer un
`PONG` avec le **même token**. Sans `PONG` en retour, un vrai client considère
la connexion muette, affiche du *lag*, puis **ferme la connexion sur timeout**
(quelques minutes). Avec `nc` on ne le voit pas — mais un client de référence
(Irssi, HexChat, WeeChat) décrochait. C'est un bloquant pour l'évaluation, qui
se fait justement avec un vrai client.

## 2. Fichiers modifiés

### `srcs/Server.cpp` (modifié)

**Deux `case` ajoutés dans le `switch` de `_processCommand`, placés
volontairement AVANT la barrière `isRegistered`** (au même niveau que
`PASS`/`NICK`/`USER`/`QUIT`/`CAP`) :

```cpp
case CMD_ENUM_PING:
    this->_handlePing(fd, message);
    return true;
case CMD_ENUM_PONG:
    return true; // réponse du client à un PING serveur : rien à renvoyer
```

Le placement est le point de design important : un client peut pinguer
**pendant** le handshake, et un `PING` ne doit **jamais** recevoir `451`.
Le traiter avant la vérification d'enregistrement garantit ça. Le `PONG`
entrant (réponse du client à un éventuel `PING` serveur) est simplement
accepté sans réponse, et surtout sans tomber dans le gate `451`.

**Nouvelle méthode `Server::_handlePing(int fd, const IrcMessage& message)` :**

```cpp
void Server::_handlePing(int fd, const IrcMessage& message) {
    std::string token = message.params.empty() ? std::string() : message.params[0];
    this->sendToClient(fd, "PONG ft_irc :" + token + "\r\n");
}
```

Elle renvoie le nom de serveur `ft_irc` (cohérent avec les numériques `002`
`RPL_YOURHOST` et `004` `RPL_MYINFO` qui l'utilisent déjà) suivi du token tel
quel. Un `PING` sans token est géré sans planter (token vide). La signature ne
prend pas de `Client&` : `PING` n'a besoin ni du pseudo ni de l'état
d'enregistrement, juste du fd pour répondre et du message pour le token. Comme
tout le reste du projet, la réponse passe par `sendToClient()` — aucun
`send()` direct.

### `include/Server.hpp` (modifié)

Une déclaration privée ajoutée à côté de `_handlePass`/`_handleNick`/
`_handleUser`, avec lesquels `PING` partage le fait d'être traité au niveau
connexion, avant l'enregistrement :

```cpp
void _handlePing(int fd, const IrcMessage& message);
```

### `srcs/commands/commands.cpp` (non modifié)

`PING`/`PONG` étaient déjà dans la table de résolution `{nom, enum}` depuis le
squelette — seul le handler manquait.

## 3. Ce que ça apporte, en une phrase

Le serveur répond désormais correctement au keepalive, ce qui permet à un vrai
client IRC de rester connecté indéfiniment sans décrocher sur *ping timeout* —
c'est ce qui débloque les tests (et l'évaluation) avec Irssi/HexChat/WeeChat.

## 4. Tests effectués (résultats réels)

Script Python sur une connexion TCP brute (7 scénarios), tous verts :

| Scénario | Commande | Réponse obtenue |
|---|---|---|
| PING pendant le handshake | `PING :handshake42` (avant PASS/NICK/USER) | `PONG` avec le token, **jamais `451`** |
| Handshake toujours OK ensuite | `PASS`/`NICK`/`USER` | `001` reçu normalement |
| Token avec `:` | `PING :LAG1234567` | `PONG ft_irc :LAG1234567` |
| Token sans `:` | `PING token789` | `PONG` avec `token789` |
| PONG entrant | `PONG :ft_irc` | aucune réponse, aucune erreur |
| Token avec espace | `PING :abc def` | renvoyé entier (`abc def`) |
| Non-régression | `FOOBAR` | `421` |

**Simulation de la connexion réelle d'Irssi** (ordre exact `CAP LS 302` →
`PASS`/`NICK`/`USER` → `CAP END`) : `001` et `004` bien reçus, jamais de `451`
ni de `421` (le `CAP` déjà ignoré en silence depuis l'étape 4 ne génère pas
d'erreur). La séquence complète d'un vrai client aboutit proprement.

Compilation : `make re` → zéro warning avec `-Wall -Wextra -Werror -std=c++98`.

## 5. Ce qui reste hors périmètre

- Le serveur ne **génère pas** lui-même de `PING` vers les clients inactifs
  (détection proactive des connexions mortes). Il se contente de répondre aux
  `PING` reçus, ce qui suffit pour l'obligatoire et pour un client de référence
  qui pingue de son côté.
- La négociation `CAP` reste un simple « ignore en silence » : la connexion
  aboutit, avec au pire un léger délai selon le client. Une réponse `CAP`
  complète (`CAP LS` → liste vide → `CAP END`) est possible mais optionnelle,
  non nécessaire à l'obligatoire.
