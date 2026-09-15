const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const source = fs.readFileSync('web/i18n.js', 'utf8');
const html = fs.readFileSync('web/index.html', 'utf8');
const picker = {};
const document = {
  documentElement: {},
  title: '',
  querySelectorAll: () => [],
  getElementById: () => picker,
  dispatchEvent: () => {},
};
const window = {};
vm.runInNewContext(source, {
  window,
  document,
  navigator: {language: 'en'},
  CustomEvent: function CustomEvent(name) { this.type = name; },
});

const translations = window.DTE_TRANSLATIONS;
const expected = Object.keys(translations.en).sort();
for (const [locale, table] of Object.entries(translations))
  assert.deepStrictEqual(Object.keys(table).sort(), expected, `${locale} keys differ`);
for (const key of [...html.matchAll(/data-i18n(?:-aria)?="([^"]+)"/g)].map(m => m[1]))
  assert(expected.includes(key), `missing dictionary key: ${key}`);
assert.deepStrictEqual(Object.keys(translations).sort(), ['en', 'ja', 'zh-CN']);
console.log(`i18n ok: ${expected.length} keys × ${Object.keys(translations).length} locales`);
