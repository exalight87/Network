const scenarios = {
  ping: {
    method: 'GET', url: '/api-demo/ping', body: '',
    code: `server.addRoute({\n  .route = "ping",\n  .callable = [](const HttpRequest&, HttpResponse& response) {\n    response.code = 200;\n    response.body = R"({"status":"ok","message":"pong"})";\n    return true;\n  }\n});`
  },
  user: {
    method: 'GET', url: '/api-demo/users/42?details=true', body: '',
    code: `server.addRoute({\n  .route = "users/{id}",\n  .callable = [](const HttpRequest& request, HttpResponse& response) {\n    const auto id = request.pathParams.at("id");\n    const auto details = request.url.queryParams.at("details");\n    // Construire la réponse...\n    return true;\n  }\n});`
  },
  echo: {
    method: 'POST', url: '/api-demo/echo', body: '{\n  "framework": "test-curl",\n  "language": "C++23"\n}',
    code: `server.addRoute({\n  .route = "echo",\n  .allowedMethods = {HttpRequest::POST},\n  .callable = [](const HttpRequest& request, HttpResponse& response) {\n    response.body = request.body;\n    response.code = 200;\n    return true;\n  }\n});`
  },
  error: {
    method: 'GET', url: '/api-demo/errors/422', body: '',
    code: `server.addRoute({\n  .route = "errors/{code}",\n  .callable = [](const HttpRequest&, HttpResponse& response) {\n    response.code = 422;\n    response.body = R"({"error":"validation failure"})";\n    return true;\n  }\n});`
  }
};

const $ = (selector) => document.querySelector(selector);
const urlInput = $('#request-url');
const bodyInput = $('#request-body');
let active = scenarios.ping;

function selectScenario(name) {
  active = scenarios[name];
  document.querySelectorAll('.scenario').forEach((button) => button.classList.toggle('active', button.dataset.scenario === name));
  $('#method-badge').textContent = active.method;
  urlInput.value = active.url;
  bodyInput.value = active.body;
  bodyInput.disabled = active.method === 'GET';
  $('#route-code').textContent = active.code;
}

async function sendRequest() {
  const button = $('#send-request');
  const state = $('#api-state');
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 6000);
  button.disabled = true;
  state.textContent = 'Requête en cours…';
  state.className = 'state loading';
  const started = performance.now();
  try {
    const options = { method: active.method, signal: controller.signal, headers: { Accept: 'application/json' } };
    if (active.method !== 'GET') {
      options.body = bodyInput.value;
      options.headers['Content-Type'] = 'application/json';
    }
    const response = await fetch(urlInput.value, options);
    const raw = await response.text();
    let formatted = raw;
    try { formatted = JSON.stringify(JSON.parse(raw), null, 2); } catch { /* Preserve non-JSON responses. */ }
    $('#status-code').textContent = response.status;
    $('#status-text').textContent = response.statusText || (response.ok ? 'Succès' : 'Erreur HTTP');
    $('#duration').textContent = `${Math.round(performance.now() - started)} ms`;
    $('#response-meta').textContent = response.ok ? 'Succès' : 'Erreur HTTP';
    $('#response-headers').textContent = [...response.headers].map(([key, value]) => `${key}: ${value}`).join('\n') || '—';
    $('#response-body').textContent = formatted || '(réponse vide)';
    state.textContent = response.ok ? 'API disponible' : `API joignable · HTTP ${response.status}`;
    state.className = response.ok ? 'state' : 'state error';
  } catch (error) {
    const message = error.name === 'AbortError' ? 'La requête a dépassé 6 secondes.' : 'Le serveur de démonstration est indisponible.';
    $('#status-code').textContent = '—';
    $('#status-text').textContent = 'Erreur réseau';
    $('#duration').textContent = `${Math.round(performance.now() - started)} ms`;
    $('#response-meta').textContent = 'Échec';
    $('#response-headers').textContent = '—';
    $('#response-body').textContent = message;
    state.textContent = 'API indisponible';
    state.className = 'state error';
  } finally {
    clearTimeout(timeout);
    button.disabled = false;
  }
}

document.querySelectorAll('.scenario').forEach((button) => button.addEventListener('click', () => selectScenario(button.dataset.scenario)));
$('#send-request').addEventListener('click', sendRequest);
document.addEventListener('keydown', (event) => { if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') sendRequest(); });
selectScenario('ping');
