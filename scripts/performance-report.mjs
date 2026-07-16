#!/usr/bin/env node
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';

const [, , inputPath, outputPath] = process.argv;
if (!inputPath || !outputPath) {
  throw new Error('Usage: node scripts/performance-report.mjs <performance.log> <benchmarks.json>');
}

const selectedScenarios = {
  baseline_sequential_keep_alive: {
    label: 'Keep-alive séquentiel', method: 'GET', path: '/ping', concurrency: 1
  },
  concurrent_keep_alive_32_threads_50_requests_per_thread: {
    label: '32 clients concurrents', method: 'GET', path: '/ping', concurrency: 32
  },
  sustained_keep_alive_8_threads_1000_ms: {
    label: 'Charge soutenue · 1 s', method: 'GET', path: '/ping', concurrency: 8
  }
};

function parseMetrics(line) {
  const separator = line.indexOf(' | ');
  if (!line.startsWith('[perf] ') || separator < 0) return null;
  const name = line.slice(7, separator);
  if (!selectedScenarios[name]) return null;
  const values = {};
  for (const token of line.slice(separator + 3).trim().split(/\s+/)) {
    const [key, rawValue] = token.split('=');
    if (key && rawValue !== undefined) values[key] = Number(rawValue);
  }
  const required = ['requests', 'failure', 'rps', 'p50_ms', 'p95_ms', 'p99_ms'];
  if (required.some((key) => !Number.isFinite(values[key]))) {
    throw new Error(`Métriques incomplètes pour ${name}.`);
  }
  return {
    id: name,
    ...selectedScenarios[name],
    requests: values.requests,
    rps: values.rps,
    p50Ms: values.p50_ms,
    p95Ms: values.p95_ms,
    p99Ms: values.p99_ms,
    errors: values.failure
  };
}

const log = await readFile(inputPath, 'utf8');
const parsed = log.split(/\r?\n/).map(parseMetrics).filter(Boolean);
const resultsById = new Map(parsed.map((result) => [result.id, result]));
const missing = Object.keys(selectedScenarios).filter((id) => !resultsById.has(id));
if (missing.length > 0) throw new Error(`Scénarios absents du rapport : ${missing.join(', ')}`);

const report = {
  schemaVersion: 1,
  source: 'native-cpp-performance-tests',
  generatedAt: new Date().toISOString(),
  commit: (process.env.GITHUB_SHA || process.env.BENCHMARK_COMMIT || 'local').slice(0, 7),
  runUrl: process.env.GITHUB_SERVER_URL && process.env.GITHUB_REPOSITORY && process.env.GITHUB_RUN_ID
    ? `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}` : null,
  environment: {
    runner: process.env.RUNNER_NAME || os.hostname(),
    os: `${os.type()} ${os.release()}`,
    cpu: os.cpus()[0]?.model || 'inconnu',
    cores: os.cpus().length,
    build: 'release · C++23 · libcurl'
  },
  results: Object.keys(selectedScenarios).map((id) => resultsById.get(id))
};

await mkdir(path.dirname(outputPath), { recursive: true });
await writeFile(outputPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
console.log(`Rapport natif C++ écrit dans ${outputPath}`);
