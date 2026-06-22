import { readFileSync } from 'fs';
const src = readFileSync('J:/romajitxted/index.html', 'utf8');
console.log('File size:', src.length, 'bytes');

const checks = {
  modelSelect: src.includes('id="modelSelect"'),
  fetchModels: src.includes('async function fetchModels'),
  rebuildSelect: src.includes('function rebuildModelSelect'),
  getEffective: src.includes('function getEffectiveModel'),
  ollamaBtn: src.includes('id="fetchOllamaBtn"'),
  fetchModelsBtn: src.includes('id="fetchModelsBtn"'),
  currentModelList: src.includes('let currentModelList'),
};
console.log(checks);

const scriptStart = src.indexOf('<script>');
const scriptEnd = src.lastIndexOf('</script>');
const js = src.slice(scriptStart + 8, scriptEnd);

// 簡易：未閉じ括弧チェック
let depth = 0;
for (const ch of js) {
  if (ch === '{') depth++;
  if (ch === '}') depth--;
}
console.log('Brace depth (should be 0):', depth);
