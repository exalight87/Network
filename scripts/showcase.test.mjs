import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function executablePath() {
  if (process.env.SHOWCASE_BINARY) return process.env.SHOWCASE_BINARY;
  const platform = process.platform === 'darwin' ? 'macosx' : process.platform;
  const architecture = process.arch === 'x64' ? 'x86_64' : process.arch;
  const filename = process.platform === 'win32' ? 'http_server.exe' : 'http_server';
  const candidate = path.join(root, 'build', platform, architecture, 'release', filename);
  assert.ok(existsSync(candidate), `Binaire absent : ${candidate}. Lancez d'abord xmake -y http_server.`);
  return candidate;
}

function startServer(port) {
  const executable = executablePath();
  if (process.env.SHOWCASE_LAUNCHER) {
    return spawn(process.env.SHOWCASE_LAUNCHER, ['--exec', executable, String(port)], { cwd: root, stdio: 'ignore' });
  }
  return spawn(executable, [String(port)], { cwd: root, stdio: 'ignore' });
}

async function availablePort() {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once('error', reject);
    probe.listen(0, '127.0.0.1', () => {
      const { port } = probe.address();
      probe.close((error) => error ? reject(error) : resolve(port));
    });
  });
}

async function waitUntilReady(baseUrl, child) {
  for (let attempt = 0; attempt < 40; attempt += 1) {
    if (child.exitCode !== null) throw new Error(`Le serveur a quitté avec le code ${child.exitCode}.`);
    try {
      const response = await fetch(`${baseUrl}/ping`);
      if (response.ok) return;
    } catch { /* Le socket n'écoute pas encore. */ }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error('Le serveur de démonstration ne répond pas.');
}

test('the showcase exposes advanced HTTP user journeys', async (context) => {
  const port = await availablePort();
  const baseUrl = `http://127.0.0.1:${port}`;
  const child = startServer(port);
  context.after(() => child.kill());
  await waitUntilReady(baseUrl, child);

  const inspected = await fetch(`${baseUrl}/inspect`, { headers: { 'X-Demo-User': 'Lea' } });
  assert.equal(inspected.status, 200);
  assert.equal(inspected.headers.get('vary'), 'Accept, X-Demo-User');
  assert.equal((await inspected.json()).demoUser, 'Lea');

  const patched = await fetch(`${baseUrl}/users/42`, {
    method: 'PATCH', headers: { 'Content-Type': 'application/json' }, body: '{"role":"lead"}'
  });
  assert.equal(patched.status, 200);
  assert.equal(patched.headers.get('x-resource-version'), '2');
  assert.equal((await patched.json()).updated, true);

  const created = await fetch(`${baseUrl}/jobs`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"type":"report"}'
  });
  assert.equal(created.status, 201);
  assert.equal(created.headers.get('location'), '/jobs/job-demo-001');
  assert.equal((await created.json()).status, 'queued');

  const rejected = await fetch(`${baseUrl}/jobs`);
  assert.equal(rejected.status, 405);
  assert.equal(rejected.headers.get('allow'), 'POST');
});
