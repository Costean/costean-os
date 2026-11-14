import fs from 'node:fs';
const v = JSON.parse(fs.readFileSync(new URL('../package.json', import.meta.url), 'utf8')).version;
const dir = new URL('../public/', import.meta.url);
fs.mkdirSync(dir, { recursive: true });
fs.writeFileSync(new URL('version.txt', dir), v + '\n');
console.log('version.txt', v);
