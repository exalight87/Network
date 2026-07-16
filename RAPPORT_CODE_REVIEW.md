# Rapport de revue du projet `test_curl`

Date de revue : 2026-07-06  
Périmètre : code C++23 du serveur HTTP, wrapper libcurl, exemples, tests et configuration `xmake`.

## Synthèse exécutive

Le projet fournit une base intéressante : un serveur HTTP léger, un modèle de routes hiérarchiques, une génération de documentation, des exemples et une première suite de tests GoogleTest. Le code reste toutefois au stade prototype avancé plutôt que bibliothèque maintenable et sûre. Les risques principaux se concentrent dans le parsing HTTP, la gestion des buffers réseau, la sécurité du service de fichiers, l'arrêt du serveur, la séparation entre bibliothèque et application de démonstration, et le workflow de test.

La priorité n'est pas d'ajouter beaucoup de fonctionnalités. Il faut d'abord stabiliser les contrats de base : parser une requête complète, router de manière déterministe, répondre correctement aux méthodes non autorisées, servir les fichiers sans traversal, arrêter le serveur proprement, et rendre les tests reproductibles en CI.

## État actuel du projet

### Structure

Le dépôt contient environ 4 947 lignes suivies par cette revue, réparties ainsi :

| Zone | Rôle actuel | Observation |
| --- | --- | --- |
| `src/` | Bibliothèque, serveur réseau, parser, routes, curl, HTTP/2 expérimental, application `main.cpp` | Le code bibliothèque et l'exécutable de démonstration sont mélangés. |
| `tests/` | Tests GoogleTest avec serveur lancé en sous-processus | Les tests couvrent surtout les chemins heureux et dépendent d'un binaire externe. |
| `examples/` | Serveurs simple, API et fichiers statiques | Bon matériel de documentation, mais quelques exemples reproduisent des risques de sécurité. |
| `xmake.lua` | Build, dépendances et exécution des tests après build | La cible statique inclut `src/main.cpp`, et les tests sont lancés dans `after_build`. |
| `README.md` | Documentation utilisateur | Clair pour démarrer, mais ne décrit pas les limites de production ni le workflow de contribution. |

### Modules principaux

#### `HttpServer`

`HttpServer` construit un `ConnectionPool`, lit une première tranche de données, détecte éventuellement HTTP/2, parse une requête HTTP/1.x, puis itère sur `m_routes` pour produire une réponse. La classe expose peu d'API : `start`, `addRoute`, `clearRoutes`, `routeCount`, `enableAutoDocs`.

Points forts :

- API d'ajout de route simple.
- Support des routes imbriquées.
- Auto-documentation activable.
- Gestion keep-alive présente au niveau intention.

Points faibles :

- `start()` est une boucle bloquante sans chemin d'arrêt contrôlé.
- La lecture réseau ne garantit pas qu'une requête complète est disponible.
- Les exceptions pendant le routing sont capturées globalement puis transformées implicitement en 404.
- La route `/docs` est injectée à chaque `start()` quand `enableAutoDocs()` est actif.

#### `HttpRequest` et `URL`

Le parser transforme une requête brute en méthode, URL, version, headers, body et path params. C'est le composant le plus critique du projet, car tout le serveur repose dessus.

Points forts :

- Décodage basique des query params.
- Support des méthodes HTTP courantes.
- Structure de données simple à utiliser dans les handlers.

Points faibles :

- Plusieurs accès aux ranges supposent qu'un token existe.
- `Content-Length` n'est pas utilisé pour borner le body.
- Les bodies multi-lignes ou binaires sont tronqués.
- Le parsing d'URI absolue est fragile.
- Les headers sont sensibles à la casse, alors que HTTP les définit comme case-insensitive.

#### `HttpRoute` et `RouteRegistry`

`HttpRoute` mélange le matching de segments, la vérification de méthode, l'extraction de paramètres et l'appel du handler. `RouteRegistry` est un singleton global utilisé pour refuser les routes dupliquées.

Points forts :

