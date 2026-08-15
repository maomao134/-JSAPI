const fs = require('fs');
const bmp = fs.readFileSync('penfs-shot.bmp');
const w = bmp.readInt32LE(18), h = bmp.readInt32LE(22);
const rowSize = Math.ceil(w * 3 / 4) * 4;
function px(x, y) { const o = 54 + (h - 1 - y) * rowSize + x * 3; return [bmp[o+2], bmp[o+1], bmp[o]]; }
// 1) overall histogram (quantized)
const hist = {};
for (let y = 0; y < h; y += 4) for (let x = 0; x < w; x += 4) {
  const [r,g,b] = px(x,y);
  const k = (r>>4<<8)|(g>>4<<4)|(b>>4);
  hist[k] = (hist[k]||0)+1;
}
const top = Object.entries(hist).sort((a,b)=>b[1]-a[1]).slice(0,12);
console.log('w=%d h=%d top colors:', w, h);
for (const [k,c] of top) console.log('  #%s%s%s x%d', (k>>8&15).toString(16).repeat(1), (k>>4&15).toString(16).repeat(1), (k&15).toString(16).repeat(1), c);
// 2) region averages
function avg(x0,y0,x1,y1){let r=0,g=0,b=0,n=0;for(let y=y0;y<y1;y+=3)for(let x=x0;x<x1;x+=3){const p=px(x,y);r+=p[0];g+=p[1];b+=p[2];n++;}return [Math.round(r/n),Math.round(g/n),Math.round(b/n),n];}
console.log('left panel avg  :', avg(0,0,330,240));
console.log('right area avg  :', avg(330,0,1020,240));
console.log('status row avg  :', avg(340,6,1020,34));
// 3) yellow-ish pixels (status text #f5c542) bbox
let nY=0,minX=1e9,maxX=-1,minY=1e9,maxY=-1;
for(let y=0;y<h;y++)for(let x=0;x<w;x++){const [r,g,b]=px(x,y);
  if(r>190&&g>150&&g<230&&b<120){nY++;if(x<minX)minX=x;if(x>maxX)maxX=x;if(y<minY)minY=y;if(y>maxY)maxY=y;}}
console.log('yellow px:', nY, nY?`bbox x[${minX}-${maxX}] y[${minY}-${maxY}]`:'(none)');
// 4) light text pixels in left list area
let nT=0;
for(let y=44;y<240;y++)for(let x=0;x<330;x++){const [r,g,b]=px(x,y);
  if(r>170&&g>180&&b>200){nT++;}}
console.log('light text px in left list:', nT);
