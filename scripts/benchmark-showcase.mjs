#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const output = process.env.BENCHMARK_OUTPUT || path.join(root, 'website', 'data', 'benchmarks.json');
const binary = process.env.SHOWCASE_BINARY || path.join(root, 'build', 'linux', 'x86_64', 'release', 'http_server');

function startServer(port) {
  if (process.env.SHOWCASE_LAUNCHER) {
    return spawn(process.env.SHOWCASE_LAUNCHER, ['--exec', binary, String(port)], { cwd: root, stdio: 'ignore' });
  }
  return spawn(binary, [String(port)], { cwd: root, stdio: 'ignore' });
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
  for (let attempt = 0; attempt < 60; attempt += 1) {
    if (child.exitCode !== null) throw new Error(`Le serveur a quitté avec le code ${child.exitCode}.`);
    try {
      if ((await fetch(`${baseUrl}/ping`)).ok) return;
    } catch { /* Le serveur démarre encore. */ }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error('Le serveur de benchmark ne répond pas.');
}

function percentile(sortedValues, ratio) {
  const index = Math.ceil(ratio * sortedValues.length) - 1;
  return sortedValues[Math.max(0, index)];
}

async function runProfile(baseUrl, profile) {
  const latencies = [];
  let cursor = 0;
  let errors = 0;
  const started = performance.now();

  async function worker() {
    while (cursor < profile.requests) {
      cursor += 1;
      const requestStarted = performance.now();
      try {
        const response = await fetch(`${baseUrl}${profile.path}`, profile.options);
        const body = await response.text();
        if (!response.ok || !profile.validate(body)) errors += 1;
      } catch { errors += 1; }
      latencies.push(performance.now() - requestStarted);
    }
  }

  await Promise.all(Array.from({ length: profile.concurrency }, worker));
  const wallMs = performance.now() - started;
  latencies.sort((left, right) => left - right);
  return {
    id: profile.id,
    label: profile.label,
    method: profile.options?.method || 'GET',
    path: profile.path,
    concurrency: profile.concurrency,
    requests: profile.requests,
    rps: Number((profile.requests * 1000 / wallMs).toFixed(1)),
    p50Ms: Number(percentile(latencies, 0.50).toFixed(2)),
    p95Ms: Number(percentile(latencies, 0.95).toFixed(2)),
    p99Ms: Number(percentile(latencies, 0.99).toFixed(2)),
    errors
  };
}

const port = await availablePort();
const baseUrl = `http://127.0.0.1:${port}`;
const child = startServer(port);

try {
  await waitUntilReady(baseUrl, child);
  for (let index = 0; index < 100; index += 1) await fetch(`${baseUrl}/ping`);

  const jsonOptions = { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"benchmark":true}' };
  const profiles = [
    { id: 'ping-1', label: 'Ping séquentiel', path: '/ping', concurrency: 1, requests: 300, validate: (body) => body.includes('"pong"') },
    { id: 'ping-25', label: 'Ping concurrent', path: '/ping', concurrency: 25, requests: 1000, validate: (body) => body.includes('"pong"') },
    { id: 'echo-25', label: 'JSON concurrent', path: '/echo', options: jsonOptions, concurrency: 25, requests: 1000, validate: (body) => body.includes('"bytes"') }
  ];
  const results = [];
  for (const profile of profiles) results.push(await runProfile(baseUrl, profile));

  const report = {
    schemaVersion: 1,
    generatedAt: new Date().toISOString(),
    commit: (process.env.GITHUB_SHA || process.env.BENCHMARK_COMMIT || 'local').slice(0, 7),
    runUrl: process.env.GITHUB_SERVER_URL && process.env.GITHUB_REPOSITORY && process.env.GITHUB_RUN_ID
      ? `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}` : null,
    environment: {
      runner: process.env.RUNNER_NAME || os.hostname(),
      os: `${os.type()} ${os.release()}`,
      cpu: os.cpus()[0]?.model || 'inconnu',
      cores: os.cpus().length,
      node: process.version,
      build: 'release'
    },
    results
  };
  await mkdir(path.dirname(output), { recursive: true });
  await writeFile(output, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`Benchmark écrit dans ${output}`);
  for (const result of results) console.log(`${result.label}: ${result.rps} req/s · p95 ${result.p95Ms} ms · ${result.errors} erreur(s)`);
  if (results.some((result) => result.errors > 0)) process.exitCode = 1;
} finally {
  child.kill();
}
