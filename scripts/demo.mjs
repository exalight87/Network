#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { cp, mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';
import {
  caddyFile, deploymentVersion, exists, loadJson, parseArgs, personalSiteConfigPath,
  quoteRemote, sshTarget, validatePersonalSiteConfig, validateSiteConfig
} from './demo-lib.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const HELP = `Usage: npm run demo -- <commande>\n\nCommandes:\n  doctor  Vérifier les prérequis locaux et distants\n  setup   Créer le site et préparer Docker/Caddy\n  build   Compiler le serveur et produire dist/\n  deploy  Construire et publier le site et le conteneur\n`;

function executable(program, args) {
  if (process.platform !== 'win32' || program !== 'site') return { program, args };
  // npm installs the CLI as site.cmd on Windows; cmd.exe is required to run that shim.
  return {
    program: process.env.ComSpec || 'cmd.exe',
    args: ['/d', '/s', '/c', ['site', ...args].join(' ')]
  };
}

function run(program, args, options = {}) {
  return new Promise((resolve, reject) => {
    const command = executable(program, args);
    const child = spawn(command.program, command.args, { shell: false, stdio: 'inherit', ...options });
    child.once('error', (error) => reject(new Error(`${program} introuvable: ${error.message}`)));
    child.once('exit', (code, signal) => code === 0 ? resolve() : reject(new Error(`${program} a échoué${signal ? ` (${signal})` : ` (code ${code})`}.`)));
  });
}

async function commandExists(program, args = ['--version']) {
  return new Promise((resolve) => {
    const command = executable(program, args);
    const child = spawn(command.program, command.args, { shell: false, stdio: 'ignore' });
    child.once('error', () => resolve(false));
    child.once('spawn', () => { resolve(true); child.kill(); });
  });
}

async function context() {
  const site = await loadJson(path.join(ROOT, 'site.json'), validateSiteConfig);
  const configPath = personalSiteConfigPath();
  let config;
  try { config = await loadJson(configPath, validatePersonalSiteConfig); }
  catch (error) { throw new Error(`${error.message}\nLancez d'abord: site config`); }
  return { site, config, target: sshTarget(config), fqdn: `${site.name}.${config.domain}` };
}

async function build() {
  console.log('==> Compilation release du serveur de démonstration');
  await run('xmake', ['f', '-m', 'release', '-y'], { cwd: ROOT });
  await run('xmake', ['-y', 'http_server'], { cwd: ROOT });
  const destination = path.join(ROOT, 'dist');
  await rm(destination, { recursive: true, force: true });
  await cp(path.join(ROOT, 'website'), destination, { recursive: true });
  if (!(await exists(path.join(destination, 'index.html')))) throw new Error('Le build frontend ne contient pas dist/index.html.');
  console.log(`==> Frontend produit dans ${destination}`);
}

async function doctor() {
  let failed = false;
  for (const program of ['node', 'xmake', 'site', 'ssh', 'scp', 'tar']) {
    const available = await commandExists(program, program === 'site' ? ['help'] : ['--version']);
    console.log(`${available ? '✓' : '✗'} ${program}`);
    failed ||= !available;
  }
  try {
    const { target, fqdn } = await context();
    await run('ssh', ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', target,
      `printf 'SSH ok\\n'; if command -v docker >/dev/null; then sudo docker --version; else printf 'Docker sera installé par demo setup\\n'; fi; if test -f ${quoteRemote(`/etc/caddy/sites.d/${fqdn}.caddy`)}; then printf 'Caddy: configuré\\n'; else printf 'Caddy: setup requis\\n'; fi`]);
  } catch (error) { failed = true; console.error(`✗ ${error.message}`); }
  if (failed) throw new Error('Le diagnostic a détecté un ou plusieurs problèmes.');
}

async function uploadAndRun(localFile, remoteFile, target, args) {
  await run('scp', [localFile, `${target}:${remoteFile}`]);
  try {
    await run('ssh', ['-t', target, `sudo bash ${quoteRemote(remoteFile)} ${args.map(quoteRemote).join(' ')}`]);
  } finally {
    await run('ssh', [target, `rm -f ${quoteRemote(remoteFile)}`], { stdio: 'ignore' }).catch(() => {});
  }
}

async function setup() {
  const { site, config, target, fqdn } = await context();
  const remoteCaddy = caddyFile(config, site);
  const check = `test -f ${quoteRemote(remoteCaddy)}`;
  try { await run('ssh', [target, check], { stdio: 'ignore' }); }
  catch {
    console.log(`==> Création de ${fqdn} avec la CLI site`);
    await run('site', ['create'], { cwd: ROOT });
  }
  console.log('==> Installation de Docker et configuration de Caddy');
  await uploadAndRun(path.join(ROOT, 'scripts', 'remote-setup.sh'), '/tmp/http-demo-setup.sh', target,
    [fqdn, `/opt/sites/${fqdn}`, config.webUser, '9090']);
  console.log(`==> Infrastructure prête: https://${fqdn}`);
}

async function deploy() {
  await build();
  const { config, target, fqdn } = await context();
  console.log('==> Publication du frontend avec la CLI site');
  await run('site', ['deploy'], { cwd: ROOT });

  const temporary = await mkdtemp(path.join(os.tmpdir(), 'http-demo-'));
  const archive = path.join(temporary, 'context.tar.gz');
  const remoteArchive = `/tmp/http-demo-${process.pid}.tar.gz`;
  const remoteContext = `/tmp/http-demo-${process.pid}`;
  const remoteScript = `/tmp/http-demo-deploy-${process.pid}.sh`;
  try {
    console.log('==> Préparation du contexte Docker');
    await run('tar', [
      '-czf', archive,
      '-C', ROOT,
      'Dockerfile',
      '.dockerignore',
      'xmake.lua',
      'include',
      'src',
      'examples/showcase_server'
    ]);
    await run('scp', [archive, `${target}:${remoteArchive}`]);
    await run('scp', [path.join(ROOT, 'scripts', 'remote-deploy.sh'), `${target}:${remoteScript}`]);
    await run('ssh', [target, `rm -rf ${quoteRemote(remoteContext)} && mkdir -p ${quoteRemote(remoteContext)} && tar -xzf ${quoteRemote(remoteArchive)} -C ${quoteRemote(remoteContext)}`]);
    console.log('==> Construction et bascule du conteneur');
    await run('ssh', ['-t', target,
      `sudo bash ${quoteRemote(remoteScript)} ${quoteRemote(remoteContext)} ${quoteRemote(deploymentVersion())} '9090'`]);
  } finally {
    await run('ssh', [target, `rm -rf ${quoteRemote(remoteContext)} ${quoteRemote(remoteArchive)} ${quoteRemote(remoteScript)}`], { stdio: 'ignore' }).catch(() => {});
    await rm(temporary, { recursive: true, force: true });
  }
  console.log(`==> Démo publiée: https://${fqdn}`);
}

async function main() {
  const command = parseArgs(process.argv.slice(2));
  if (command === 'help') return console.log(HELP);
  if (command === 'doctor') return doctor();
  if (command === 'setup') return setup();
  if (command === 'build') return build();
  if (command === 'deploy') return deploy();
}

main().catch((error) => { console.error(`Erreur: ${error.message}`); process.exitCode = 1; });
