*This project has been created as part of the 42 curriculum by adecauch, ramahrez, anturtsc.*

# ft_irc

## Description

Serveur IRC écrit en C++98, conforme à la partie obligatoire du sujet ft_irc.
Il implémente le protocole défini par la RFC 1459 (avec quelques emprunts à la
RFC 2812) et fonctionne avec de vrais clients IRC, Irssi étant notre client de
référence.

Le serveur accepte plusieurs clients simultanément dans un **processus unique et
mono-thread**, sans `fork()`. Tout le multiplexage repose sur un **unique appel à
`poll()`** : chaque `accept()`, `recv()` et `send()` n'a lieu qu'en réaction à un
événement signalé par lui, et aucune décision du serveur ne repose sur `errno`.
Toutes les sockets sont non bloquantes, de sorte qu'un client lent ou figé ne
peut jamais retarder les autres.

### Fonctionnalités

- **Authentification** par mot de passe donné au lancement, puis enregistrement
  par `PASS` / `NICK` / `USER` dans n'importe quel ordre, avec unicité des
  pseudos insensible à la casse.
- **Messages privés** entre clients et **messages de channel** diffusés à tous
  les membres.
- **Channels** avec sujet, liste de membres et opérateurs. Le créateur d'un
  channel en devient automatiquement opérateur ; un channel vide est détruit.
- **Commandes d'opérateur** : `KICK`, `INVITE`, `TOPIC`, et `MODE` avec les cinq
  modes demandés — `+i` (sur invitation), `+t` (sujet réservé aux opérateurs),
  `+k` (clé), `+o` (donner/retirer le statut d'opérateur), `+l` (limite de
  membres).
- **Réassemblage des paquets fragmentés** : le protocole IRC circule sur un flux
  d'octets, une commande coupée en plusieurs morceaux est recollée avant d'être
  exécutée.
- **Keepalive** `PING`/`PONG`, et `CAP` acceptée en silence pour la négociation
  qu'envoient les clients modernes.
- **Réponses numériques** du protocole pour tous les cas d'erreur (401, 403,
  411, 412, 421, 431 à 433, 441 à 443, 451, 461, 462, 464, 471, 473, 475, 476,
  482).

### Commandes reconnues

| Commande | Usage |
|---|---|
| `PASS` | `PASS <motdepasse>` |
| `NICK` | `NICK <pseudo>` |
| `USER` | `USER <username> <mode> <unused> :<realname>` |
| `QUIT` | `QUIT [:<raison>]` |
| `JOIN` | `JOIN <#channel> [<cle>]` |
| `PART` | `PART <#channel> [:<raison>]` |
| `PRIVMSG` | `PRIVMSG <pseudo\|#channel> :<message>` |
| `TOPIC` | `TOPIC <#channel> [:<sujet>]` |
| `MODE` | `MODE <#channel> [<+/-modes> [<arguments>]]` |
| `KICK` | `KICK <#channel> <pseudo> [:<commentaire>]` |
| `INVITE` | `INVITE <pseudo> <#channel>` |
| `PING` | `PING <token>` |

`NOTICE`, `WHO` et `WHOIS` sont reconnues et acceptées sans réponse : le sujet ne
les demande pas, et les ignorer silencieusement évite d'envoyer une erreur
parasite à un client qui les émet automatiquement.

## Instructions

### Compilation

```bash
make
```

Compile avec `c++ -Wall -Wextra -Werror -std=c++98` et produit l'exécutable
`ircserv`. Les règles `clean`, `fclean` et `re` sont disponibles.

### Lancement

```bash
./ircserv <port> <mot_de_passe>
```

Exemple :

```bash
./ircserv 6667 secretpass
```

