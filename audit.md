# Audit — Étape 2 : boucle réseau complète (lecture + écriture)

Portée de cet audit : implémentation de la gestion `POLLOUT` (buffer de sortie
non-bloquant) et de la détection propre des déconnexions/erreurs
(`POLLHUP`/`POLLERR`/`POLLNVAL`), sur la base du code existant (`Client`,
`Server`, `main`). Correspond à l'étape 2 du `ROADMAP.md`.

## 1. Modifications apportées

### `Client.hpp` / `Client.cpp`

- Ajout de `bool hasOutputData() const` : renvoie `!_outputBuffer.empty()`.
  C'est la seule information dont `Server` a besoin pour décider s'il doit
  garder `POLLOUT` actif sur le `pollfd` du client — `Client` reste seul
  responsable de son propre buffer, `Server` ne lit jamais `_outputBuffer`
  directement pour cette décision.
- Nettoyage : suppression de `#include <iostream>` dans `Client.hpp`, non
  utilisé (ni dans le `.hpp` ni dans le `.cpp`).
- Correction mineure d'indentation sur le commentaire d'`extractNextLine()`
  (mélange tabulations/espaces).

### `Server.hpp` / `Server.cpp`

- **Bug corrigé** : `_acceptNewConnection()` armait `POLLIN | POLLOUT` dès
  l'acceptation de la connexion. Un client sans rien à recevoir déclenchait
  donc `POLLOUT` à chaque tour de `poll()` pour rien. Un nouveau client
  n'écoute désormais que `POLLIN`.
- Nouvelle méthode privée `_writeClientData(int fd)` : flush du buffer de
  sortie d'un client via `send()`, appelée uniquement depuis `start()` quand
  `poll()` signale `POLLOUT`.
- Nouvelle méthode privée `_findPollFd(int fd)` : recherche linéaire dans
  `_pollFds`, factorisée pour être réutilisée par `_disconnectClient()`,
  `_writeClientData()` et `sendToClient()` (avant, la même boucle de recherche
  était dupliquée).
- Nouvelle méthode publique `sendToClient(int fd, const std::string& message)` :
  point d'entrée unique pour qu'un futur handler de commande (étape 3) mette
  du texte dans le buffer de sortie d'un client et active `POLLOUT`. Aucun
  `send()` n'est jamais appelé directement depuis cette méthode.
