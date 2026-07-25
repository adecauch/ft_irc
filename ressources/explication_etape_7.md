# Étape 7 — Commandes opérateur : `TOPIC` / `MODE` / `KICK` / `INVITE`

Ce document explique ce qui a été ajouté pour l'étape 7 du `ROADMAP.md`
(les commandes réservées aux opérateurs de channel). Il est destiné à un
collègue qui n'a pas suivi le développement en direct : chaque fichier
touché, chaque fonction, et ce que ça apporte au serveur — sur le même
modèle que `ressources/explication_etape_5.md` et `_6.md`.

**Statut : terminé et testé à 100 %.** `make re` compile sans aucun warning
(`-Wall -Wextra -Werror -std=c++98`). Les quatre commandes ont été vérifiées
avec des scripts Python pilotant plusieurs vrais clients TCP enregistrés
simultanément : **56 scénarios au total** (11 pour TOPIC, 24 pour MODE, 21
pour KICK+INVITE), tous verts — détails au §4.

## 1. Vue d'ensemble : qu'est-ce que ça ajoute concrètement ?

Avant cette étape, l'étape 6 avait posé la classe `Channel` avec l'*état* des
quatre modes (`i`/`t`/`k`/`l`) et une liste d'invités, mais rien ne permettait
de les *modifier* : les modes restaient donc toujours désactivés, la liste
d'invités toujours vide, et `TOPIC`/`KICK`/`INVITE`/`MODE` tombaient dans le
`return true;` final du dispatch sans aucune réponse. L'étape 7 rend tout ça
vivant :

- `TOPIC <#chan> [:<topic>]` : lecture ou écriture du sujet du salon, l'écriture
  étant réservée aux opérateurs quand le mode `+t` est actif.
- `MODE <#chan> [<modes> [args]]` : consultation ou modification des modes du
  channel (`i`, `t`, `k`, `o`, `l`) — c'est cette commande qui *active* enfin
  les restrictions que `JOIN` lisait déjà (invite-only, clé, limite) et le `+t`
  qui verrouille `TOPIC`.
- `KICK <#chan> <user> [:<raison>]` : éjection d'un membre par un opérateur.
- `INVITE <nick> <#chan>` : invitation qui remplit la liste `_invited`, ce qui
  permet à l'invité de contourner l'invite-only le temps d'un `JOIN`.

