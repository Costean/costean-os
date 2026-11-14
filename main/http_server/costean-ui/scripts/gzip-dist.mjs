// Firmware's file server always serves "<path>.gz" with Content-Encoding: gzip, so ship only .gz files.
import fs from 'node:fs'; import path from 'node:path'; import zlib from 'node:zlib';
const root = path.resolve('dist/axe-os'); let total = 0;
const walk = d => fs.readdirSync(d, { withFileTypes: true }).forEach(e => {
  const p = path.join(d, e.name);
  if (e.isDirectory()) return walk(p);
  if (p.endsWith('.gz')) return;
  const gz = zlib.gzipSync(fs.readFileSync(p), { level: 9 }); gz[9] = 3;
  fs.writeFileSync(p + '.gz', gz); if (!/\.(json|svg)$/.test(p)) fs.unlinkSync(p); else total += fs.statSync(p).size; total += gz.length;
  console.log(`${path.relative(root, p)}.gz ${gz.length} B`);
});
walk(root); console.log(`total gz ${total} B (www partition 3145728 B)`);
