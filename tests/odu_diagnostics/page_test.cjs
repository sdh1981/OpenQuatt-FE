const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../../components/openquatt_odu_diagnostics/page.h'), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = Object.fromEntries(['hp', 'scan', 'notice', 'identity', 'status'].map(id => [id, {}]));
elements.hp.options = [{}, {}]; elements.hp.value = '1';
let state = {state: 'ready', read_only: true, duo: false, csrf: 'local-token', serial_hex: '00'.repeat(40),
  registers: {version: {status: 'not_read'}, pcb: {status: 'not_read'}, serial: {status: 'not_read'}}};
const posts = []; let refresh; let failed = false;
vm.runInNewContext(script, {
  document: {getElementById: id => elements[id]},
  setInterval: fn => { refresh = fn; },
  fetch: async (url, request) => {
    if (request?.method === 'POST') posts.push({url, ...request});
    return {ok: !failed, status: failed ? 401 : 200, json: async () => state};
  },
});
const settle = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  await settle();
  assert.equal(posts.length, 0); assert.equal(elements.scan.disabled, false);
  assert.equal(elements.hp.options[1].disabled, true);
  await elements.scan.onclick();
  assert.equal(posts.length, 1); assert.equal(posts[0].url, '/openquatt/odu-diagnostics/scan?hp=1');
  assert.equal(posts[0].headers['X-OQ-CSRF'], 'local-token'); assert.equal(posts[0].body, undefined);
  state = {...state, state: 'reading', duo: true}; refresh(); await settle();
  assert.equal(elements.scan.disabled, true); assert.equal(elements.hp.disabled, true);
  assert.equal(elements.identity.textContent, '');
  const text = '<script>unsafe</script>';
  state = {...state, state: 'complete', serial_hex: Buffer.from(text.padEnd(40, '\0')).toString('hex'),
    registers: {version: {status: 'ok', value: 0x0201}, pcb: {status: 'ok', value: 0x1037}, serial: {status: 'ok'}}};
  refresh(); await settle();
  assert(elements.identity.textContent.includes('Firmware: 2.01'));
  assert(elements.identity.textContent.includes('Printcode: 0x1037'));
  assert(elements.identity.textContent.includes(text)); // textContent, never HTML interpolation.
  assert.equal(elements.identity.innerHTML, undefined);
  assert.equal(posts.length, 1); // Polls/completion cannot trigger a second scan or resume.
  state.serial_hex = '01'.repeat(40); refresh(); await settle();
  assert(elements.identity.textContent.includes('niet leesbaar'));
  state.registers.serial.status = 'exception'; refresh(); await settle();
  assert(!elements.identity.textContent.includes('Serienummer:'));
  failed = true; refresh(); await settle();
  assert.equal(elements.scan.disabled, true); assert(elements.notice.textContent.includes('401'));
  console.log('PASS diagnosis page: manual scan only, CSRF, Single/Duo, busy, safe identity display, errors');
})().catch(error => { console.error(error); process.exitCode = 1; });
