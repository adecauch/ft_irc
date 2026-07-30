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

- [x] `TOPIC` : lecture/écriture, restreinte aux opérateurs si mode `+t` actif. → `_handleTopic` dans `srcs/commands/Channels.cpp`. Lecture (`331`/`332`) vs écriture décidée par le nombre de params (le parser distingue `TOPIC #c` de `TOPIC #c :`), effacement propre via `Channel::clearTopic()` (relecture → `331`, pas un `332` vide). Diffusion `:<nick> TOPIC #c :<topic>` à tous les membres émetteur inclus. **Vérifié** (11 scénarios, `test_topic.py`) : lecture sans/avec topic, écriture par op, écriture par non-op quand `+t` inactif, effacement, `461`/`403`/`442`, non-régression `421`. Restriction `+t` → `482` validée une fois `MODE` implémenté (chaîne op/non-op testée dans `test_mode.py`).
- [x] `MODE` sur un channel : `i`, `t`, `k`, `o`, `l`, avec les erreurs déjà prévues (`471`, `473`, `475`, `482`). → `_handleMode`/`_applyModeChanges` dans `srcs/commands/Channels.cpp`. Sans modestring → consultation `324` (`buildModeString`, ordre `i,t,k,l`) ; avec → modification réservée aux opérateurs (`482`). Modestring lu de gauche à droite, `+`/`-` fixe le sens, `k`/`o`/`l` consomment un argument dans l'ordre (`i`/`t` non). `-k` consomme sa clé si présente (comme Irssi) pour ne pas décaler les args suivants. Diffusion du seul changement réellement appliqué (`:<nick> MODE #c +ot-l ...`). **Vérifié** (24 scénarios, `test_mode.py`) : `324`, `+i`→`473`, `+t`→`482` (chaîne op/non-op complète), `+k`→`475`/OK/`-k`, `+l`→`471` + `+l` non numérique ignoré, `+o`/`-o`, `461`/`403`/`442`/`441`/`482`, `MODE` sur un nick (mode user) ignoré, non-régression `421`.
- [x] `KICK` : éjecte un membre (opérateur uniquement), diffuse l'événement au channel. → `_handleKick`. Ordre `461`→`403`→`442`→`482`→`441` ; diffusion `:<op> KICK #c <kické> :<commentaire>` à tous **avant** le retrait (le kické reçoit sa propre éjection), commentaire par défaut = pseudo du kickeur, pseudo canonique du kické dans le message ; channel supprimé s'il devient vide. **Vérifié** (9 scénarios, `test_kick_invite.py`).
- [x] `INVITE` : ajoute à la liste d'invités, confirme à l'inviteur (`341`, déjà écrit). → `_handleInvite` (ordre des args inversé vs KICK : pseudo puis channel). `341` à l'inviteur + `:<inviteur> INVITE <invité> #c` à l'invité ; op requis seulement si `+i` (RFC 2812). Remplit `_invited`, que `JOIN` consulte déjà sous `+i` ; l'invitation est **consommée** au JOIN (usage unique, via `Channel::removeInvited`). Erreurs `461`/`403`/`442`/`482`/`401`/`443`. **Vérifié** (12 scénarios dont contournement `+i` + consommation, `test_kick_invite.py`).

**Fini quand** : toutes les commandes listées par le sujet comme "spécifiques aux opérateurs" fonctionnent avec le client de référence.

### Étape 8 — Durcissement (moyen) ✅

Menée manette en main sur une VM Fedora 44 (gcc 16.1.1), pour être dans les mêmes
conditions que les postes de l'école plutôt que sur macOS. Les items ci-dessous
reprennent aussi la fiche d'évaluation officielle, pas seulement cette roadmap.

