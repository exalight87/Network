import assert from 'node:assert/strict';
import path from 'node:path';
import test from 'node:test';
import {
  caddyFile, deploymentVersion, parseArgs, personalSiteConfigPath, quoteRemote,
  sshTarget, validatePersonalSiteConfig, validateSiteConfig
} from './demo-lib.mjs';

test('parseArgs accepts the documented commands', () => {
  for (const command of ['help', 'doctor', 'setup', 'build', 'deploy']) assert.equal(parseArgs([command]), command);
  assert.throws(() => parseArgs(['deploy', '--force']), /n'accepte aucun argument/);
  assert.throws(() => parseArgs(['destroy']), /Commande inconnue/);
});

test('quoteRemote protects single quotes', () => {
  assert.equal(quoteRemote("a'b"), "'a'\\''b'");
});

test('site configuration is bounded to the expected static build', () => {
  assert.equal(validateSiteConfig({ name: 'http-server', publish: 'dist', routing: 'static' }).name, 'http-server');
  assert.throws(() => validateSiteConfig({ name: '../bad', publish: 'dist' }), /invalide/);
  assert.throws(() => validateSiteConfig({ name: 'ok', publish: '../dist' }), /dist/);
});

test('personal site configuration rejects unknown keys', () => {
  const config = { host: 'example.test', user: 'debian', domain: 'example.test', webUser: 'debian' };
  assert.equal(sshTarget(validatePersonalSiteConfig(config)), 'debian@example.test');
  assert.throws(() => validatePersonalSiteConfig({ ...config, password: 'secret' }), /inconnue/);
});

test('paths and deployment identifiers are deterministic', () => {
  assert.equal(personalSiteConfigPath({}, '/home/test'), path.join('/home/test', '.config', 'personal-site', 'config.json'));
  assert.equal(caddyFile({ domain: 'example.test' }, { name: 'demo' }), '/etc/caddy/sites.d/demo.example.test.caddy');
  assert.equal(deploymentVersion(new Date('2026-07-16T12:34:56Z')), '20260716123456');
});
