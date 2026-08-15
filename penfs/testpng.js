const fs = require('fs'), zlib = require('zlib');
const t = new Uint32Array(256);
for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[i] = c >>> 0; }
function crc32(buf) { let c = 0xFFFFFFFF; for (let i = 0; i < buf.length; i++) c = t[(c ^ buf[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
const w = 64, h = 32;
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
ihdr[8] = 8; ihdr[9] = 2; // RGB
const stride = w * 3;
const raw = Buffer.alloc((stride + 1) * h);
for (let y = 0; y < h; y++) { raw[y*(stride+1)] = 0; for (let x = 0; x < w; x++) {
  const d = y*(stride+1)+1+x*3; raw[d]=255; raw[d+1]=0; raw[d+2]=0; } }
const png = Buffer.concat([Buffer.from([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a]),
  chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
fs.writeFileSync('test-red.png', png);
console.log('ok');
