const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../components/openquatt_odu_firmware/page.h'), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = Object.fromEntries(['file', 'upload', 'hp', 'consent', 'start', 'progress', 'notice', 'status'].map(id => [id, {}]));
elements.hp.options = [{}, {}];
elements.hp.value = '1';
elements.consent.checked = false;
let state = {writes_enabled: false, locked: false, image: 'IMAGE_VERIFIED', acknowledged: 0, duo: false, csrf: 'offline-test-token', can_upload: true, busy: false, resume_pending: false};
const posts = [];
let refresh;
vm.runInNewContext(script, {
  document: {getElementById: id => elements[id]},
  setInterval: fn => { refresh = fn; },
  fetch: async (url, request) => {
    if (request?.method === 'POST') posts.push({url, ...request});
    return {ok: true, status: 200, json: async () => state};
  },
});
const settle = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  await settle();
  assert.equal(elements.start.disabled, true);
  assert.equal(elements.hp.options[1].disabled, true);
  assert.equal(posts.length, 0);
  elements.consent.checked = true;
  elements.consent.onchange();
  assert.equal(elements.start.disabled, true); // Consent cannot bypass offline configuration.
  state = {...state, writes_enabled: true, duo: true};
  refresh(); await settle();
  assert.equal(elements.start.disabled, false);
  elements.file.files = [{size: 1}];
  await elements.upload.onclick();
  assert.equal(posts.length, 0);
  const file = {size: 116736};
  elements.file.files = [file];
  await elements.upload.onclick();
  assert.equal(posts.length, 1);
  assert.equal(posts[0].url, '/openquatt/odu-firmware/image');
  assert.equal(posts[0].body, file);
  assert.equal(posts[0].headers['X-OQ-CSRF'], 'offline-test-token');
  assert.equal(posts[0].headers['Content-Type'], 'application/octet-stream');
  state = {...state, locked: true, can_upload: false, resume_pending: true, hp: 2, auto_resume: true};
  refresh(); await settle();
  assert.equal(elements.start.disabled, true);
  assert.equal(elements.upload.disabled, true);
  assert.equal(elements.hp.disabled, true);
  assert.equal(elements.hp.value, '2');
  state = {...state, image: 'CACHE_INVALID', can_upload: true};
  refresh(); await settle();
  assert.equal(elements.start.disabled, true);
  assert.equal(elements.upload.disabled, false); // Only the matching cache can be repaired; no new start.
  assert.equal(elements.hp.disabled, true);
  assert.equal(posts.length, 1); // Polling never generates start/resume requests.
  console.log('PASS local OpenQuatt page: offline gate, consent, cache repair, pinned HP, CSRF, no auto-start');
})().catch(error => { console.error(error); process.exitCode = 1; });
