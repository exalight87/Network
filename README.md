# C++ HTTP Server & Curl Wrapper

Bibliotheque C++23 fournissant un serveur HTTP leger, un systeme de routes hierarchiques, un support de fichiers statiques, une generation simple de pages HTML, une documentation automatique des endpoints, ainsi qu'un wrapper minimal autour de libcurl.

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
- libcurl
- GoogleTest

Sous Linux, le serveur utilise aussi `pthread` et `epoll`.

## Build

```bash
xmake
```

Build d'un target precis :

```bash
xmake build http_server
xmake build example_simple_server
xmake build example_api_server
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

## Exemple minimal

```cpp
#include <HttpServer.hpp>
#include <HttpRoute.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
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
#include <NetworkCurl.hpp>
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
xmake build server_tests
xmake build performance_tests
xmake build robustness_tests
xmake build route_duplicate_tests
```

Ou lancer un test compile :

```bash
xmake run route_duplicate_tests
```

## Structure du projet

```text
src/                    Code source principal
tests/                  Tests GoogleTest
examples/               Exemples d'utilisation
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