- [x] Relire chaque `recv`/`send` du code : aucun ne doit exister en dehors du passage par `poll()`. → Vérifié : **un seul** `poll()`, **un seul** `recv()` (`_readClientData`), **un seul** `send()` (`_writeClientData`), **un seul** `accept()` (`_acceptNewConnection`), tous atteints uniquement depuis la boucle de `start()`. Les handlers de commandes passent tous par `sendToClient()`, qui ne fait que remplir `_outputBuffer` et armer `POLLOUT`.
- [x] **Correction d'un motif qui valait 0** : la fiche exige que `errno` ne pilote **aucune** décision après `accept`/`recv`/`send` (« errno should not be used to trigger specific action, e.g. like reading again after errno == EAGAIN »). Le code testait `EAGAIN`/`EWOULDBLOCK` après `recv()` **et** après `send()`. Supprimé : c'est `poll()` seul qui dit quand lire/écrire, et un retour ≤ 0 vaut déconnexion. Le test `EINTR` après `poll()` a été retiré lui aussi et `<cerrno>` n'est même plus inclus, pour que la propriété soit vérifiable d'un coup d'œil (`grep errno` ne sort que des commentaires).
- [x] **`SIGPIPE` ignoré** (`main.cpp`). C'était le piège le plus dangereux : l'action par défaut de `SIGPIPE` est la terminaison du process, donc le premier `send()` vers un client parti en `RST` tuait le serveur net — soit exactement le « no unexpected termination » qui vaut 0. Via `sigaction` plutôt que `MSG_NOSIGNAL` (absent de macOS) ou `SO_NOSIGPIPE` (absent de Linux), pour rester portable.
- [x] Revérifier qu'aucun `fcntl` n'utilise autre chose que `fcntl(fd, F_SETFL, O_NONBLOCK)`. → Les deux seuls appels (socket d'écoute + socket cliente) sont sous cette forme exacte, sans aucun autre flag.
- [x] Tests de charge : plusieurs clients simultanés, déconnexions brutales en plein milieu d'une commande, lignes très longues, paquets fragmentés, flood de connexions. → Suite de **77 vérifications, 77 réussies**, serveur toujours vivant à la fin et arrêt propre au `SIGINT`. Couvre en particulier le scénario `^Z` de la fiche (client figé par `SIGSTOP`, channel floodé de 20 000 messages, `SIGCONT`) : aucun blocage, serveur réactif pour les autres pendant le flood, et **20 000/20 000** messages délivrés au réveil.
- [x] S'assurer qu'aucune entrée ne peut faire planter le serveur.

**Fini quand** : rien de ce qu'on envoie au serveur, aussi tordu soit-il, ne le fait planter. **Atteint.**

Failles trouvées et corrigées au passage (au-delà des deux motifs à 0 ci-dessus) :

- **Mémoire non bornée en entrée.** Un client qui envoyait des octets sans jamais de `\r\n` faisait grossir `_inputBuffer` indéfiniment jusqu'au `std::bad_alloc`. Plafond à 8 Ko (la RFC 1459 limite une commande à 512 octets, on est large), `417` renvoyé, ligne jetée. Premier jet insuffisant : vider le buffer ne suffisait pas, car le reste de la ligne continuait d'arriver et se collait devant la commande suivante — 2,5 Mo de `A` donnaient **142 fois** `417` puis un `421` avec un nom de commande géant. D'où `Client::markLineOversized()` / `resyncAfterOversizedLine()` : on ignore le flux jusqu'au prochain saut de ligne, seul point de resynchronisation fiable. Résultat : **un seul** `417` par ligne fautive, et la commande d'après passe intacte.
- **Boucle à 100 % de CPU sur un flood de connexions.** `poll()` étant level-triggered, une fois les descripteurs épuisés il resignalait `POLLIN` sur la socket d'écoute à chaque tour alors qu'`accept()` échouait en boucle. Plafond de 512 clients : au-delà on accepte puis on ferme immédiatement, ce qui vide le backlog et laisse `poll()` se rendormir.
- **fd fantômes.** `_disconnectClient` ne nettoyait jamais `Channel::_invited` : un fd recyclé par le noyau pour un tout autre client héritait de l'invitation du précédent et contournait le mode `+i`. Idem pour `_operators`, qui n'était purgé que si le client était encore membre. Les trois registres sont maintenant nettoyés inconditionnellement.
- **Référence pendante.** `_channels.erase(channel->getName())` passait à `erase()` une référence vers une chaîne membre de l'objet qu'il était justement en train de détruire (`PART` et `KICK`). La clé est désormais copiée avant l'appel.
- **`_clients[fd]`** dans `_readClientData` créait par défaut un `Client` fantôme (fd -1) si les deux registres se désynchronisaient. Remplacé par un `find()` explicite.
- **`QUIT` et changement de `NICK` n'étaient pas diffusés** aux channels : le client de référence gardait des listes de pseudos périmées. Nouveau `_broadcastToPeers()`, qui dédoublonne les destinataires via un `std::set` (sinon quiconque partage deux channels avec l'émetteur recevait le message en double). `QUIT` reprend la raison donnée par le client.
- **Filet de sécurité** : le traitement de chaque fd est enveloppé dans un `try/catch`. Une exception remontant d'un handler (typiquement un `std::bad_alloc`) coûte la connexion fautive — dont le retrait libère justement la mémoire — mais jamais le process entier.

