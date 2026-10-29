import * as THREE from './vendor/three.module.js';

const stage = document.querySelector('#device-viewport');
const status = document.querySelector('#model-status');
const canvas = document.querySelector('#device-canvas');
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
const finePointer = matchMedia('(hover: hover) and (pointer: fine)');

function showFallback() {
  stage.classList.remove('loaded');
  status.textContent = 'Static illustration · 3D unavailable';
}

try {
  const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'low-power' });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setClearColor(0x000000, 0);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.45;
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(32, 1, 0.1, 100);
  camera.position.set(0, 1.1, 26.8);
  camera.lookAt(0, 1.2, 0);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x747063, 3));
  const key = new THREE.DirectionalLight(0xffffff, 5.5);
  key.position.set(-6, 9, 10); scene.add(key);
  const rim = new THREE.DirectionalLight(0xfff6d4, 4);
  rim.position.set(6, 4, -4); scene.add(rim);
  const fill = new THREE.DirectionalLight(0xffffff, 1.3);
  fill.position.set(4, -2, 8); scene.add(fill);

  const model = new THREE.Group();
  const device = new THREE.Group();
  device.position.y = -1.8;
  model.add(device); scene.add(model);
  const shell = new THREE.MeshStandardMaterial({ color: 0x252724, roughness: .42, metalness: .17 });
  const face = new THREE.MeshStandardMaterial({ color: 0x1d201d, roughness: .48, metalness: .12 });
  const rubber = new THREE.MeshStandardMaterial({ color: 0x131512, roughness: .7, metalness: .08 });
  const edge = new THREE.MeshStandardMaterial({ color: 0x373a34, roughness: .36, metalness: .3 });
  const steel = new THREE.MeshStandardMaterial({ color: 0x8a8b80, roughness: .26, metalness: .85 });
  const slot = new THREE.MeshStandardMaterial({ color: 0x090b08, roughness: .6 });
  const mesh = (geometry, material, x=0, y=0, z=0) => {
    const result = new THREE.Mesh(geometry, material);
    result.position.set(x,y,z); device.add(result); return result;
  };
  function roundedBox(w,h,d,r,material,x=0,y=0,z=0) {
    const shape = new THREE.Shape();
    const left = -w/2, bottom = -h/2;
    shape.moveTo(left+r,bottom);
    shape.lineTo(left+w-r,bottom);shape.quadraticCurveTo(left+w,bottom,left+w,bottom+r);
    shape.lineTo(left+w,bottom+h-r);shape.quadraticCurveTo(left+w,bottom+h,left+w-r,bottom+h);
    shape.lineTo(left+r,bottom+h);shape.quadraticCurveTo(left,bottom+h,left,bottom+h-r);
    shape.lineTo(left,bottom+r);shape.quadraticCurveTo(left,bottom,left+r,bottom);
    const geo = new THREE.ExtrudeGeometry(shape,{ depth:d-.12,bevelEnabled:true,bevelSegments:3,steps:1,bevelSize:.06,bevelThickness:.06,curveSegments:8 });
    geo.translate(0,0,-d/2+.06);
    return mesh(geo,material,x,y,z);
  }
  // Approximate exterior proportions from Elecrow; this is not production CAD.
  roundedBox(5.16,8.2,2.63,.47,shell);
  roundedBox(5.08,8.08,.10,.45,edge,0,0,.95);
  roundedBox(4.98,7.95,.28,.43,face,0,.015,1.21);
  roundedBox(3.94,4.21,.18,.44,edge,0,.78,1.43);
  roundedBox(3.64,3.91,.13,.31,slot,0,.78,1.55);
  roundedBox(3.43,3.55,.07,.14,steel,0,.78,1.63);

  const display = document.createElement('canvas'); display.width=400;display.height=400;
  const ctx = display.getContext('2d');
  ctx.scale(2,2);
  ctx.fillStyle='#e5e7d9';ctx.fillRect(0,0,200,200);
  ctx.fillStyle='#20251b';ctx.textBaseline='top';ctx.font='bold 12px Arial';ctx.fillText('LORA TX',13,13);
  ctx.font='12px Arial';ctx.textAlign='right';ctx.fillText('▮▮▮▯',187,13);ctx.textAlign='left';
  ctx.fillRect(13,35,174,1.5);
  ctx.font='bold 29px Arial';ctx.fillText('Presenter',12,54);
  ctx.font='12px Arial';ctx.fillText('No action yet',13,104);
  ctx.font='10px Arial';ctx.fillText('No acknowledgement yet',13,125);
  ctx.fillRect(13,158,174,1);ctx.font='10px Arial';ctx.fillText('◀ Previous',13,171);ctx.textAlign='right';ctx.fillText('Next ▶',187,171);
  ctx.font='8px Arial';ctx.textAlign='center';ctx.fillText('Both: menu',100,187);
  const texture = new THREE.CanvasTexture(display);texture.colorSpace=THREE.SRGBColorSpace;
  const screen = mesh(new THREE.PlaneGeometry(3.30,3.39),new THREE.MeshBasicMaterial({ map:texture }),0,.78,1.68);
  screen.renderOrder=1;

  mesh(new THREE.CylinderGeometry(.48,.52,.48,48),steel,-1.31,4.2,0);
  mesh(new THREE.CylinderGeometry(.51,.58,.55,48),rubber,-1.31,4.64,0);
  mesh(new THREE.CylinderGeometry(.38,.50,.65,48),rubber,-1.31,5.23,0);
  mesh(new THREE.CylinderGeometry(.35,.38,3.51,48),rubber,-1.31,7.29,0);
  mesh(new THREE.CylinderGeometry(.35,.35,.12,48),edge,-1.31,8.66,0);
  mesh(new THREE.CylinderGeometry(.36,.35,.12,48),edge,-1.31,8.42,0);
  mesh(new THREE.CylinderGeometry(.31,.35,.11,48),rubber,-1.31,9.10,0);
  mesh(new THREE.CylinderGeometry(.62,.65,.20,48),steel,1.21,4.21,0);
  mesh(new THREE.CylinderGeometry(.61,.61,.82,64),rubber,1.21,4.73,0);
  mesh(new THREE.CylinderGeometry(.62,.62,.09,64),steel,1.21,5.18,0);
  for(let i=0;i<48;i++) {
    const angle=i/48*Math.PI*2;
    const ridge=mesh(new THREE.BoxGeometry(.035,.65,.045),edge,1.21+Math.sin(angle)*.609,4.76,Math.cos(angle)*.609);
    ridge.rotation.y=angle;
  }
  // Two physical side buttons, plus the unused GPS toggle above them.
  for (const y of [-.3,-1.7]) {
    const surround=mesh(new THREE.CylinderGeometry(.43,.43,.14,32),edge,2.62,y,.23);surround.rotation.z=Math.PI/2;
    const button=mesh(new THREE.CylinderGeometry(.31,.34,.17,32),rubber,2.73,y,.23);button.rotation.z=Math.PI/2;
  }
  const gps=roundedBox(.45,.81,.18,.1,slot,2.64,1.20,.23);gps.rotation.y=Math.PI/2;
  const toggle=roundedBox(.29,.29,.18,.05,edge,2.75,1.35,.23);toggle.rotation.y=Math.PI/2;
  for(const x of [-2.14,2.14]) for(const y of [-.1,1.02,2.13]) {
    roundedBox(.055,.39,.035,.022,edge,x,y,1.39);
    roundedBox(.055,.26,.035,.022,edge,x+(x<0?-.095:.095),y,1.39);
  }
  roundedBox(2.12,.25,.11,.12,edge,0,-2.57,1.44);
  for(let i=0;i<5;i++){
    const ledMat=new THREE.MeshStandardMaterial({color:i===0?0xd6c76e:0x32382b,roughness:.3,emissive:i===0?0x67501a:0x000000,emissiveIntensity:.2});
    const led=mesh(new THREE.SphereGeometry(.045,12,8),ledMat,-.76+i*.38,-2.57,1.52);led.scale.z=.5;
  }
  for(const [x,y] of [[-2.14,3.62],[2.14,3.62],[-2.14,-3.62],[2.14,-3.62]]) {
    const screw=mesh(new THREE.CylinderGeometry(.08,.08,.04,16),steel,x,y,1.39);screw.rotation.x=Math.PI/2;
    const cut=mesh(new THREE.BoxGeometry(.085,.018,.01),slot,x,y,1.417);cut.rotation.z=.5;
  }
  for(let i=0;i<7;i++)roundedBox(.25,.12,.08,.05,edge,-.85+i*.28,3.88,1.35);
  const port=roundedBox(.83,.25,.08,.10,slot,0,-4.12,.1);port.rotation.x=Math.PI/2;
  const label=document.createElement('canvas');label.width=512;label.height=90;
  const labelCtx=label.getContext('2d');labelCtx.font='bold 48px Arial';labelCtx.textAlign='center';labelCtx.fillStyle='#55584a';labelCtx.fillText('ThinkNode',256,61);
  const labelTex=new THREE.CanvasTexture(label);labelTex.colorSpace=THREE.SRGBColorSpace;
  mesh(new THREE.PlaneGeometry(2.7,.47),new THREE.MeshBasicMaterial({map:labelTex,transparent:true}),0,-3.39,1.38);

  const home={ x:.10,y:-.39,z:-.085 };
  const target={x:home.x,y:home.y};model.rotation.set(home.x,home.y,home.z);
  let frame=0;
  const canMove=()=>finePointer.matches&&!reducedMotion.matches;
  function updateStatus(){status.textContent=canMove()?'Move your pointer to explore':'3D model · fixed view';}
  function render(){renderer.render(scene,camera);}
  function animate(){
    frame=0;
    model.rotation.x+=(target.x-model.rotation.x)*.12;
    model.rotation.y+=(target.y-model.rotation.y)*.12;
    render();
    if(Math.abs(target.x-model.rotation.x)+Math.abs(target.y-model.rotation.y)>.0002)frame=requestAnimationFrame(animate);
  }
  function requestRender(){if(!frame)frame=requestAnimationFrame(animate);}
  let bounds=stage.getBoundingClientRect();
  stage.addEventListener('pointerenter',()=>{bounds=stage.getBoundingClientRect();});
  stage.addEventListener('pointermove',event=>{
    if(!canMove())return;
    const x=Math.max(-1,Math.min(1,(event.clientX-bounds.left)/bounds.width*2-1));
    const y=Math.max(-1,Math.min(1,(event.clientY-bounds.top)/bounds.height*2-1));
    target.x=home.x+y*.16;target.y=home.y+x*.17;requestRender();
  });
  function returnHome(){target.x=home.x;target.y=home.y;if(reducedMotion.matches){model.rotation.set(home.x,home.y,home.z);render();}else requestRender();}
  stage.addEventListener('pointerleave',returnHome);
  reducedMotion.addEventListener('change',()=>{returnHome();updateStatus();});
  finePointer.addEventListener('change',()=>{returnHome();updateStatus();});
  new ResizeObserver(entries=>{
    const {width,height}=entries[0].contentRect;
    if(!width||!height)return;
    camera.aspect=width/height;camera.updateProjectionMatrix();renderer.setSize(width,height,false);render();
  }).observe(stage);
  canvas.addEventListener('webglcontextlost',event=>{event.preventDefault();cancelAnimationFrame(frame);showFallback();});
  canvas.addEventListener('webglcontextrestored',()=>{render();stage.classList.add('loaded');updateStatus();});
  render();stage.classList.add('loaded');updateStatus();
} catch(error) {
  showFallback();
  console.warn('3D preview unavailable; the static device illustration is shown.',error);
}