Tout est concentré dans `srcs/commands/Channels.cpp` (déjà présent depuis
l'étape 6) : **aucun nouveau fichier**, et donc **aucune modification du
Makefile** nécessaire cette fois-ci.

## 2. Fichiers modifiés ou créés

### `srcs/commands/Channels.cpp` (modifié — cœur de l'étape)

Tout le code métier de l'étape 7 vit ici, dans le style des étapes
précédentes (namespace anonyme pour les utilitaires internes au fichier, puis
les méthodes de `Server`). Un `#include <sstream>` a été ajouté pour la
conversion d'entiers (pas de `std::to_string` en C++98).

**Utilitaires ajoutés au namespace anonyme :**

- **`sizeToString(std::size_t)`** : convertit un entier non signé en chaîne via
  `std::ostringstream`. Sert à afficher la limite `+l` dans la réponse `324`.
- **`parsePositiveInt(const std::string&, std::size_t&)`** : valide l'argument
  d'un `+l`. Renvoie `false` (et laisse la sortie inchangée) sur chaîne vide,
  non entièrement numérique, valeur nulle, ou dépassement d'`unsigned long`
  (l'`istringstream` met alors `failbit`). Évite qu'un `+l abc` ou `+l 0` ne
  fasse n'importe quoi.
- **`buildModeString(const Channel&)`** : reconstruit la chaîne `+modes [args]`
  des modes actifs pour `RPL_CHANNELMODEIS` (324), dans l'ordre fixe `i,t,k,l`.
  La clé (`+k`) et la limite (`+l`) ne sont ajoutées en argument que si le mode
  est actif.

**`Server::_handleTopic(int fd, Client&, const IrcMessage&)`**

Vérifications dans l'ordre : `461` (aucun paramètre) → `403` (channel
introuvable) → `442` (émetteur pas membre). Ensuite, deux comportements
distingués **par le nombre de paramètres** — et c'est le parser qui rend cette
distinction possible :

- `TOPIC #chan` → 1 paramètre → **lecture** : `332 RPL_TOPIC` si un topic est
  posé, `331 RPL_NOTOPIC` sinon.
- `TOPIC #chan :nouveau` → 2 paramètres → **écriture**.
- `TOPIC #chan :` → 2 paramètres dont le 2ᵉ vide → **effacement**.

Pour l'écriture, si le channel est en `+t` et que l'émetteur n'est pas
opérateur → `482 ERR_CHANOPRIVSNEEDED`. Sinon le topic est posé
(`setTopic`) ou effacé (`clearTopic`, cf. §`Channel`) puis diffusé à **tous
les membres, émetteur inclus** au format `:<nick> TOPIC #chan :<topic>`.

**`Server::_handleMode(int fd, Client&, const IrcMessage&)`**

Le point d'entrée de MODE. Ordre : `461` (aucun paramètre) → cible qui n'est
pas un channel (ne commence pas par `#`) **ignorée en silence** (les modes
utilisateur type `MODE <nick> +i`, envoyés par certains clients au démarrage,
sont hors périmètre du sujet ; on ne renvoie pas d'erreur parasite) → `403`
(channel introuvable) → `442` (pas membre). Ensuite :

- Sans modestring (`MODE #chan`) → **consultation** : `324 RPL_CHANNELMODEIS`
  construit par `buildModeString`.
- Avec modestring → toute modification exige d'être **opérateur** (`482`
  sinon), puis délègue à `_applyModeChanges`.

**`Server::_applyModeChanges(int fd, Client&, Channel&, const IrcMessage&)`**

Le moteur de MODE, appelé une fois les droits vérifiés. Le modestring est lu
**de gauche à droite** : un `+`/`-` fixe le sens courant, chaque lettre
suivante s'applique dans ce sens. Les arguments (`params[2]`, `params[3]`, …)
sont consommés **dans l'ordre** par les modes qui en ont besoin :

| Mode | `+` | `-` |
|---|---|---|
| `i` (invite-only) | 0 arg | 0 arg |
| `t` (topic restreint) | 0 arg | 0 arg |
| `k` (clé) | 1 arg (clé, `461` si absente) | 1 arg **si présent** (valeur ignorée) |
| `o` (opérateur) | 1 arg (nick, `461` si absent, `441` si pas membre) | idem |
| `l` (limite) | 1 arg (entier > 0, sinon consommé et ignoré) | 0 arg |

Le point le plus subtil est `-k` : la RFC passe la clé en argument, et Irssi
l'envoie effectivement (`MODE #c -k <clé>`). On la **consomme si elle est
présente** — sinon un modestring combiné comme `-k+o clé nick` décalerait les
arguments et `+o` prendrait « clé » pour un pseudo. Les lettres inconnues (ex.
`b`, liste de bans envoyée par certains clients) sont ignorées sans planter.

Seuls les changements **réellement appliqués** sont ensuite rediffusés au
channel (`:<nick> MODE #chan +ot-l …`), en réécrivant le `+`/`-` uniquement
quand le sens change. Un mode ignoré ou en erreur n'apparaît pas dans l'écho.

**`Server::_handleKick(int fd, Client&, const IrcMessage&)`**

`KICK <#chan> <user> [:<raison>]`. Comme `JOIN`/`PART`, seul le premier
channel/pseudo avant une éventuelle virgule est traité (pas de multi-KICK,
hors périmètre). Ordre : `461` (< 2 paramètres) → `403` → `442` (kickeur pas
membre) → `482` (kickeur pas opérateur) → `441` (cible absente du channel).
Le commentaire est optionnel ; à défaut, on utilise le pseudo du kickeur
(convention usuelle). Point important : le message
`:<kickeur> KICK #chan <kické> :<raison>` est diffusé à **tous les membres
AVANT le retrait effectif** — c'est ce message qui fait quitter le salon côté
client du kické ; le retirer d'abord l'empêcherait de savoir qu'il a été
éjecté. Le pseudo canonique du kické (`target->getNickname()`) est utilisé
dans le message, pas ce que le kickeur a tapé. Si le channel devient vide, il
est détruit (même logique que `PART`).

**`Server::_handleInvite(int fd, Client&, const IrcMessage&)`**

`INVITE <nick> <#chan>`. **Attention : l'ordre des arguments est l'inverse de
KICK** (pseudo d'abord, channel ensuite — RFC 2812). Ordre des vérifs :
`461` (< 2 paramètres) → `403` → `442` (inviteur pas membre) → `482`
**uniquement si le channel est en `+i`** et que l'inviteur n'est pas opérateur
(sur un salon ouvert, n'importe quel membre peut inviter) → `401` (pseudo cible
inexistant) → `443` (cible déjà membre). En cas de succès : le fd de l'invité
est ajouté à `_invited`, l'inviteur reçoit `341 RPL_INVITING`, et l'invité
reçoit la notification `:<inviteur> INVITE <invité> #chan`.

**`Server::_handleJoin` (modifié)**

Une seule ligne ajoutée après l'entrée effective d'un membre :
`channel->removeInvited(fd);`. Une invitation ne vaut que pour **un seul
JOIN** — sans ça, un invité pourrait quitter puis revenir indéfiniment sur un
channel `+i`. C'est un no-op si le client n'était pas invité (notamment le
créateur d'un channel neuf).

### `include/Channel.hpp` / `srcs/Channel.cpp` (modifié)

Deux petites méthodes ajoutées, cohérentes avec l'état déjà modélisé par la
classe :

- **`clearTopic()`** : remet `_topic` à vide **et** `_hasTopic` à `false`.
  Indispensable pour que `TOPIC #chan :` (effacement) soit distinct d'un topic
  vide : après effacement, un futur `JOIN` renvoie bien `331` (« pas de
  topic ») et non un `332` avec un sujet vide. C'est cohérent avec le choix de
  l'étape 6 de séparer délibérément `_topic` (le texte) et `_hasTopic`
  (le booléen « un topic est posé »).
- **`removeInvited(int fd)`** : retire un fd de `_invited` (`std::set::erase`,
  no-op si absent). Appelée par `_handleJoin` pour consommer l'invitation.

### `include/Server.hpp` (modifié)

Cinq déclarations privées ajoutées à côté des handlers existants :

```cpp
void _handleTopic(int fd, Client& client, const IrcMessage& message);
void _handleMode(int fd, Client& client, const IrcMessage& message);
void _applyModeChanges(int fd, Client& client, Channel& channel, const IrcMessage& message);
void _handleKick(int fd, Client& client, const IrcMessage& message);
void _handleInvite(int fd, Client& client, const IrcMessage& message);
```

### `srcs/Server.cpp` (modifié)

Quatre nouveaux branchements dans `_processCommand`, **après** la barrière
`isRegistered` (au même niveau que `PRIVMSG`/`JOIN`/`PART`) — ces commandes
n'ont de sens qu'une fois le client enregistré :

```cpp
if (type == CMD_ENUM_TOPIC)  { this->_handleTopic(fd, client, message);  return true; }
if (type == CMD_ENUM_MODE)   { this->_handleMode(fd, client, message);   return true; }
if (type == CMD_ENUM_KICK)   { this->_handleKick(fd, client, message);   return true; }
if (type == CMD_ENUM_INVITE) { this->_handleInvite(fd, client, message); return true; }
```

### `include/NumericReplies.hpp` (non modifié)

Aucun changement : **tous** les codes utilisés par l'étape 7 existaient déjà
dans le squelette (`324` modes, `331`/`332` topic, `341` inviting, `401`,
`403`, `441`, `442`, `443`, `461`, `471`, `473`, `475`, `476`, `482`).

### `Makefile` (non modifié)

Contrairement aux étapes 5 et 6, aucun fichier n'a été créé : tout est allé
dans `Channels.cpp`, `Channel.cpp` et `Server.cpp`, déjà listés dans `SRCS`.

### `ROADMAP.md` (modifié)

Les quatre sous-cases de l'étape 7 cochées (`[x]`) avec une note « Vérifié »
résumant les scénarios testés, dans le style des étapes précédentes.

## 3. Ce que ça apporte, en une phrase

Un opérateur peut désormais gérer entièrement son salon — poser/verrouiller le
sujet, activer les restrictions d'accès (invite-only, clé, limite), promouvoir
ou rétrograder d'autres opérateurs, éjecter et inviter — et toutes les
restrictions que l'étape 6 savait déjà *lire* (`+i`/`+k`/`+l` sur `JOIN`, `+t`
sur `TOPIC`) sont enfin *pilotables*, chaque changement étant diffusé aux
membres concernés sans jamais faire de `send()` hors de `sendToClient()`.

## 4. Tests effectués (résultats réels)

Trois scripts Python pilotant plusieurs vrais clients TCP enregistrés
simultanément. Extrait des scénarios (56 au total, tous verts) :

**TOPIC (11 scénarios)**

| Scénario | Résultat |
|---|---|
| Lecture sans topic | `331 RPL_NOTOPIC` |
| Écriture par un opérateur | diffusé à tous les membres, émetteur inclus |
| Lecture après pose | `332` + le texte |
| Écriture par un non-op quand `+t` inactif | autorisée (RFC 2812) |
| Effacement `TOPIC #c :` puis relecture | `331` (pas un `332` vide) |
| `461`/`403`/`442`, non-régression `421` | conformes |

**MODE (24 scénarios)**

| Scénario | Résultat |
|---|---|
| Consultation channel neuf | `324` avec `+` |
| `+t` puis TOPIC par non-op | `482` (chaîne op/non-op complète validée) |
| `+i` puis JOIN non invité | `473` |
| `+k secret` puis JOIN mauvaise/bonne clé | `475` / accepté ; `-k` libère |
| `+l 2` plein puis 3ᵉ JOIN | `471` ; `+l abc` ignoré sans plantage |
| `+o`/`-o` | promotion/rétrogradation diffusées |
| Consultation cumulée | `324 +itkl <clé> <limite>` (ordre `i,t,k,l`) |
| `461`/`403`/`442`/`441`/`482`, `MODE <nick>` ignoré | conformes |

**KICK + INVITE (21 scénarios)**

| Scénario | Résultat |
|---|---|
| KICK par un op | le kické **et** les autres reçoivent l'éjection |
| KICK sans commentaire | défaut = pseudo du kickeur |
| KICK : `461`/`403`/`442`/`482`/`441` | conformes |
| INVITE sur channel ouvert | `341` à l'inviteur + notification à l'invité |
| INVITE : `461`/`403`/`442`/`401`/`443` | conformes |
| INVITE non-op sur salon ouvert | autorisé |
| Synergie `+i` : invité → JOIN OK → PART → re-JOIN | `473` (invitation à usage unique) |
| INVITE sous `+i` par un non-op | `482` |

Compilation : `make re` → zéro warning avec `-Wall -Wextra -Werror -std=c++98`.

## 5. Ce qui reste hors périmètre de l'étape 7

- Pas de multi-cible (`MODE #a,#b`, `KICK #c a,b`, `JOIN #a,#b`) : seul le
  premier élément est traité, volontairement (hors périmètre obligatoire).
- Pas de modes utilisateur (`MODE <nick> …`) : ignorés en silence.
- Modes de channel autres que `i`/`t`/`k`/`o`/`l` (ex. `b` liste de bans) :
  ignorés sans erreur.
- Le keepalive `PING`/`PONG`, nécessaire pour tester sereinement avec un vrai
  client (Irssi, HexChat, WeeChat), a été ajouté en parallèle — voir
  `ressources/explication_ping_pong.md`.
- Le durcissement final combiné (relecture de tous les `recv`/`send`, tests de
  charge avec toutes les commandes ensemble) reste à faire à l'étape 8.

Pour plus de détail (design, pièges C++98, plan de tests complet), voir
`audit.md` à la racine du projet — ce document en est le résumé
« post-implémentation ».