- Le modèle de routes imbriquées est expressif.
- Les paramètres de chemin du type `{id}` sont une bonne base.
- Des tests existent pour les doublons de route.

Points faibles :

- `std::string_view route` peut pointer vers une donnée temporaire.
- `RouteRegistry` global rend plusieurs serveurs indépendants difficiles à isoler.
- Le statut 405 est perdu dans certains cas, car `operator()` renvoie `false`.
- Le matching construit des `string_view` sur des vues temporaires après `join_with`.

#### `NetworkSocket`, `SocketConnection`, `ConnectionPool`

Le serveur ouvre une socket TCP, utilise `epoll` sous Linux, accepte les connexions, puis confie chaque connexion à un pool de threads.

Points forts :

- Utilisation d'un pool de workers plutôt qu'un thread par connexion.
- Compteur de connexions concurrentes.
- `send()` boucle jusqu'à écrire tout le buffer sous Linux.

Points faibles :

- La boucle d'acceptation est infinie et ignore une demande d'arrêt.
- Les sockets acceptées sont repassées en mode bloquant, ce qui peut immobiliser un worker.
- `recv()` lit une seule tranche puis retourne.
- Le nettoyage des connexions et le compteur `m_currentConnections` dépendent d'effets de bord dans `disconnect()`.

#### `HttpResponse`

`HttpResponse` formate une réponse HTTP/1.1 et peut charger un fichier.

Points forts :

- Formatage simple avec `Content-Length`.
- Détection de quelques content-types.
- API pratique pour servir des fichiers.

Points faibles :

- La ligne de statut ne contient pas de reason phrase (`HTTP/1.1 200` au lieu de `HTTP/1.1 200 OK`).
- `loadFile()` concatène un root et un chemin sans normalisation sécurisée.
- L'encodage de la 404 contient un caractère corrompu.
- `ROOT_FOLDER` a une valeur Windows absolue codée en dur.

#### `NetworkCurl`

Le wrapper libcurl offre `Get` et `Post` via un singleton.

Points forts :

- API très simple.
- Mutex autour du handle curl partagé.
- Récupération du body en mémoire.

Points faibles :

- Le singleton limite les tests parallèles et la configuration par client.
- `curl_global_cleanup()` n'est pas appelé.
- Les options curl persistent entre appels, par exemple `CURLOPT_POST` peut rester actif après un POST si un GET suit.
- L'URL et le payload sont passés avec `string_view::data()`, ce qui suppose une terminaison nulle pour l'URL.

#### HTTP/2 expérimental

Le module HTTP/2 détecte le preface et manipule quelques frames, mais ne décode pas HPACK et ne branche pas les frames HEADERS/DATA sur le routeur HTTP.

Conclusion : il doit rester explicitement expérimental ou être désactivé par défaut tant qu'il ne produit pas de vraies réponses HTTP/2 applicatives.

## Bugs prioritaires

### P0 - Parsing réseau non borné et requêtes incomplètes

Références : `src/HttpServer.cpp:264-299`, `src/SocketConnection.cpp:29-47`, `src/HttpRequest.cpp:79-145`.

`SocketConnection::receive()` retourne après un seul `recv()`. `_getHttpRequest()` parse dès que `data` n'est plus vide, sans attendre `\r\n\r\n` ni le nombre d'octets annoncé par `Content-Length`. En plus, `request.parse(data.data())` construit un `std::string_view` depuis un pointeur C sans longueur explicite.

Impact :

- Requête fragmentée TCP traitée comme invalide.
- Body POST potentiellement tronqué.
- Lecture au-delà du buffer si aucun `\0` n'est présent rapidement.
- Comportement instable selon la taille et le découpage réseau.

Recommandation :

- Introduire un `HttpRequestReader` qui accumule dans un buffer par connexion.
- Parser les headers seulement après `\r\n\r\n`.
- Lire exactement `Content-Length` octets pour les requêtes avec body.
- Appeler `HttpRequest::parse(std::string_view(data.data(), data.size()))`.
- Ajouter des tests socket bruts qui envoient une requête en plusieurs morceaux.

