# C++ HTTP Kernel & Curl Integration

Bibliotheque C++23 separee en deux couches :

- `http_kernel` : serveur HTTP, routes, parsing, reponses, fichiers statiques et documentation automatique.
- `http_curl` : integration optionnelle libcurl pour les requetes HTTP clientes.

## Fonctionnalites

- Serveur HTTP base sur sockets TCP
- Boucle d'acceptation avec `epoll` sous Linux
- Pool de connexions pour gerer plusieurs clients
- Routes imbriquees avec handlers C++
- Filtrage par methode HTTP : `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD`, `OPTIONS`
- Parametres de chemin : `/api/users/{id}/info`
- Parsing des query params, headers et body
- Reponses texte, HTML ou fichiers statiques
- Documentation automatique disponible sur `/docs`
- Wrapper libcurl pour requetes HTTP `GET` et `POST`
- Tests avec GoogleTest
- Support HTTP/2 bas niveau experimental

## Prerequis

- Compilateur C++23
- [Xmake](https://xmake.io/)
- libcurl, uniquement pour `http_curl`, `example_curl_client` et les tests d'integration
- GoogleTest, uniquement pour les tests gtest

Sous Linux, le serveur utilise aussi `pthread` et `epoll`.

## Build

```bash
xmake
```

Par defaut, les dependances externes xmake sont desactivees afin que le kernel et les exemples serveur restent buildables seuls.

Activer GoogleTest :

```bash
xmake f --with_gtest=y
```

Activer GoogleTest et libcurl :

```bash
xmake f --with_gtest=y --with_curl=y
```

Build d'un target precis :

```bash
xmake build http_kernel
xmake build http_server
xmake build example_simple_server
xmake build example_api_server
xmake build example_curl_client
xmake build route_duplicate_tests
```

Modes debug/release :

```bash
xmake f -m debug
xmake
```

```bash
xmake f -m release
xmake
```

## Lancer le serveur principal

```bash
xmake run http_server
```

Port personnalise :

```bash
xmake run http_server 8080
```

Endpoints exposes par l'exemple principal :

```text
GET  /
GET  /ping
POST /your-post-endpoint
GET  /api/1
GET  /api/2
GET  /foo/bar/toto
GET  /resources/...
GET  /docs
```

Tester rapidement :

```bash
curl http://localhost:9090/ping
curl http://localhost:9090/docs
```

## Démo web interactive

Le dépôt contient une CLI locale qui produit le site statique dans `dist/` et déploie le serveur d'exemple dans un
conteneur Docker durci. Elle utilise la CLI `site` installée sur la machine sans modifier son dépôt.

Prérequis locaux : Node.js 18+, Xmake, OpenSSH, `tar`, la CLI `site` et une configuration créée avec `site config`.

```bash
npm run demo -- doctor
npm run demo -- setup    # une seule fois : sous-domaine, Docker et Caddy
npm run demo -- build
npm run demo -- deploy
```

Le frontend et l'API partagent la même origine : la page appelle `/api-demo/*`, que Caddy transmet au conteneur
écoutant uniquement sur `127.0.0.1:9090`. Les routes de démonstration sont :

```text
GET  /api-demo/ping
GET  /api-demo/users/{id}?details=true
POST /api-demo/echo
GET  /api-demo/errors/{code}
GET  /api-demo/docs
```

## Exemple minimal

```cpp
#include <test_curl/kernel/HttpServer.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <iostream>

int main()
{
    HttpServer server;
    server.port(8080);

    server.addRoute({
        .route = "",
        .callable = [](const HttpRequest&, HttpResponse& response) -> bool {
            response.body = "Hello, World!";
            response.code = 200;
            return true;
        }
    });

    auto result = server.start();
    if (!result) {
        std::cerr << result.GetError().GetFormatedError();
        return 1;
    }

    return 0;
}
```

## Routes avec methodes et parametres

```cpp
server.addRoute({
    .route = "api/users/{id}/info",
    .allowedMethods = { HttpRequest::GET },
    .callable = [](const HttpRequest& request, HttpResponse& response) -> bool {
        const auto id = request.pathParams.at("id");

        response.headers["Content-Type"] = "application/json";
        response.body = "{\"id\": " + id + "}";
        response.code = 200;
        return true;
    },
    .description = "Retourne les informations d'un utilisateur"
});
```

## Documentation automatique

Activez la documentation avec :

```cpp
server.enableAutoDocs();
```

Une route `/docs` est alors ajoutee au demarrage du serveur. Elle liste les routes declarees, leurs methodes HTTP et leurs descriptions quand elles sont renseignees.

## Exemples fournis

| Target | Description |
| --- | --- |
| `example_simple_server` | Serveur minimal "Hello, World!" |
| `example_file_server` | Serveur de fichiers statiques depuis `public/` |
| `example_api_server` | API JSON avec routes imbriquees et parametres |
| `example_curl_client` | Client GET minimal utilisant l'integration libcurl |

Lancer un exemple :

```bash
xmake build example_api_server
xmake run example_api_server
```

Puis tester :

```bash
curl http://localhost:8080/api/users
curl http://localhost:8080/api/users/1/info
curl http://localhost:8080/docs
```

## Wrapper curl

```cpp
#include <test_curl/integrations/curl/NetworkCurl.hpp>
#include <format>
#include <iostream>

int main()
{
    auto& curl = NetworkCurl::GetInstance();

    NetworkResponse response = curl.Get("http://example.com");
    std::cout << std::format("{}\n", response);

    return 0;
}
```

## Tests

```bash
xmake build core_unit_tests
xmake build route_duplicate_tests
xmake build server_tests
xmake build performance_tests
xmake build robustness_tests
```

Ou lancer toute la suite activee par la configuration courante :

```bash
xmake test -j1
```

`-j1` force l'execution sequentielle des binaires de tests d'integration, qui ouvrent des ports localhost.

Ou lancer un test compile :

```bash
xmake run route_duplicate_tests
```

## Structure du projet

```text
include/test_curl/kernel/              API publique du kernel HTTP
include/test_curl/integrations/curl/   API publique de l'integration curl
src/kernel/                            Implementation du kernel HTTP
src/integrations/curl/                 Implementation libcurl
tests/support/                         Helpers de tests partages
tests/                                 Tests unitaires et integration
examples/                              Exemples d'utilisation
public/                 Fichiers servis par l'exemple file_server
website/                Page servie par le serveur principal
resources/              Ressources statiques
xmake.lua               Configuration de build
```

## Notes

- Les routes dupliquees avec la meme methode HTTP sont refusees par le registre de routes.
- Les routes sans `allowedMethods` sont traitees comme routes generiques.
- Le support HTTP/2 existe au niveau detection/preface/frames, mais doit etre considere comme experimental.
- Le serveur principal est un exemple de demonstration ; pour une application reelle, creez plutot un target dedie inspire de `examples/api_server`.

## Licence

A preciser.
