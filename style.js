/* StormSense AI — style.js
   Injects the full site stylesheet. Load synchronously in <head> so styles apply before first paint:
   <script src="style.js"></script> */
(function () {
  const css = `
:root{--bg:#020617;--bg2:#030712;--bg3:#06111F;--cy:#00F5D4;--bl:#00BFFF;--pu:#7C3AED;--wa:#F59E0B;--da:#FF3B30;--tx:#e6f1ff;--mu:#8ea3bd}
*{box-sizing:border-box;margin:0;padding:0}
html{scroll-behavior:auto}
body{background:var(--bg);color:var(--tx);font-family:Rajdhani,system-ui,sans-serif;font-size:19px;line-height:1.5;overflow-x:hidden;cursor:none}
@media(hover:none){body{cursor:auto}#cur,#glow{display:none}}
h1,h2,h3,.orb{font-family:Orbitron,sans-serif}
h2{font-size:clamp(2rem,5vw,4rem);line-height:1.08;margin-bottom:1.2rem}
a{color:inherit;text-decoration:none}
section{position:relative;padding:8rem 6vw;min-height:80vh}
body::before{content:"";position:fixed;inset:0;z-index:-2;background:linear-gradient(180deg,var(--bg),var(--bg3) 50%,var(--bg2));background-size:100% 100%}
body::after{content:"";position:fixed;inset:0;z-index:-1;background-image:linear-gradient(rgba(0,245,212,.05) 1px,transparent 1px),linear-gradient(90deg,rgba(0,245,212,.05) 1px,transparent 1px);background-size:60px 60px;mask-image:radial-gradient(ellipse at center,#000 30%,transparent 80%)}
#scan{position:fixed;inset:0;pointer-events:none;z-index:90;background:repeating-linear-gradient(0deg,rgba(255,255,255,.018) 0 1px,transparent 1px 3px)}
#glow{position:fixed;width:600px;height:600px;left:0;top:0;margin:-300px 0 0 -300px;background:radial-gradient(circle,rgba(0,191,255,.12),transparent 65%);pointer-events:none;z-index:-1}
#cur{position:fixed;width:18px;height:18px;margin:-9px 0 0 -9px;border:2px solid var(--cy);border-radius:50%;pointer-events:none;z-index:200;box-shadow:0 0 14px var(--cy);transition:width .2s,height .2s,margin .2s,background .2s}
#cur.h{width:46px;height:46px;margin:-23px 0 0 -23px;background:rgba(0,245,212,.12)}
#prog{position:fixed;top:0;left:0;height:3px;width:0;background:linear-gradient(90deg,var(--cy),var(--bl),var(--pu));z-index:150;box-shadow:0 0 10px var(--cy)}
.glass{background:rgba(8,20,38,.55);backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border:1px solid rgba(0,245,212,.25);border-radius:18px;box-shadow:0 0 30px rgba(0,191,255,.08),inset 0 0 30px rgba(0,245,212,.03)}
.tag{font-family:Orbitron;font-size:.7rem;letter-spacing:.25em;color:var(--cy);display:inline-block;padding:.4rem .9rem;border:1px solid var(--cy);border-radius:99px;box-shadow:0 0 16px rgba(0,245,212,.35);margin-bottom:1.2rem}
.demo{color:var(--wa);border-color:var(--wa);box-shadow:0 0 16px rgba(245,158,11,.3)}
.btn{font-family:Orbitron;font-size:.8rem;letter-spacing:.18em;padding:1rem 1.7rem;border-radius:10px;border:1px solid var(--cy);color:var(--cy);background:rgba(0,245,212,.06);display:inline-block;cursor:none;transition:box-shadow .25s,background .25s}
.btn:hover{background:rgba(0,245,212,.2);box-shadow:0 0 28px rgba(0,245,212,.55)}
.btn.p{background:linear-gradient(90deg,var(--cy),var(--bl));color:#001018;font-weight:800}
.btn.sm{padding:.6rem 1rem;font-size:.7rem}
@media(hover:none){.btn{cursor:pointer}}
/* NAV */
nav{position:fixed;top:14px;left:50%;transform:translateX(-50%);width:min(1150px,94vw);z-index:120;display:flex;align-items:center;justify-content:space-between;padding:.7rem 1.2rem}
.logo{font-family:Orbitron;font-weight:800;line-height:1;font-size:.95rem;letter-spacing:.15em}.logo span{color:var(--cy);display:block;font-size:.7rem}
.links{display:flex;gap:1.1rem;font-family:Orbitron;font-size:.62rem;letter-spacing:.14em}
.links a{color:var(--mu);padding:.3rem 0;border-bottom:1px solid transparent}.links a:hover,.links a.on{color:var(--cy);border-color:var(--cy)}
#ham{display:none;background:none;border:1px solid var(--cy);color:var(--cy);padding:.4rem .7rem;border-radius:8px;font-size:1.1rem}
.sys{font-family:Orbitron;font-size:.6rem;letter-spacing:.15em;color:var(--cy)}.sys i{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--cy);margin-right:6px;box-shadow:0 0 10px var(--cy);animation:bl 1.4s infinite}
@keyframes bl{50%{opacity:.25}}
@media(max-width:980px){.links{position:absolute;top:64px;left:0;right:0;flex-direction:column;background:rgba(3,7,18,.95);padding:1rem;border-radius:14px;display:none;border:1px solid var(--cy)}.links.open{display:flex}#ham{display:block}.sys{display:none}}
/* HERO */
#home{height:100vh;min-height:640px;display:flex;align-items:center;padding-top:6rem;overflow:hidden}
#hero3d{position:absolute;inset:0;z-index:0}
.hc{position:relative;z-index:2;max-width:640px;pointer-events:none}.hc *{pointer-events:auto}
.hc h1{font-size:clamp(2.4rem,6.5vw,5.4rem);line-height:1;letter-spacing:.04em;text-shadow:0 0 40px rgba(0,245,212,.4)}
.hc .sub{color:var(--bl);font-family:Orbitron;font-size:.85rem;letter-spacing:.22em;margin:1rem 0}
.hc q{display:block;font-size:1.6rem;margin:1rem 0;quotes:none;color:#fff}
.big{font-family:Orbitron;font-weight:800;font-size:clamp(4rem,11vw,8rem);line-height:1;background:linear-gradient(180deg,#fff,var(--cy) 60%,var(--bl));-webkit-background-clip:text;background-clip:text;color:transparent;filter:drop-shadow(0 0 22px rgba(0,245,212,.45))}
.big small{display:block;font-size:.8rem;letter-spacing:.35em;-webkit-text-fill-color:var(--mu)}
.btns{display:flex;gap:1rem;flex-wrap:wrap;margin-top:1.6rem}
.scrl{position:absolute;bottom:22px;left:50%;transform:translateX(-50%);z-index:3;font-family:Orbitron;font-size:.65rem;letter-spacing:.3em;color:var(--mu);animation:fl 2s infinite}
@keyframes fl{50%{transform:translate(-50%,8px)}}
.coord{position:absolute;font-family:Orbitron;font-size:.55rem;color:rgba(0,245,212,.55);letter-spacing:.15em;z-index:2}
/* PROBLEM */
.split{display:grid;grid-template-columns:1fr 1fr;gap:3rem;align-items:center}
@media(max-width:900px){.split{grid-template-columns:1fr}section{padding:6rem 5vw}}
.pc{padding:1.5rem 1.8rem;margin-bottom:1rem;transition:transform .35s,box-shadow .35s,border-color .35s}
.pc:hover{transform:scale(1.04) translateX(10px);border-color:var(--da);box-shadow:0 0 40px rgba(255,59,48,.35)}
.pc b{font-family:Orbitron;font-size:1.25rem;display:block;color:var(--wa)}
.pc p{color:var(--mu)}
#stormcv{width:100%;aspect-ratio:1;border-radius:50%}
/* VISION */
.flow{display:flex;align-items:center;justify-content:space-between;gap:1rem;margin:3rem 0;flex-wrap:wrap}
.fn{flex:1;min-width:220px;padding:2rem;text-align:center;font-family:Orbitron;font-size:1.1rem}
.fn.a{border-color:var(--da)}.fn.b{border-color:var(--pu);box-shadow:0 0 40px rgba(124,58,237,.4)}.fn.c{border-color:var(--cy)}
.arr{font-size:3rem;color:var(--cy);text-shadow:0 0 20px var(--cy)}
.lead{font-family:Orbitron;font-size:clamp(2rem,6vw,4.5rem);color:var(--cy);text-align:center}.lead small{display:block;font-size:.9rem;letter-spacing:.4em;color:var(--mu)}
.obj{display:grid;grid-template-columns:repeat(3,1fr);gap:1rem;margin-top:2rem}@media(max-width:900px){.obj{grid-template-columns:1fr}}
.obj div{padding:1.4rem}.obj h3{font-size:1rem;color:var(--cy);margin-bottom:.4rem}
/* CAPABILITIES */
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:1.4rem;perspective:1200px}
@media(max-width:980px){.grid3{grid-template-columns:1fr 1fr}}@media(max-width:620px){.grid3{grid-template-columns:1fr}}
.card{padding:2rem;position:relative;overflow:hidden;transform-style:preserve-3d;transition:box-shadow .3s,border-color .3s;will-change:transform}
.card:hover{border-color:var(--cy);box-shadow:0 0 50px rgba(0,245,212,.3)}
.card::before{content:"";position:absolute;inset:-50%;background:conic-gradient(from 0deg,transparent,rgba(0,245,212,.16),transparent 30%);animation:sp 7s linear infinite;z-index:0}
.card>*{position:relative;z-index:1}
@keyframes sp{to{transform:rotate(360deg)}}
.card svg{width:56px;height:56px;stroke:var(--cy);fill:none;stroke-width:1.5;filter:drop-shadow(0 0 8px var(--cy));margin-bottom:1rem;animation:fl2 3s ease-in-out infinite}
@keyframes fl2{50%{transform:translateY(-6px)}}
.card h3{font-size:1rem;margin-bottom:.5rem}.card p{color:var(--mu)}
/* STORM SIMULATION */
#simwrap{padding:1rem;position:relative}
#simcv{width:100%;height:min(62vh,560px);display:block;border-radius:12px;background:#03101c;cursor:crosshair}
.hud{display:grid;grid-template-columns:repeat(5,1fr);gap:.8rem;margin:1rem 0}
@media(max-width:800px){.hud{grid-template-columns:repeat(2,1fr)}}
.hud div{padding:.8rem 1rem;text-align:center}.hud small{font-family:Orbitron;font-size:.6rem;letter-spacing:.2em;color:var(--mu);display:block}
.hud b{font-family:Orbitron;font-size:1.4rem;color:var(--cy)}
.ctl{display:flex;gap:.8rem;align-items:center;flex-wrap:wrap}
.ctl label{font-family:Orbitron;font-size:.65rem;letter-spacing:.15em;color:var(--mu)}
input[type=range]{accent-color:var(--cy);width:180px}
#tl{display:flex;justify-content:space-between;font-family:Orbitron;font-size:.6rem;margin-top:.8rem;color:var(--mu)}
#tl span.on{color:var(--cy);text-shadow:0 0 10px var(--cy)}
#bar{height:5px;background:rgba(255,255,255,.1);border-radius:9px;margin-top:.5rem}#bar i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--cy),var(--wa),var(--da));border-radius:9px}
#alert{position:absolute;top:1.6rem;right:1.6rem;width:min(320px,80%);padding:1rem 1.2rem;border:1px solid var(--da);background:rgba(40,6,6,.85);border-radius:12px;box-shadow:0 0 30px rgba(255,59,48,.5);transform:translateX(120%);transition:transform .6s cubic-bezier(.2,1.2,.3,1);font-size:.95rem}
#alert.show{transform:none;animation:ap 1.6s infinite}
@keyframes ap{50%{box-shadow:0 0 55px rgba(255,59,48,.85)}}
#alert b{font-family:Orbitron;color:var(--da);font-size:.8rem;display:block}
.demolbl{position:absolute;top:1.6rem;left:1.6rem;z-index:3}
body.ex nav,body.ex section:not(#simulation),body.ex footer{display:none}
body.ex #simulation{position:fixed;inset:0;z-index:300;background:var(--bg);overflow:auto;padding:1.2rem 3vw;min-height:0}
body.ex #simcv{height:66vh}
#exh{display:none;text-align:center;font-family:Orbitron;font-size:clamp(1rem,2.4vw,1.8rem);letter-spacing:.2em;color:var(--cy);text-shadow:0 0 20px var(--cy);margin-bottom:.8rem}
body.ex #exh{display:block}
body.ex .hd{display:none}
/* PIPELINE / DATA */
#pipecv,#datacv{width:100%;height:440px;display:block}
.phases{display:grid;grid-template-columns:repeat(4,1fr);gap:1rem;margin-top:1rem}@media(max-width:800px){.phases{grid-template-columns:1fr 1fr}}
.phases div{padding:1rem;font-size:.95rem;color:var(--mu)}.phases b{font-family:Orbitron;color:var(--cy);display:block;font-size:.75rem;letter-spacing:.12em}
/* TECH */
#stack{perspective:1400px;display:flex;flex-direction:column;gap:1.1rem;max-width:900px;margin:2rem auto;transform-style:preserve-3d}
.layer{padding:1.4rem 2rem;transform:rotateX(52deg) rotateZ(-18deg);transform-origin:center;transition:transform .5s,box-shadow .5s,border-color .5s;display:flex;justify-content:space-between;gap:1rem;flex-wrap:wrap;align-items:center}
.layer:hover{transform:rotateX(20deg) rotateZ(-4deg) translateZ(60px) scale(1.04);border-color:var(--cy);box-shadow:0 0 60px rgba(0,245,212,.45)}
.layer h3{color:var(--cy);font-size:.95rem}.layer span{color:var(--mu);font-size:1.05rem}.layer:nth-child(2){border-color:var(--bl)}.layer:nth-child(3){border-color:var(--pu)}.layer:nth-child(4){border-color:var(--wa)}
@media(max-width:700px){.layer{transform:none}}
/* METHOD */
#mh{display:flex;gap:1.4rem;overflow-x:auto;padding:1rem 0 2rem;scroll-snap-type:x mandatory}
.step{min-width:min(320px,80vw);padding:2rem;scroll-snap-align:start;flex:1}
.step em{font-family:Orbitron;font-style:normal;font-size:3.4rem;color:transparent;-webkit-text-stroke:1px var(--cy);display:block}
.step h3{margin:.4rem 0;font-size:1.1rem}.step p{color:var(--mu)}
/* IMPACT */
#impact{background:radial-gradient(ellipse at 50% 100%,rgba(124,58,237,.18),transparent 65%)}
.ic{padding:3rem 2.2rem;margin-bottom:2rem;display:flex;gap:2rem;align-items:center;flex-wrap:wrap}
.ic .e{font-size:5rem;filter:drop-shadow(0 0 20px var(--cy))}.ic h3{font-size:1.4rem;color:var(--cy)}.ic q{font-size:clamp(1.5rem,3.4vw,2.6rem);display:block;line-height:1.2;quotes:none;margin:.5rem 0}.ic p{color:var(--mu)}
/* TEAM */
.tg{display:grid;grid-template-columns:repeat(3,1fr);gap:1.2rem}@media(max-width:800px){.tg{grid-template-columns:1fr 1fr}}
.tm{padding:1.6rem;text-align:center;transform-style:preserve-3d;will-change:transform}
.av{width:96px;height:96px;margin:0 auto 1rem;border-radius:50%;background:radial-gradient(circle at 50% 38%,rgba(0,245,212,.5) 0 17%,transparent 18%),radial-gradient(ellipse at 50% 100%,rgba(0,191,255,.5) 0 42%,transparent 43%),#06182a;border:1px solid var(--cy);box-shadow:0 0 24px rgba(0,245,212,.4);position:relative;overflow:hidden}
.av::after{content:"";position:absolute;left:0;right:0;height:2px;background:var(--cy);box-shadow:0 0 12px var(--cy);animation:scn 2.4s linear infinite}
@keyframes scn{from{top:0}to{top:100%}}
.tm h3{font-size:1.1rem}.tm small{color:var(--wa);font-family:Orbitron;font-size:.6rem;letter-spacing:.2em;display:block}.tm p{color:var(--mu);font-size:.95rem}
.tm.lead{border-color:var(--wa);box-shadow:0 0 34px rgba(245,158,11,.3)}
/* NEXT */
.tlx{display:flex;flex-direction:column;gap:1.4rem;max-width:760px;margin:2rem auto;position:relative;padding-left:3rem}
.tlx::before{content:"";position:absolute;left:1rem;top:0;bottom:0;width:2px;background:linear-gradient(var(--cy),var(--pu));box-shadow:0 0 12px var(--cy)}
.ls{padding:1.4rem 1.8rem;position:relative}.ls::before{content:"";position:absolute;left:-2.45rem;top:1.7rem;width:16px;height:16px;border-radius:50%;background:var(--bg);border:2px solid var(--cy);box-shadow:0 0 14px var(--cy)}
.ls b{font-family:Orbitron;color:var(--cy);font-size:.7rem;letter-spacing:.25em}.ls h3{font-size:1.2rem}
/* FINAL */
#final{min-height:100vh;background:#000;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;gap:2rem}
#final h2{font-size:clamp(2rem,6vw,5rem);text-shadow:0 0 50px rgba(0,245,212,.6);color:#fff}
#final q{font-size:1.5rem;color:var(--mu);quotes:none}
footer{padding:1.5rem;text-align:center;color:var(--mu);font-size:.8rem;background:#000}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
`;
  const el = document.createElement('style');
  el.id = 'stormsense-styles';
  el.textContent = css;
  document.head.appendChild(el);
})();