### P0 - Traversal possible dans le service de fichiers

Références : `src/HttpResponse.cpp:84-98`, `src/main.cpp:121-131`, `examples/file_server/main.cpp:24-45`.

`loadFile()` ouvre `ROOT_FOLDER + '/' + filename`. Les handlers lui transmettent directement `request.url.path` ou un chemin construit avec le path utilisateur. Aucun `weakly_canonical`, aucune vérification que le chemin final reste sous le dossier public, et aucun rejet de `..`.

Impact :

- Un client peut tenter d'accéder à des fichiers hors du répertoire prévu.
- Le comportement dépend du `cwd` et de `SERVER_ROOT`.

Recommandation :

- Remplacer `loadFile(string)` par une API de fichier statique qui reçoit `baseDir` et `requestPath`.
- Normaliser le chemin final via `std::filesystem::weakly_canonical`.
- Vérifier que le chemin final commence par le dossier canonique autorisé.
- Ajouter des tests pour `/../`, `%2e%2e`, double slash et fichiers inexistants.

### P0 - Durée de vie dangereuse des routes

Référence : `src/HttpRoute.hpp:18`.

`HttpRoute::route` est un `std::string_view`. Les exemples utilisent des littéraux, ce qui fonctionne. Mais l'API publique accepte aussi des routes construites dynamiquement. Dans ce cas, le serveur peut stocker une vue pendante après `addRoute()`.

Impact :

- Crashs ou matching erratique si une route provient d'une `std::string` temporaire.
- Bug difficile à diagnostiquer, car le code compile et les tests actuels ne le couvrent pas.

Recommandation :

- Remplacer `std::string_view route` par `std::string route`.
- Garder `string_view` uniquement pour les paramètres d'entrée temporaires.
- Ajouter un test avec une route construite dynamiquement puis détruite.

### P1 - 405 Method Not Allowed masqué en 404

Références : `src/HttpRoute.hpp:110-114`, `src/HttpServer.cpp:146-149`.

Quand une route matche le chemin mais pas la méthode, `HttpRoute::operator()` met `response.code = 405` puis retourne `false`. Le serveur interprète ensuite `routeFound == false` comme une absence de route et remplace la réponse par 404.

Impact :

- Les clients reçoivent 404 au lieu de 405.
- Les tests ne vérifient pas les méthodes interdites.

Recommandation :

- Faire retourner un résultat structuré : `NoMatch`, `Matched`, `MethodNotAllowed`, `HandlerError`.
- Ajouter l'en-tête `Allow`.
- Tester `GET` sur une route `POST` only.

### P1 - `RouteRegistry` global et non thread-safe

Références : `src/RouteRegistry.hpp:11-27`, `src/HttpServer.cpp:189-250`.

Le registre de routes est un singleton global. Deux instances de `HttpServer` dans le même process partagent donc les routes enregistrées. Les tests compensent avec `clearRoutes()`, mais cela rend les tests et serveurs concurrents fragiles.

Impact :

- Isolation impossible entre deux serveurs.
- Risque de data race si routes ajoutées pendant que le serveur tourne.
- API difficile à maintenir.

Recommandation :

- Déplacer le registre dans `HttpServer`.
- Faire retourner `Result<void, RouteError>` depuis `addRoute()`.
- Refuser les mutations de routes après `start()` ou protéger avec une phase de construction explicite.

### P1 - Injection répétée de `/docs`

Références : `src/HttpServer.cpp:21-37`.

`enableAutoDocs()` ne fait que poser un booléen, et `start()` pousse une route `docs` à chaque appel. Si `start()` est appelé plus d'une fois sur la même instance, la route est dupliquée. Elle contourne aussi `addRoute()`, donc elle ne passe pas par le registre.

Impact :

- État serveur non idempotent.
- Documentation potentiellement dupliquée.

Recommandation :

- Enregistrer `/docs` dans `enableAutoDocs()` ou dans une phase `finalizeRoutes()`.
- Utiliser le même chemin de validation que `addRoute()`.
- Rendre l'opération idempotente.

