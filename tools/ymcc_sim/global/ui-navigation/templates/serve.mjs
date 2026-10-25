import fs from 'node:fs';
import path from 'node:path';
import {createServer} from 'node:http';
import {randomInt} from 'node:crypto';

const task = path.resolve(import.meta.dirname);
const site = path.join(task, 'site');
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store');
  const pathname = new URL(request.url, 'http://127.0.0.1').pathname;
  const name = pathname === '/' ? 'index.html' : pathname === '/product' ? 'product.html' : pathname.slice(1);
  const asset=['gyro-simulation','performance-simulation','settings-preview'].some(folder=>name.startsWith(folder+'/')&&path.resolve(site,name).startsWith(path.resolve(site,folder)+path.sep)&&fs.existsSync(path.resolve(site,name)));
  if (!asset&&!['index.html', 'product.html', 'product.js', 'product.css'].includes(name)) {
    response.writeHead(404);
    response.end();
    return;
  }
  response.setHeader('Content-Type', name.endsWith('.js') ? 'text/javascript; charset=utf-8' : name.endsWith('.css') ? 'text/css; charset=utf-8' : name.endsWith('.mp4')?'video/mp4':name.endsWith('.glb')?'model/gltf-binary':name.endsWith('.webp')?'image/webp':name.endsWith('.json')?'application/json':'text/html; charset=utf-8');
  response.end(fs.readFileSync(path.join(site, name)));
});

// A port selected by listen(0) can be blocked by the browser. Reuse the
// lighting probe's high dynamic range without weakening browser security.
let listenAttempts = 0;
for (; listenAttempts < 32; listenAttempts++) {
  try {
    await new Promise((resolve, reject) => {
      const failed = error => { server.removeListener('listening', ready); reject(error); };
      const ready = () => { server.removeListener('error', failed); resolve(); };
      server.once('error', failed);
      server.once('listening', ready);
      server.listen(randomInt(49152, 65536), '127.0.0.1');
    });
    break;
  } catch (error) {
    if (error.code !== 'EADDRINUSE' && error.code !== 'EACCES') throw error;
  }
}
if (!server.listening) throw new Error('Could not reserve a browser-compatible loopback port after 32 attempts');
const port = server.address().port;
const url = 'http://127.0.0.1:' + port + '/';
fs.writeFileSync(path.join(task, 'server.json'), JSON.stringify({pid: process.pid, port, url, listenAttempts: listenAttempts + 1, started: new Date().toISOString()}, null, 2));
console.log(url);