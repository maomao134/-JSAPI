const fs = require('fs'), zlib = require('zlib');
const buf = fs.readFileSync('penfs-shot.png');
let off = 8; const chunks = [];
while (off < buf.length) {
  const len = buf.readUInt32BE(off);
  const type = buf.toString('ascii', off + 4, off + 8);
  chunks.push({ type, data: buf.slice(off + 8, off + 8 + len) });
  off += 12 + len;
}
const ihdr = chunks[0].data;
const w = ihdr.readUInt32BE(0), h = ihdr.readUInt32BE(4), depth = ihdr[8], ctype = ihdr[9];
console.log('IHDR', w, h, depth, ctype, 'chunks:', chunks.map(c=>c.type).join(','));
const idat = Buffer.concat(chunks.filter(c => c.type === 'IDAT').map(c => c.data));
const raw = zlib.inflateSync(idat);
const bpp = ctype === 6 ? 4 : ctype === 2 ? 3 : 1;
const stride = w * bpp;
const px = Buffer.alloc(h * stride);
let p = 0;
for (let y = 0; y < h; y++) {
  const f = raw[p++];
  const row = raw.slice(p, p + stride); p += stride;
  for (let x = 0; x < stride; x++) {
    const a = x >= bpp ? px[(y-1)*stride + x] : 0;
    const b = x >= bpp ? row[x - bpp] : 0;
    const c = x >= bpp ? px[(y-1)*stride + x - bpp] : 0;
    let v = row[x];
    if (f === 1) v = (v + a) & 0xff;
    else if (f === 2) v = (v + b) & 0xff;
    else if (f === 3) v = (v + ((a + b) >> 1)) & 0xff;
    else if (f === 4) { const pr = a + b - c, pa = Math.abs(pr-a), pb = Math.abs(pr-b), pc = Math.abs(pr-c); const pred = (pa<=pb&&pa<=pc)?a:(pb<=pc?b:c); v = (v + pred) & 0xff; }
    px[y*stride + x] = v;
  }
}
// BMP 24bpp bottom-up
const rowSize = Math.ceil(w * 3 / 4) * 4;
const bmp = Buffer.alloc(54 + rowSize * h);
bmp.write('BM', 0);
bmp.writeUInt32LE(54 + rowSize * h, 2);
bmp.writeUInt32LE(54, 10);
bmp.writeUInt32LE(40, 14);
bmp.writeInt32LE(w, 18); bmp.writeInt32LE(h, 22);
bmp.writeUInt16LE(1, 26); bmp.writeUInt16LE(24, 28);
bmp.writeUInt32LE(rowSize * h, 34);
for (let y = 0; y < h; y++) {
  const src = (h - 1 - y) * stride, dst = 54 + y * rowSize;
  for (let x = 0; x < w; x++) {
    bmp[dst + x*3] = px[src + x*bpp + 2];
    bmp[dst + x*3+1] = px[src + x*bpp + 1];
    bmp[dst + x*3+2] = px[src + x*bpp];
  }
}
fs.writeFileSync('penfs-shot.bmp', bmp);
console.log('wrote penfs-shot.bmp', bmp.length);
