# ft_irc — Roadmap

Plan de travail interne (pas un livrable noté). Périmètre : partie obligatoire uniquement, pas de bonus (file transfer / bot exclus — de toute façon le sujet ne les évalue que si l'obligatoire est parfait).

## 0. Où on en est

Déjà écrit et réutilisable tel quel :

- `Client` : fd, nickname, username, hostname, realname, flags `hasGivenPassword`/`isRegistered`, buffers in/out avec append/clear. Squelette complet.
- `Server` : socket d'écoute (`SO_REUSEADDR`, non-bloquant, `bind`/`listen`), `poll()` unique, `accept()`, lecture (`recv`) qui remplit le buffer d'entrée du client, déconnexion propre sur `recv() == 0`/erreur.
- `commands.hpp` : toutes les commandes utiles sont déjà nommées (`PASS`, `NICK`, `USER`, `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `MODE`, `PRIVMSG`...) + un enum tout prêt pour un dispatch par `switch`.
- `NumericReplies.hpp` : quasiment tous les codes numériques nécessaires à l'obligatoire sont déjà écrits (welcome 001-004, erreurs nickname 431-433, erreurs channel 403/404/441-443/471/473/475/482, erreurs registration 451/461/462/464...). Bonne base, à réutiliser directement, pas besoin de la réécrire.

Ce qui manque pour que ça compile et tourne :

- Pas de `Makefile`.
- Pas de `main.cpp` → aucun point d'entrée, rien n'est exécutable pour l'instant.
- Pas de gestion de signal (`SIGINT`/`SIGQUIT`) alors que `Server.hpp` en parle déjà en commentaire.

Ce qui manque côté logique (le gros du travail) :

- La boucle `poll()` ne traite que `POLLIN`. Rien n'est jamais renvoyé à un client : aucun appel à `send()` nulle part dans le code actuel.
- Aucune extraction de ligne (`\r\n`) : le buffer d'entrée se remplit mais personne ne le lit.
- Aucun dispatch de commande : l'enum de `commands.hpp` n'est utilisé nulle part encore.
- Aucune classe `Channel` : rien pour `JOIN`/`PART`/topic/opérateurs/modes.
- Donc PASS/NICK/USER/QUIT/PRIVMSG/JOIN/PART/TOPIC/KICK/INVITE/MODE restent tous à écrire.

## 1. Règles qui valent zéro si elles sautent

À garder en tête en permanence pendant le dev, pas juste à la fin :

- Le serveur ne doit **jamais** planter, même à court de mémoire. Un crash = note 0.
- Un seul `poll()` (ou équivalent) pour toutes les opérations (lecture, écriture, accept). Tout `recv`/`send` fait en dehors de ce mécanisme = note 0, même si le fd est non-bloquant et que ça "marche quand même".
- Pas de `fork()`.
- Ne pas développer de client IRC, ne pas faire de communication serveur-à-serveur.
- Sur macOS, `fcntl()` n'est autorisé que sous la forme exacte `fcntl(fd, F_SETFL, O_NONBLOCK)` — aucun autre flag. Le code actuel respecte déjà ça, à ne pas casser en ajoutant autre chose plus tard.
- Le serveur doit recoller les paquets fragmentés avant de traiter une commande (le sujet donne l'exemple `nc -C` qui envoie `com`, `man`, `d\n` séparément — ça doit être traité comme `command\n`).

## 2. Décisions à prendre avant/pendant le code

- **Client de référence** : à figer maintenant et ne plus en changer pour les tests (le sujet insiste dessus, c'est celui utilisé en évaluation). Le code cite déjà Irssi en commentaire dans `commands.hpp` — partir là-dessus semble cohérent, sinon choisissez-en un autre ensemble (WeeChat, HexChat...) mais un seul.
- **Dispatch des commandes** : un point d'entrée unique, par exemple `Server::_processCommand(Client&, const std::string& cmd, const std::vector<std::string>& params)`, qui fait un `switch` sur l'enum déjà défini dans `commands.hpp` et appelle un handler par commande. Séparer les handlers dans plusieurs fichiers (par thème : auth, messages, channels) pour pouvoir avancer à deux sans toucher les mêmes fichiers en même temps — par exemple sous `srcs/commands/`, en miroir du dossier `include/commands/` déjà créé.
- **Channels** : `Server` garde un registre `std::map<std::string, Channel>` par nom de channel, et `Channel` référence ses membres par `fd` — cohérent avec ce que fait déjà `Server` pour les `Client`.
- **Fin de ligne** : gérer `\r\n` (norme IRC), et accepter aussi un `\n` seul pour rester tolérant avec des tests faits à la main via `nc`.
- **Opérateur par défaut** : le premier client qui `JOIN` un channel qui n'existe pas le crée et devient automatiquement opérateur — comportement standard, à confirmer entre vous avant de coder KICK/MODE.

## 3. Étapes

### Étape 1 — Squelette exécutable (petit) ✅

- [x] `main.cpp` : parse `argv` (port + password), valide le port, instancie `Server`, `try/catch` autour du démarrage (aucune exception ne doit remonter jusqu'à un crash).
- [x] Gestion de `SIGINT`/`SIGQUIT` (`sigaction`) pour arrêter proprement la boucle — signal handler minimal (`Server::requestStop()`, juste un flag) + cleanup réel dans `Server::stop()` appelé hors du handler, pour rester async-signal-safe.
- [x] `Makefile` : règles `NAME`, `all`, `clean`, `fclean`, `re`, compilation en `c++ -Wall -Wextra -Werror -std=c++98`, pas de relink inutile. (Un `-Iinc` → `-Iinclude` corrigé, c'est ce qui cassait la compilation.)

**Fini quand** : `./ircserv 6667 pass` tourne, accepte une connexion `nc 127.0.0.1 6667` sans planter, s'arrête proprement au `Ctrl+C`.

### Étape 2 — Boucle réseau complète : lecture ET écriture (moyen) ✅

- [x] Extraire les lignes complètes du buffer d'entrée (découpage sur `\r\n`), traiter chaque ligne une par une (`clearInputBuffer` est déjà prête pour ça).
- [x] Envoi réel des données : flush de `_outputBuffer` via `send()` déclenché sur `POLLOUT`. N'activer l'intérêt `POLLOUT` pour un fd que quand il a effectivement quelque chose à écrire, pour ne pas tourner à vide.
- [x] Gérer `POLLHUP`/`POLLERR` proprement (déconnexion). (`POLLNVAL` couvert aussi, par défensivité.)

**Fini quand** : le test `nc -C` du sujet (envoi de `com`, puis `man`, puis `d\n` séparément via Ctrl+D) est recollé en une seule commande avant traitement, sans plantage.

**Vérifié** : fragmentation ✅, POLLOUT dynamique ✅, EAGAIN/EWOULDBLOCK ✅, déconnexion brutale d'un client sans impact sur les autres ✅. Bug corrigé : `extractNextLine()` renvoyait `""` à la fois pour "pas de ligne complète" et pour "ligne vide reçue", donc une commande arrivée juste après une ligne vide (même paquet) restait bloquée dans le buffer jusqu'au prochain événement réseau sur ce client. Signature changée en `bool extractNextLine(std::string&)`.

### Étape 3 — Parsing + dispatch générique (moyen) ✅

- [x] Parser une ligne IRC : commande + liste de paramètres + paramètre "trailing" après `" :"`. → `Parser::parse()` dans `include/Parser.hpp` / `srcs/Parser.cpp`, branché dans `Server::_readClientData` (log `[DEBUG]` en attendant le dispatch réel). Gère : params multiples, trailing (y compris vide), espaces multiples, préfixe `:nick` optionnel en tête de ligne, casse (commande normalisée en majuscules), et n'importe quel binaire/garbage sans planter.
- [x] Brancher ce parsing sur l'enum de `commands.hpp`. → `resolveCommand()` déclarée dans `include/commands/commands.hpp`, implémentée dans `srcs/commands/commands.cpp` (table `{nom, enum}` + recherche linéaire, 17 entrées). Appelée juste après `Parser::parse()` dans `_readClientData`. Testé : les 17 commandes (casse mélangée) résolvent vers le bon ordinal, commande inconnue et ligne vide → `CMD_UNKNOWN`.
- [x] Répondre `421` (déjà défini) pour toute commande inconnue ; ne jamais planter sur une entrée absurde (ligne vide, commande sans paramètre, données binaires, flood...). Ligne vide → aucune réponse (pas une vraie tentative de commande), commande inconnue non vide → `421` via `sendToClient` (jamais de `send()` direct).

**Fini quand** : n'importe quelle ligne envoyée au serveur produit soit un traitement, soit une erreur propre — jamais un crash. **Vérifié** : commande inconnue → 421 correct ; commande connue → pas de 421 ; ligne vide → silence ; flood de 5000 commandes d'un coup → 5000×421, serveur toujours vivant ; ligne de 5 Mo sans `\r\n` → pas de crash, connexion toujours utilisable ensuite.

### Étape 4 — Authentification & enregistrement (moyen) ✅

- [x] `PASS` : vérifie le mot de passe passé en argument du programme (`464` si faux, `461` si absent, `462` si déjà enregistré).
- [x] `NICK` : validation format (lettre + jusqu'à 9 car., `432` sinon) + unicité insensible à la casse (`433`), `431` si absent.
- [x] `USER` : enregistre username/realname (`461` si `<4` params, `462` si déjà enregistré). Les champs `<mode>`/`<unused>` sont ignorés, comme les vrais ircd.
- [x] Séquence complète PASS+NICK+USER (peu importe l'ordre) → `001`-`004`, une seule fois, idempotent.
- [x] Toute commande reconnue mais envoyée avant enregistrement complet → `451` (sauf `CAP`, volontairement ignorée en silence — Irssi l'envoie en tout premier).
- [x] `QUIT` : ferme réellement la connexion (`close(fd)` via `_disconnectClient`), sans affecter les autres clients.

Architecture : dispatch central `Server::_processCommand()` (switch sur l'enum) dans `srcs/Server.cpp`, handlers PASS/NICK/USER dans `srcs/commands/Registration.cpp` (nouveau, miroir de `commands.cpp`). Point d'attention géré : `_processCommand` renvoie `false` quand `QUIT` vient de fermer la socket, pour que `_readClientData` arrête immédiatement de toucher `client`/`_pollFds` (référence qui serait sinon dangling après l'`erase`).

**Fini quand** : le client de référence choisi arrive à se connecter, donner un nick et un username, et apparaît "connecté" côté serveur. **Vérifié** (14 scénarios via script) : handshake complet → 001-004 ; mauvais mot de passe → jamais de welcome même avec NICK/USER après ; ordre NICK/USER avant PASS → complète quand même ; nick invalide/absent/déjà pris (y compris collision insensible à la casse) → 432/431/433 ; USER incomplet → 461 ; PASS/USER après enregistrement → 462 ; commande avant enregistrement → 451 ; CAP jamais d'erreur ; QUIT ferme vraiment la socket sans toucher aux autres clients ; deux clients s'enregistrent sans interférence ; garbage binaire pendant le handshake → pas de crash.

### Étape 5 — Messages privés (petit/moyen) ✅

- [x] `PRIVMSG <nick> :<message>` entre deux clients déjà connectés (pas besoin des channels pour ça, peut se faire avant l'étape 6).
- [x] Erreurs : cible absente (`401`), pas de destinataire (`411`), pas de texte (`412`).

**Fini quand** : deux clients connectés en simultané peuvent se parler en privé.

**Vérifié** (script Python, `srcs/commands/Messaging.cpp`) : Alice → Bob livré tel quel (`:Alice PRIVMSG Bob :...`) ; nick inexistant → `401` ; pas de destinataire → `411` ; pas de texte → `412`.

### Étape 6 — Classe `Channel` + `JOIN`/`PART`/`PRIVMSG` de channel (gros) ✅

- [x] Nouvelle classe `Channel` : nom, topic, membres, opérateurs, liste d'invités, modes (`i`, `t`, `k`, `l`), clé, limite d'utilisateurs. (`include/Channel.hpp` / `srcs/Channel.cpp`)
- [x] `JOIN` : crée le channel s'il n'existe pas (créateur = opérateur), sinon ajoute le membre en respectant les restrictions déjà actives (`+i`, `+k`, `+l`), envoie `353`/`366`/topic courant, prévient les autres membres.
- [x] `PART` : quitte le channel, prévient les autres membres, supprime le channel s'il devient vide.
- [x] `PRIVMSG <#channel> :<message>` : diffusion à tous les membres sauf l'émetteur, erreurs `403`/`442` si channel/membre invalide (voir `audit.md` section 5.3 pour la justification du choix `403` plutôt que `404`, réservé à un futur mode d'écriture restreint).

**Fini quand** : plusieurs clients peuvent rejoindre le même channel et discuter dedans.

**Vérifié** (script Python, `srcs/commands/Channels.cpp`) : JOIN création (`@op` dans `353`, `331`/`366`) ; JOIN sur channel existant (nouveau membre sans `@`, broadcast `JOIN` aux membres déjà présents) ; PRIVMSG channel livré aux membres sauf l'émetteur, `442` pour un non-membre, `403` pour un channel inexistant ; PART broadcasté à tous y compris l'émetteur, channel détruit et recréé proprement (topic vide, nouvel opérateur unique) une fois vide ; `461` sur JOIN/PART sans argument ; `442`/`403` sur PART selon que le channel existe ; déconnexion brutale (fermeture socket sans QUIT) → fd retiré du channel sans crash, channel vide supprimé silencieusement ; ligne `JOIN` fragmentée recollée correctement ; flood de 500×`JOIN` sur un channel déjà rejoint → aucun broadcast dupliqué, serveur toujours vivant ; non-régression `421` sur commande inconnue.

### Étape 7 — Commandes opérateur (gros)

- [ ] `TOPIC` : lecture/écriture, restreinte aux opérateurs si mode `+t` actif.
- [ ] `KICK` : éjecte un membre (opérateur uniquement), diffuse l'événement au channel.
- [ ] `INVITE` : ajoute à la liste d'invités, confirme à l'inviteur (`341`, déjà écrit).
- [ ] `MODE` sur un channel : `i`, `t`, `k`, `o`, `l`, avec les erreurs déjà prévues (`471`, `473`, `475`, `482`).

**Fini quand** : toutes les commandes listées par le sujet comme "spécifiques aux opérateurs" fonctionnent avec le client de référence.

### Étape 8 — Durcissement (moyen)

- [ ] Relire chaque `recv`/`send` du code : aucun ne doit exister en dehors du passage par `poll()`.
- [ ] Revérifier qu'aucun `fcntl` n'utilise autre chose que `fcntl(fd, F_SETFL, O_NONBLOCK)`.
- [ ] Tests de charge : plusieurs clients simultanés, déconnexions brutales en plein milieu d'une commande, lignes très longues, paquets fragmentés, flood de connexions.
- [ ] S'assurer qu'aucune entrée ne peut faire planter le serveur.

**Fini quand** : rien de ce qu'on envoie au serveur, aussi tordu soit-il, ne le fait planter.

### Étape 9 — README + soutenance (petit)

- [ ] Première ligne du `README.md` en italique, au format imposé : `*This project has been created as part of the 42 curriculum by <login1>, <login2>.*`
- [ ] Sections `Description`, `Instructions`, `Resources` (docs/tutos utilisés + déclaration de l'usage de l'IA, en précisant sur quelles tâches précises).
- [ ] Chacun doit pouvoir expliquer n'importe quelle ligne du code, y compris celle écrite avec une IA — le sujet le précise explicitement (chapitre III) et c'est un motif d'échec direct en soutenance si ce n'est pas le cas.
- [ ] Se préparer à une petite modification de code demandée en direct pendant l'évaluation (mentionné dans le sujet).

## 4. Répartition à deux

- La personne qui a déjà écrit `Client`/`Server` continue naturellement les étapes 1, 2, 3, 4, 5 (réseau, parsing, auth, PRIVMSG).
- L'autre personne prend les étapes 6 et 7 (`Channel` + commandes opérateur) en parallèle, dès que l'étape 3 fixe la signature des handlers — même si le dispatcher lui-même n'est pas encore fini, il suffit de se mettre d'accord sur la signature à l'avance.
- Resynchronisation avant l'étape 8 pour intégrer et tester ensemble.
- Étape 9 à deux, chacun documentant la partie qu'il a écrite.

## 5. Tests

- Le test `nc -C` du sujet avec envoi fragmenté (Ctrl+D entre chaque morceau).
- Plusieurs `nc` ou plusieurs instances du client de référence en simultané.
- Séquence complète : connexion → PASS → NICK → USER → JOIN → PRIVMSG → MODE/KICK/INVITE/TOPIC → QUIT → déconnexion brutale (fermer le terminal sans QUIT) pour vérifier que le serveur détecte et nettoie bien.
- Cas limites : mauvais mot de passe, nickname déjà pris, commande envoyée avant enregistrement, commande inconnue, ligne vide, ligne sans `\r\n` final.

## 6. Checklist avant rendu

- [ ] `make re` compile sans warning avec les flags imposés, y compris avec `-std=c++98` explicite.
- [ ] Aucun crash trouvé pendant les tests de l'étape 8.
- [ ] Un seul `poll()` (ou équivalent) dans tout le code, et tous les `recv`/`send` passent par lui.
- [ ] `README.md` conforme au format imposé par le sujet.
- [ ] Dépôt Git propre (pas de fichiers de test, pas de binaires, `.gitignore` à jour).
- [ ] Les deux comprennent l'ensemble du code, pas juste "leur" partie.