### P1 - Arrêt serveur non maîtrisé

Références : `src/NetworkSocket.cpp:116-153`, `src/NetworkSocket.cpp:170-211`.

`NetworkSocket::start()` entre dans une boucle infinie et ne consulte pas de flag d'arrêt. `stop()` existe, mais la boucle d'acceptation ne l'observe pas. Cela complique les tests et force les tests actuels à tuer un sous-processus.

Impact :

- Tests lents et fragiles.
- Intégration applicative difficile.
- Pas de shutdown gracieux.

Recommandation :

- Ajouter un `std::stop_source` ou un atomic `m_running`.
- Fermer la socket d'écoute pour réveiller `epoll_wait`.
- Fournir `startBlocking()`, `startAsync()` et `stop()`.
- Réécrire les tests d'intégration sans `fork/kill` quand possible.

### P1 - Build/test non reproductible

Références : `xmake.lua:10-16`, `xmake.lua:28-80`.

La cible statique `test_curl` compile `src/*.cpp`, donc aussi `src/main.cpp`. Les tests dépendent à la fois de `test_curl` et de `http_server`. Les tests sont exécutés automatiquement en `after_build`, ce qui mélange compilation et exécution. Dans l'environnement de revue, `xmake build route_duplicate_tests` échoue avec `fatal error: gtest/gtest.h: No such file or directory`.

Impact :

- La bibliothèque embarque un `main`.
- Les tests ne sont pas reproductibles sans préparation manuelle.
- Les builds CI risquent d'être instables.

Recommandation :

- Exclure `src/main.cpp` de la bibliothèque.
- Définir une liste explicite de sources de bibliothèque.
- Supprimer les `after_build` et créer une commande `xmake test`.
- Verrouiller ou documenter l'installation des packages `xmake`.
- Ajouter CI Linux avec build Debug, Release et tests.

### P2 - `NetworkCurl` garde des options entre requêtes

Références : `src/NetworkCurl.cpp:159-240`.

Le même handle curl est réutilisé. `Post()` active `CURLOPT_POST`, mais `Get()` ne remet pas explicitement la méthode à GET. D'autres options peuvent aussi persister.

Impact :

- Séquence POST puis GET potentiellement incorrecte.
- Tests parallèles limités malgré le mutex.

Recommandation :

- Appeler `curl_easy_reset()` au début de chaque requête, puis réappliquer les options communes.
- Ou créer un handle par requête/client.
- Remplacer le singleton par une classe instanciable.

### P2 - Logs directs et erreurs non structurées

Références : nombreuses occurrences de `std::cout` et `std::cerr` dans `src/HttpServer.cpp`, `src/NetworkSocket.cpp`, `src/Http2.cpp`, `src/main.cpp`.

Les modules bibliothèque écrivent directement sur stdout/stderr.

Impact :

- Tests bruyants.
- Impossible de contrôler le niveau de log.
- Intégration difficile dans une application.

Recommandation :

- Introduire une interface de logger ou des callbacks.
- Laisser les exemples décider de la sortie console.
- Remonter les erreurs via `Result` plutôt que logs dispersés.

## Analyse de maintenabilité

### Couplage

Le couplage principal vient de trois endroits :

- `HttpServer` hérite de `NetworkSocket`, ce qui mélange protocole HTTP et transport TCP.
- `RouteRegistry` est global.
- `HttpResponse::loadFile()` dépend d'un root global initialisé par variable d'environnement.

Une séparation plus maintenable serait :

- `TcpServer` : acceptation, pool, lifecycle.
- `HttpConnection` : lecture complète d'une ou plusieurs requêtes.
- `HttpParser` : parsing pur et testable.
- `Router` : matching, méthodes, params.
- `StaticFileHandler` : fichiers et sécurité.
- `HttpServer` : façade qui compose ces blocs.

### Contrats d'API

Plusieurs fonctions publiques ne signalent pas l'échec :

