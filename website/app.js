const scenarios = {
  ping: {
    method: 'GET', url: '/api-demo/ping', headers: '', body: '',
    code: `server.addRoute({\n  .route = "ping",\n  .callable = [](const HttpRequest&, HttpResponse& response) {\n    response.code = 200;\n    response.body = R"({"status":"ok","message":"pong"})";\n    return true;\n  }\n});`
  },
  user: {
    method: 'GET', url: '/api-demo/users/42?details=true', headers: '', body: '',
    code: `server.addRoute({\n  .route = "users/{id}",\n  .callable = [](const HttpRequest& request, HttpResponse& response) {\n    const auto id = request.pathParams.at("id");\n    const auto details = request.url.queryParams.at("details");\n    // Construire la réponse...\n    return true;\n  }\n});`
  },
  echo: {
    method: 'POST', url: '/api-demo/echo', headers: 'Content-Type: application/json', body: '{\n  "framework": "test-curl",\n  "language": "C++23"\n}',
    code: `server.addRoute({\n  .route = "echo",\n  .allowedMethods = {HttpRequest::POST},\n  .callable = [](const HttpRequest& request, HttpResponse& response) {\n    response.body = request.body;\n    response.code = 200;\n    return true;\n  }\n});`
  },
  error: {
    method: 'GET', url: '/api-demo/errors/422', headers: '', body: '',
    code: `server.addRoute({\n  .route = "errors/{code}",\n  .callable = [](const HttpRequest&, HttpResponse& response) {\n    response.code = 422;\n    response.body = R"({"error":"validation failure"})";\n    return true;\n  }\n});`
  },
  inspect: {
    method: 'GET', url: '/api-demo/inspect', headers: 'Accept: application/json\nX-Demo-User: Léa', body: '',
    code: `server.addRoute({\n  .route = "inspect",\n  .callable = [](const HttpRequest& request, HttpResponse& response) {\n    const auto user = headerValue(request, "X-Demo-User");\n    response.headers["Vary"] = "Accept, X-Demo-User";\n    // Renvoyer les headers inspectés...\n    return true;\n  }\n});`
  },
  patch: {
    method: 'PATCH', url: '/api-demo/users/42', headers: 'Content-Type: application/json', body: '{\n  "role": "lead engineer",\n  "active": true\n}',
    code: `server.addRoute({\n  .route = "users/{id}",\n  .allowedMethods = {HttpRequest::GET, HttpRequest::PATCH},\n  .callable = [](const HttpRequest& request, HttpResponse& response) {\n    response.code = 200;\n    response.headers["X-Resource-Version"] = "2";\n    // Appliquer request.body...\n    return true;\n  }\n});`
  },
  job: {
    method: 'POST', url: '/api-demo/jobs', headers: 'Content-Type: application/json', body: '{\n  "type": "generate-report",\n  "format": "pdf"\n}',
    code: `server.addRoute({\n  .route = "jobs",\n  .allowedMethods = {HttpRequest::POST},\n  .callable = [](const HttpRequest&, HttpResponse& response) {\n    response.code = 201;\n    response.headers["Location"] = "/jobs/job-demo-001";\n    response.headers["X-Request-Id"] = "demo-request-001";\n    return true;\n  }\n});`
  }
};

const $ = (selector) => document.querySelector(selector);
const urlInput = $('#request-url');
const bodyInput = $('#request-body');
const headersInput = $('#request-headers');
let active = scenarios.ping;

function selectScenario(name) {
  active = scenarios[name];
  document.querySelectorAll('.scenario').forEach((button) => button.classList.toggle('active', button.dataset.scenario === name));
  $('#method-badge').textContent = active.method;
  urlInput.value = active.url;
  headersInput.value = active.headers;
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
    const headers = { Accept: 'application/json' };
    for (const line of headersInput.value.split('\n')) {
      if (!line.trim()) continue;
      const separator = line.indexOf(':');
      if (separator < 1) throw new Error(`Header invalide : ${line}`);
      headers[line.slice(0, separator).trim()] = line.slice(separator + 1).trim();
    }
    const options = { method: active.method, signal: controller.signal, headers };
    if (active.method !== 'GET') {
      options.body = bodyInput.value;
      if (!options.headers['Content-Type']) options.headers['Content-Type'] = 'application/json';
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
    const message = error.name === 'AbortError' ? 'La requête a dépassé 6 secondes.' : error.message.startsWith('Header invalide') ? error.message : 'Le serveur de démonstration est indisponible.';
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
