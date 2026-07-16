import { access, readFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

const PROJECT_PATTERN = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
const CONFIG_KEYS = new Set(['host', 'user', 'remoteDir', 'domain', 'webUser', 'webGroup', 'dashboardLocalPort', 'dashboardRemotePort']);

export function parseArgs(argv) {
  const [command = 'help', ...rest] = argv;
  if (!new Set(['help', 'doctor', 'setup', 'build', 'deploy']).has(command)) throw new Error(`Commande inconnue: ${command}`);
  if (rest.length) throw new Error(`La commande ${command} n'accepte aucun argument.`);
  return command;
}

export function quoteRemote(value) {
  return `'${String(value).replaceAll("'", `'\\''`)}'`;
}

export function validateSiteConfig(config) {
  if (!config || typeof config !== 'object' || Array.isArray(config)) throw new Error('site.json invalide.');
  if (!PROJECT_PATTERN.test(config.name) || config.name.includes('--')) throw new Error('Nom de site invalide.');
  if (config.publish !== 'dist') throw new Error('site.json doit publier le dossier dist.');
  return config;
}

export function validatePersonalSiteConfig(config) {
  if (!config || typeof config !== 'object' || Array.isArray(config)) throw new Error('Configuration site invalide.');
  for (const key of Object.keys(config)) if (!CONFIG_KEYS.has(key)) throw new Error(`Clé de configuration site inconnue: ${key}`);
  for (const key of ['host', 'user', 'domain', 'webUser']) {
    if (typeof config[key] !== 'string' || !config[key].trim()) throw new Error(`Configuration site invalide: ${key}.`);
  }
  return config;
}

export function personalSiteConfigPath(environment = process.env, home = os.homedir()) {
  return path.join(environment.XDG_CONFIG_HOME || path.join(home, '.config'), 'personal-site', 'config.json');
}

export async function loadJson(filePath, validator) {
  let contents;
  try { contents = await readFile(filePath, 'utf8'); }
  catch (error) {
    if (error.code === 'ENOENT') throw new Error(`Fichier absent: ${filePath}`);
    throw error;
  }
  try { return validator(JSON.parse(contents)); }
  catch (error) {
    if (error instanceof SyntaxError) throw new Error(`JSON invalide dans ${filePath}: ${error.message}`);
    throw error;
  }
}

export async function exists(filePath) {
  try { await access(filePath); return true; } catch { return false; }
}

export function sshTarget(config) {
  return `${config.user}@${config.host}`;
}

export function caddyFile(config, site) {
  return `/etc/caddy/sites.d/${site.name}.${config.domain}.caddy`;
}

export function deploymentVersion(now = new Date()) {
  return now.toISOString().replaceAll(/[-:.TZ]/g, '').slice(0, 14);
}