- `_readClientData(int fd)` renvoie désormais `bool` (`false` si le client a
  été déconnecté pendant l'appel) au lieu de `void`, pour que `start()` sache
  que `_pollFds` vient de se décaler.
- `start()` : la boucle sur `_pollFds` gère maintenant `POLLOUT` et
  `POLLHUP`/`POLLERR`/`POLLNVAL`, et corrige un bug latent de décalage
  d'index quand un client est retiré du vecteur pendant l'itération (détail
  au §3).
- `errno == EINTR` sur un `poll()` interrompu par un signal *autre* que
  `SIGINT`/`SIGQUIT` ne fait plus planter le serveur avec une exception : la
  boucle relance simplement `poll()`.
- Ajout des includes `<cerrno>`, `<cstring>`, `<stdexcept>` dans `Server.hpp`
  (utilisés par `Server.cpp`, qui n'inclut que ce header) — auparavant ces
  symboles compilaient par inclusion transitive, ce qui est fragile.

Aucun fichier `main.cpp` n'a été touché : il reste minimal (parsing d'argv,
`sigaction`, instanciation + lancement de `Server`), conforme à la consigne.

## 2. Le système `POLLOUT` étape par étape

Le principe : **on ne fait jamais de `send()` en dehors du passage par
`poll()`**, et on n'écoute `POLLOUT` que quand il y a réellement quelque
chose à écrire.

```
1. Un handler de commande veut répondre à un client (fd=5) :
       server.sendToClient(5, "001 nick :Welcome\r\n");

2. sendToClient() :
       - clientIt->second.appendOutputBuffer(message)   // Client stocke les octets
       - _findPollFd(5)->events |= POLLOUT              // Server arme l'écoute

3. Au prochain tour de la boucle start(), poll() renvoie POLLOUT
   pour le pollfd du fd 5 dès que le socket est inscriptible.

4. start() voit revents & POLLOUT -> appelle _writeClientData(5) :
       - send(5, outputBuffer.c_str(), outputBuffer.size(), 0)
       - le nombre d'octets réellement acceptés par le noyau (peut être
         inférieur à la taille du buffer) est retiré via clearOutputBuffer()
       - si hasOutputData() redevient false -> events &= ~POLLOUT
         (on arrête d'écouter POLLOUT tant qu'il n'y a plus rien à envoyer)
```

Schéma du cycle de vie de `POLLOUT` pour un `pollfd` donné :

```
   [connexion acceptée]
          |
          v
   events = POLLIN  ------------------------------+
          |                                       |
          | sendToClient() appelé                 | buffer vidé
          | (events |= POLLOUT)                   | (events &= ~POLLOUT)
          v                                       |
   events = POLLIN | POLLOUT  --- send() partiel--+
          |
          | send() a tout écrit d'un coup
          v
   events = POLLIN
```

Pourquoi c'est important : si `POLLOUT` restait actif en permanence, `poll()`
retournerait immédiatement à chaque appel (un socket est presque toujours
inscriptible), transformant la boucle événementielle en boucle active
(busy-loop) qui consomme du CPU pour rien. Le désarmement systématique dès
que le buffer est vide est ce qui évite ça.

## 3. Gestion des erreurs et déconnexions

`poll()` peut renvoyer plusieurs bits dans `revents` en même temps. L'ordre
de traitement dans `start()` est volontairement :

1. **`POLLHUP | POLLERR | POLLNVAL` en premier** : si l'un de ces bits est
   présent, le client est déconnecté immédiatement via `_disconnectClient()`
   et on ignore tout le reste (`POLLIN`/`POLLOUT` sur ce même `revents` n'ont
   plus de sens sur un fd qu'on vient de fermer).
   - `POLLHUP` : le pair a fermé la connexion (déconnexion brutale, terminal
     fermé sans `QUIT`).
   - `POLLERR` : erreur socket en attente (ex. RST reçu).
   - `POLLNVAL` : fd invalide dans le tableau de `poll()` (défensif — ne
     devrait pas arriver si `_pollFds` est bien maintenu, mais coûte rien à
     couvrir).
2. **`POLLIN`** ensuite : lecture via `_readClientData()`. Si `recv()` renvoie
   `0` (fermeture propre du pair) ou une erreur fatale, `_disconnectClient()`
   est aussi appelée et la méthode renvoie `false`.
3. **`POLLOUT`** en dernier, seulement si le client est toujours connecté
   après les deux étapes précédentes.

**Piège corrigé** : `_disconnectClient()` fait `_pollFds.erase(it)`, ce qui
décale tous les éléments suivants d'un cran vers la gauche. L'ancienne boucle
`for (i = 0; i < size; ++i)` aurait sauté un élément après chaque
déconnexion (l'élément qui prend l'index `i` juste après l'erase n'était
alors jamais examiné). La boucle décrémente maintenant `i` juste après un
`_disconnectClient()` (ou un `_readClientData()` qui renvoie `false`), pour
que l'itération suivante (`++i`) revienne examiner le même index — qui
contient désormais l'élément suivant.

Dans tous les cas, `_disconnectClient()` reste la seule fonction qui ferme le
socket et nettoie `_pollFds`/`_clients` : `close(fd)`, retrait du `pollfd`
via `_findPollFd()`, puis `_clients.erase(fd)`.

## 4. Guide de test

Compilation stricte (doit rester sans warning) :

```sh
make re   # c++ -Wall -Wextra -Werror -std=c++98 -Iinclude
```

Lancement :

```sh
./ircserv 6667 pass
```

### a) Recollage de paquets fragmentés (`nc -C`)

Reproduit l'exemple du sujet : `com`, puis `man`, puis `d\n` envoyés
séparément doivent être traités comme une seule commande `command`.

```sh
nc -C 127.0.0.1 6667
# taper : com <Ctrl+D> man <Ctrl+D> d <Entrée>
```

Ou en une commande shell (équivalent, sans terminal interactif) :

```sh
( printf 'com'; sleep 0.2; printf 'man'; sleep 0.2; printf 'd\r\n' ) | nc -w 1 127.0.0.1 6667
```

Le log serveur doit afficher une seule ligne `[DEBUG] ... "command"`, jamais
trois lignes séparées.

### b) Écriture / `POLLOUT`

Pour vérifier concrètement l'aller-retour (aucune commande n'est encore
dispatchée à ce stade du projet — étape 3), le plus simple est de brancher
temporairement un echo dans `_readClientData()` :

```cpp
this->sendToClient(fd, "ECHO :" + commandLine + "\r\n");
```

puis :

```sh
echo -e "HELLO\r" | nc -w 1 127.0.0.1 6667
# doit afficher : ECHO :HELLO
```

Retirer ce hook une fois la vérification faite (ce n'est pas une commande du
protocole IRC).

### c) Déconnexions et erreurs

```sh
# Fermeture propre côté client (nc quitte après un timeout court)
( printf 'NICK bob\r\n'; sleep 0.1 ) | nc -w 1 127.0.0.1 6667

# Plusieurs clients simultanés
nc 127.0.0.1 6667 &
nc 127.0.0.1 6667 &
```

Dans les deux cas, vérifier après coup que le process serveur tourne
toujours (`ps -p <pid>`) et que le log affiche bien
`[INFO] Client FD <n> disconnected.` sans trace d'erreur ni de crash.

### d) Non-régression générale

- `make re` ne doit produire aucun warning.
- Le serveur doit survivre à un `Ctrl+C` (arrêt propre, `[INFO] Server
  stopped successfully.`).
- Aucun `recv()`/`send()` ne doit exister ailleurs que dans
  `_readClientData()` / `_writeClientData()`, tous deux appelés uniquement
  depuis la boucle `poll()` de `start()`.

## 5. Ce qui reste ouvert (hors périmètre de cet audit)

- Le contenu de `commandLine` extrait dans `_readClientData()` n'est encore
  que loggé (`std::cout`) : le parsing (commande + paramètres) et le dispatch
  vers des handlers (`PASS`, `NICK`, `USER`, ...) sont l'étape 3 du
  `ROADMAP.md`, non traités ici.
- Pas de classe `Channel` : hors périmètre (étape 6).
- `sendToClient()` est prête à être appelée par les futurs handlers de
  commandes, mais rien ne l'appelle encore en dehors du hook de test décrit
  au §4.b.
