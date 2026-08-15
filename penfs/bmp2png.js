const fs = require('fs'), zlib = require('zlib');
const bmp = fs.readFileSync('penfs-shot.bmp');
const w = bmp.readInt32LE(18), h = bmp.readInt32LE(22);
const rowSize = Math.ceil(w * 3 / 4) * 4;
// PNG with RGBA, filter 0 rows, no sBIT
const t = new Uint32Array(256);
for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[i] = c >>> 0; }
function crc32(buf) { let c = 0xFFFFFFFF; for (let i = 0; i < buf.length; i++) c = t[(c ^ buf[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
const stride = w * 4;
const raw = Buffer.alloc((stride + 1) * h);
for (let y = 0; y < h; y++) {
  const src = 54 + (h - 1 - y) * rowSize, dst = y * (stride + 1);
  raw[dst] = 0;
  for (let x = 0; x < w; x++) {
    raw[dst + 1 + x*4] = bmp[src + x*3 + 2];
    raw[dst + 1 + x*4 + 1] = bmp[src + x*3 + 1];
    raw[dst + 1 + x*4 + 2] = bmp[src + x*3];
    raw[dst + 1 + x*4 + 3] = 255;
  }
}
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk('IHDR', ihdr),
  chunk('IDAT', zlib.deflateSync(raw, { level: 6 })),
  chunk('IEND', Buffer.alloc(0))
]);
fs.writeFileSync('penfs-shot-v.png', png);
console.log('wrote penfs-shot-v.png', png.length);
