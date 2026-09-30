// Offline projections of exported production vertices; no Unity, GPU, downloads or external assets.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
const source = resolve(process.argv[2] || 'build/sheep-model/geometry.json');
const output = resolve(process.argv[3] || 'build/sheep-model/preview.html');
const data = JSON.parse(readFileSync(source, 'utf8'));
const dot = (a,b) => a.reduce((s,x,i)=>s+x*b[i],0);
const sub = (a,b) => a.map((x,i)=>x-b[i]);
const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const unit = a => { const n=Math.hypot(...a); return a.map(x=>x/n); };
const light = unit([-0.6,1,0.9]);
const views = [['Front',[0,0,1]],['Side',[1,0,0]],['Three-quarter',[0.85,0.35,1]]];
function image(mesh, camera) {
  const toward = unit(camera), right=unit(cross([0,1,0],toward)), up=cross(toward,right);
  const points=mesh.vertices.map(v=>[dot(v,right),-dot(v,up),dot(v,toward)]);
  const xs=points.map(p=>p[0]),ys=points.map(p=>p[1]);
  const minX=Math.min(...xs),maxX=Math.max(...xs),minY=Math.min(...ys),maxY=Math.max(...ys);
  const scale=Math.min(290/(maxX-minX),260/(maxY-minY));
  const project=p=>`${(170+(p[0]-(minX+maxX)/2)*scale).toFixed(2)},${(151+(p[1]-(minY+maxY)/2)*scale).toFixed(2)}`;
  const faces=[];
  for(let i=0;i<mesh.triangles.length;i+=3){
    const ids=mesh.triangles.slice(i,i+3),v=ids.map(j=>mesh.vertices[j]);
    const normal=unit(cross(sub(v[1],v[0]),sub(v[2],v[0])));
    if(dot(normal,toward)<=0)continue;
    const shade=0.57+0.43*Math.max(0,dot(normal,light));
    const color=[0,1,2].map(c=>Math.round(ids.reduce((s,j)=>s+mesh.colors[j][c],0)/3*shade));
    const polygon=`<polygon points="${ids.map(j=>project(points[j])).join(' ')}" fill="rgb(${color})"/>`;
    faces.push({depth:ids.reduce((s,j)=>s+points[j][2],0)/3,polygon});
  }
  faces.sort((a,b)=>a.depth-b.depth);
  return `<svg viewBox="0 0 340 305" role="img"><rect width="340" height="305" fill="#e8edf0"/><ellipse cx="170" cy="289" rx="110" ry="9" fill="#c2ccd0"/>${faces.map(f=>f.polygon).join('')}</svg>`;
}
const cards=data.meshes.map(mesh=>`<section><h2>${mesh.kind} <small>${mesh.vertices.length} vertices · ${mesh.triangles.length/3} triangles</small></h2><div class="views">${views.map(([name,camera])=>`<figure>${image(mesh,camera)}<figcaption>${name}</figcaption></figure>`).join('')}</div></section>`).join('');
const html=`<!doctype html><html lang="zh"><meta charset="utf-8"><title>羊模型离线几何预览</title><style>body{font:16px system-ui;margin:28px auto;max-width:1100px;padding:0 20px;color:#253441;background:#f4f6f7}h1{margin-bottom:8px}p{line-height:1.6}section{background:white;padding:16px;margin:20px 0;border:1px solid #dce3e7;border-radius:8px}h2{margin:0 0 12px}small{font-size:14px;font-weight:400}.views{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}figure{margin:0}svg{width:100%;height:auto}figcaption{text-align:center;color:#566575;font-size:14px}code{font-size:11px;overflow-wrap:anywhere}</style><h1>圆润卡通羊 · 离线几何预览</h1><p>来自生产 SheepMesh.Build 导出的实际顶点、三角形和顶点色。未启动 Unity。此图使用简化面光照与三角形深度排序，<b>不是游戏截图</b>；不含额标、动画、URP 光照、阴影或雾，也不能替代最终视觉验收。各视图独立缩放以便看清细节。</p><code>Mesh SHA-256: ${data.meshSha256}</code>${cards}</html>`;
mkdirSync(dirname(output),{recursive:true});writeFileSync(output,html);
console.log(JSON.stringify({source,output,forms:data.meshes.length,views:views.length,mode:'offline-geometry-not-unity'}));