- `HttpServer::addRoute()` écrit un warning mais ne retourne rien.
- `enableAutoDocs()` ne dit pas si `/docs` est déjà pris.
- `HttpResponse::loadFile()` retourne seulement `bool`, sans raison.

Recommandation : utiliser `Result<T, Error>` de manière plus systématique pour les API publiques.

### Tests

Les tests actuels valident que le serveur répond à `/ping`, que quelques charges passent, et que les doublons de route sont refusés. Ils ne valident pas assez les invariants de bas niveau.

Tests prioritaires à ajouter :

| Priorité | Test | Pourquoi |
| --- | --- | --- |
| P0 | Parser une requête complète avec body multi-ligne | Évite les régressions POST. |
| P0 | Envoyer une requête TCP fragmentée | Reproduit le comportement réel du réseau. |
| P0 | Static file traversal | Sécurité. |
| P1 | Méthode non autorisée retourne 405 + `Allow` | Correction HTTP. |
| P1 | Route construite depuis `std::string` dynamique | Protège contre `string_view` pendant. |
| P1 | Deux serveurs dans le même process | Valide l'isolation du registre. |
| P2 | Séquence curl POST puis GET | Vérifie le reset des options curl. |
| P2 | HEAD ne renvoie pas de body | Conformité HTTP. |

## Features prioritaires

### 1. Mode serveur contrôlable

Ajouter une API de lifecycle :

```cpp
HttpServer server;
server.port(8080);
auto run = server.startAsync();
server.stop();
```

Valeur : tests plus fiables, intégration dans une application, shutdown propre.

### 2. Static file handler sécurisé

Ajouter un helper officiel :

```cpp
server.serveStatic("/public", "public");
```

Fonctionnalités minimales :

- canonicalisation du chemin ;
- rejet traversal ;
- index configurable ;
- content-type fiable ;
- support `HEAD` ;
- headers `Cache-Control` configurables.

### 3. Router avec résultat structuré

Remplacer le `bool` de `HttpRoute::operator()` par un type de résultat.

Valeur :

- distinguer `404`, `405`, erreur handler ;
- générer l'en-tête `Allow` ;
- faciliter les tests.

### 4. Parser HTTP isolé

Créer un parser pur, sans socket :

```cpp
Result<HttpRequest, HttpParseError> parseHttpRequest(std::string_view bytes);
```

Valeur :

- tests unitaires rapides ;
- comportement documenté ;
- moins de bugs dépendants du réseau.

### 5. Packaging bibliothèque/exemples

Séparer :

- `test_curl_core` ou `http_core` : bibliothèque sans `main`.
- `http_server_demo` : `src/main.cpp`.
- `example_*` : exemples.
- `*_tests` : tests.

Valeur : build plus clair, adoption plus facile.

## Workflow recommandé

### Branching

Utiliser un workflow simple :

- branche principale `main` toujours verte ;
- branches courtes `feature/...`, `fix/...`, `chore/...` ;
- pull request obligatoire pour fusion ;
- squash merge ou merge commit, mais choisir une convention unique.

### CI minimale

Ajouter GitHub Actions ou équivalent avec :

1. Installation de Xmake.
2. Résolution des dépendances.
3. Build Debug.
4. Build Release.
5. Tests unitaires.
6. Tests d'intégration.
7. Optionnel : sanitizers.

Pipeline conseillé :

```text
format -> build-debug -> unit-tests -> integration-tests -> build-release
```

### Qualité C++

Mettre en place :

- `clang-format` avec un fichier `.clang-format` versionné ;
- `clang-tidy` sur les fichiers modifiés ou en job séparé ;
- AddressSanitizer et UndefinedBehaviorSanitizer en CI ;
- ThreadSanitizer ponctuellement pour le pool et les routes ;
- warnings en erreur seulement après stabilisation des dépendances et plateformes.

### Gestion des tests

Changer le modèle actuel :

- `xmake build` compile seulement ;
- `xmake test` exécute les tests ;
- tests unitaires sans serveur externe ;
- tests d'intégration avec ports aléatoires ou port `0` si supporté ;
- timeout strict pour chaque test réseau.