**Valgrind** (`--leak-check=full --show-leak-kinds=all --track-fds=yes`, arrêt par
`SIGINT` pour que les destructeurs tournent) sur un scénario couvrant les quatre
voies de sortie d'un channel (`PART`, `KICK`, `QUIT`, socket tuée), les
invitations jamais consommées, 30 cycles de création/destruction de channels, les
registrations avortées et le scénario `^Z` :

```
in use at exit: 0 bytes in 0 blocks
total heap usage: 20,883 allocs, 20,883 frees, 1,299,562 bytes allocated
All heap blocks were freed -- no leaks are possible
FILE DESCRIPTORS: 3 open (3 inherited) at exit
ERROR SUMMARY: 0 errors from 0 contexts
```

Les 3 descripteurs restants sont `stdin`/`stdout`/`stderr`, hérités du shell :
aucune socket oubliée, donc pas de fuite de descripteur non plus.

**Validation avec Irssi** (client de référence, exigé par la fiche) : connexion,
`PASS`/`NICK`/`USER`, `JOIN` avec la liste des pseudos, messages de channel dans
les deux sens, message privé, `TOPIC`, les cinq modes avec `324`, `KICK`, retour
après kick, `QUIT` propagé — tout passe. Connexion stable sur 14 minutes, ce qui
valide au passage le `PING`/`PONG` : Irssi mesure la latence toutes les 60 s et
coupe au bout de 5 minutes sans réponse valable, donc nos `PONG` sont bien
acceptés.

Deux constats côté Irssi ont mené à un correctif :

- Les **numériques sont acceptés sans préfixe serveur** (`001 nick :...` plutôt
  que `:ft_irc 001 nick :...`). Irssi les affiche correctement, donc on ne
  touche pas à `NumericReplies.hpp` — la RFC 2812 demanderait le préfixe, mais
  le modifier voudrait dire toucher tous les codes pour un gain nul et un
  risque réel de casser ce qui est validé.
- En revanche les **messages relayés avaient besoin de `!user@host`**. Avec le
  seul pseudo (`:Alice PRIVMSG`), Irssi ne sait pas que l'expéditeur est un
  *utilisateur* : il **n'ouvre pas la fenêtre de dialogue** à la réception d'un
  message privé — le message arrive mais reste invisible, ce qui aurait fait
  conclure à un correcteur que les privés ne marchent pas — et affiche
  `ServerMode` au lieu de `mode` sur un changement de mode. D'où
  `Client::getPrefix()`, qui construit `nick!user@host` en un seul endroit et
  remplace les neuf concaténations `":" + getNickname() + " ..."` dispersées
  dans `Server.cpp`, `Channels.cpp`, `Messaging.cpp` et `Registration.cpp`.
  Le hostname vient de l'adresse réelle de la connexion TCP, jamais de ce que le
  client prétend dans son `USER`. Cas particulier traité : `NICK` construit son
  message **avant** le renommage, car un changement de pseudo s'annonce avec
  l'ancienne identité en préfixe (`:Bob!bob@host NICK :Bobby`), sinon plus
  personne ne peut faire le lien entre l'ancien et le nouveau nom.

Enfin, la trace `[DEBUG]` par commande a été supprimée de `_readClientData` :
elle recopiait tout ce que le client envoyait, donc le **mot de passe en clair**
(premier paramètre de `PASS`) et le contenu des messages privés. Les traces
conservées sont purement événementielles — connexion, enregistrement,
déconnexion, erreurs — et ne recopient jamais l'entrée.

Après ces changements : **79 vérifications, 79 réussies** (les deux nouvelles
portant sur la forme du préfixe et sur le fait que `NICK` porte bien l'ancienne
identité), serveur toujours vivant, arrêt propre.

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

- [x] `make re` compile sans warning avec les flags imposés, y compris avec `-std=c++98` explicite. → Vérifié sur Fedora 44 / gcc 16.1.1 **et** sur macOS / clang, zéro warning des deux côtés.
- [x] Aucun crash trouvé pendant les tests de l'étape 8.
- [x] Un seul `poll()` (ou équivalent) dans tout le code, et tous les `recv`/`send` passent par lui. → Et aucune décision pilotée par `errno` après eux, ce que la fiche sanctionne d'un 0.
- [ ] `README.md` conforme au format imposé par le sujet.
- [ ] Dépôt Git propre (pas de fichiers de test, pas de binaires, `.gitignore` à jour).
- [ ] Les deux comprennent l'ensemble du code, pas juste "leur" partie.