Le serveur écoute sur toutes les interfaces réseau. `Ctrl+C` ou `Ctrl+\`
l'arrêtent proprement, en fermant toutes les sockets ouvertes.

### Se connecter avec Irssi

```bash
irssi -c 127.0.0.1 -p 6667 -w secretpass -n Alice
```

Puis, dans Irssi :

```
/join #general
/topic Bienvenue
/msg Bob salut
/mode #general +t
/kick Bob au revoir
/invite Carol #general
```

### Se connecter avec netcat

Utile pour voir le protocole brut :

```bash
nc 127.0.0.1 6667
```

Puis, en tapant ligne par ligne :

```
PASS secretpass
NICK Bob
USER bob 0 * :Bob Le Testeur
JOIN #general
PRIVMSG #general :salut tout le monde
```

### Vérifications effectuées

Le serveur a été validé sur une VM Fedora, pour être dans les mêmes conditions
que les postes de l'école :

- Compilation sans le moindre avertissement sur Fedora (gcc) et sur macOS
  (clang), avec les flags imposés.
- Suite de 79 vérifications automatisées, toutes réussies, couvrant la
  connexion, l'enregistrement, les messages, les channels, les commandes
  d'opérateur, les envois fragmentés, les déconnexions brutales, les lignes
  démesurées, le flood de commandes et de connexions, et le scénario du client
  figé par `SIGSTOP` puis inondé.
- `valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes` : aucune
  fuite (`All heap blocks were freed`), aucune erreur mémoire, et aucun
  descripteur laissé ouvert en dehors de `stdin`/`stdout`/`stderr`.
- Validation manuelle avec Irssi comme client de référence, y compris en
  parallèle de `nc`.

## Resources

### Documentation du protocole

- [RFC 1459 — Internet Relay Chat Protocol](https://datatracker.ietf.org/doc/html/rfc1459)
  — la référence du sujet : grammaire des messages, réponses numériques, modes
  de channel.
- [RFC 2812 — IRC Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812)
  — consultée pour les points que la RFC 1459 laisse flous, notamment l'ordre
  des arguments d'`INVITE`, la restriction du `TOPIC` sous `+t`, et la forme du
  préfixe `nick!user@host`.
- [Modern IRC client protocol](https://modern.ircdocs.horse/) — utile pour
  vérifier ce que font réellement les serveurs actuels là où les RFC divergent.

### Programmation réseau

- `man 2 poll`, `man 2 recv`, `man 2 send`, `man 2 accept`, `man 2 fcntl`,
  `man 2 sigaction` — la source principale sur le comportement exact des appels
  système, en particulier les écritures partielles de `send()` et la sémantique
  de `poll()`.
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — pour la
  mise en place des sockets et la logique du multiplexage.

### Outils

- Irssi, notre client IRC de référence. La commande `/rawlog open <fichier>` a
  été déterminante pour déboguer : elle enregistre les octets réellement
  échangés, ce qui permet de distinguer « le serveur n'a rien envoyé » de « le
  client a ignoré le message ».
- `nc` (netcat), pour tester le protocole à la main et reproduire les envois
  fragmentés décrits par le sujet.
- `valgrind`, pour les fuites mémoire et les descripteurs oubliés.

### Utilisation de l'intelligence artificielle

Conformément au chapitre III du sujet, nous déclarons l'usage d'une IA (Claude)
sur les tâches précises suivantes.

**Durcissement du serveur (relecture et corrections de robustesse) :**

- Correction de cinq défauts de robustesse : croissance non bornée du buffer
  d'entrée, boucle à 100 % de CPU sur épuisement des descripteurs, descripteurs
  fantômes dans la liste des invités après recyclage d'un fd par le noyau,
  référence pendante lors de l'effacement d'un channel, et absence de diffusion
  du `QUIT` et des changements de `NICK` aux channels.
- Ajout du préfixe complet `nick!user@host` sur les messages.
- Suppression d'une trace de débogage qui affichait le mot de passe en clair.

**Tests et documentation :**

- Écriture de la suite de tests automatisée et du protocole de test manuel avec Irssi.
- Rédaction de la documentation interne du projet et de ce README.