### Versioning et releases

Comme le projet ressemble à une bibliothèque, adopter :

- version sémantique `MAJOR.MINOR.PATCH` ;
- changelog court ;
- tags Git ;
- section README "Stabilité de l'API".

### Définition de Done

Pour une PR :

- build Debug et Release passent ;
- tests unitaires et intégration passent ;
- pas de nouveau warning ;
- doc ou exemple mis à jour si API modifiée ;
- test ajouté pour tout bug corrigé ;
- comportement réseau vérifié avec au moins un test bas niveau si la PR touche sockets/parser.

## Backlog recommandé

### Sprint 1 : stabilisation critique

1. Séparer `src/main.cpp` de la bibliothèque.
2. Remplacer `HttpRoute::route` par `std::string`.
3. Corriger `_getHttpRequest()` pour parser avec longueur explicite.
4. Ajouter un reader qui attend headers complets et body complet.
5. Sécuriser `loadFile()` ou introduire `StaticFileHandler`.
6. Ajouter tests unitaires parser + traversal.

### Sprint 2 : comportement HTTP

1. Introduire résultat de routing structuré.
2. Corriger 405 + `Allow`.
3. Ajouter support correct de `HEAD`.
4. Normaliser les headers en lecture case-insensitive.
5. Ajouter reason phrases ou un format de statut cohérent.

### Sprint 3 : workflow et CI

1. Retirer les `after_build`.
2. Ajouter `xmake test`.
3. Ajouter CI Linux.
4. Ajouter `.clang-format`.
5. Ajouter ASan/UBSan.
6. Documenter le workflow de contribution.

### Sprint 4 : architecture

1. Extraire `HttpParser`.
2. Extraire `Router`.
3. Remplacer `RouteRegistry` singleton par un membre de serveur/router.
4. Ajouter lifecycle `startAsync()/stop()`.
5. Encapsuler les logs.

## Risques si le projet passe en production sans changement

| Risque | Niveau | Cause |
| --- | --- | --- |
| Lecture hors buffer ou parsing instable | Élevé | `data.data()` sans taille explicite et requêtes incomplètes. |
| Fuite de fichiers | Élevé | Service statique sans normalisation. |
| Serveur impossible à arrêter proprement | Élevé | Boucle accept infinie. |
| Régressions non détectées | Élevé | Tests non reproductibles et couverture insuffisante. |
| Comportement HTTP incorrect | Moyen | 405 masqué, HEAD non spécial, headers case-sensitive. |
| Intégration difficile comme lib | Moyen | `main.cpp` inclus dans la lib et singletons globaux. |
| HTTP/2 trompeur | Moyen | Détection présente mais implémentation applicative incomplète. |

## Vérification effectuée

Commandes exécutées :

```bash
rg --files -g '!node_modules' -g '!vendor' -g '!dist' -g '!build'
git status --short
wc -l src/*.cpp src/*.hpp tests/*.cpp examples/*/*.cpp xmake.lua README.md
rg -n "TODO|FIXME|std::cout|std::cerr|catch \\(\\.\\.\\.\\)|string_view|loadFile|RouteRegistry|enableAutoDocs|after_build|add_files\\(\\\"src/\\*\\.cpp\\\"" src tests examples xmake.lua README.md
xmake build route_duplicate_tests
```

Résultat notable :

- `xmake build route_duplicate_tests` échoue avec `fatal error: gtest/gtest.h: No such file or directory`.
- Le dépôt était déjà sale avant la revue : `.vscode/compile_commands.json` modifié et `.vs/` non suivi.

## Recommandation finale

Le projet a une bonne base pédagogique et un design d'API prometteur, mais la prochaine étape doit être une stabilisation ciblée du cœur HTTP avant toute extension. Le meilleur investissement est de rendre le parsing, le routing et le service statique testables en isolation, puis de mettre une CI simple qui empêche les régressions. Une fois ces fondations propres, les fonctionnalités comme la documentation enrichie, le support statique avancé ou HTTP/2 pourront être ajoutées avec beaucoup moins de risque.
