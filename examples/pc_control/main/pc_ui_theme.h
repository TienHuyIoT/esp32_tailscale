/**
 * @file pc_ui_theme.h
 * @brief Shared ISUI theme (Immersive Spatial UI, dark glass) for every /pc
 *        page: control deck, settings and security.
 *
 * Visual language adapted from the design kit reference
 * (docs/design-standard/ISUI_Web_Design_Kit_v2/reference/ISUI_REFERENCE.html):
 * layered glass panels, cyan/violet accents, subtle depth, restrained motion.
 * The reference is a demo prototype - only its visual language is reused;
 * every interaction on these pages stays wired to the real device APIs.
 *
 * PCCTL_UI_CSS / PCCTL_UI_JS are folded into each page by send_ui_page()
 * (pc_api.c) with the chunked HTTP responder, so all pages share one theme
 * without duplicating it inside three string literals. Include this header
 * ONCE per translation unit (pc_api.c), before the page headers.
 */

#pragma once

static const char PCCTL_UI_CSS[] = R"CSSCSS(
:root{--bg:#07111f;--panel:rgba(18,34,54,.58);--panel-2:rgba(11,24,41,.55);--line:rgba(207,231,255,.13);--line-hi:rgba(174,231,255,.28);--text:#eef7ff;--muted:#8da4bd;--dim:#6f88a4;--cyan:#67e8f9;--violet:#a78bfa;--green:#86efac;--amber:#fcd58b;--red:#fb7185;--shadow:0 24px 70px rgba(0,0,0,.34);--radius:22px}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;min-height:100vh;color:var(--text);font:14px/1.5 Inter,ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;background:radial-gradient(ellipse at 16% 8%,rgba(35,124,167,.23),transparent 34%),radial-gradient(ellipse at 82% 22%,rgba(124,74,207,.21),transparent 34%),linear-gradient(145deg,#050b15,#091525 55%,#070d18);overflow-x:hidden}
body:before{content:"";position:fixed;inset:0;z-index:-1;pointer-events:none;opacity:.13;background-image:linear-gradient(rgba(126,190,230,.12) 1px,transparent 1px),linear-gradient(90deg,rgba(126,190,230,.12) 1px,transparent 1px);background-size:48px 48px;-webkit-mask-image:linear-gradient(to bottom,black,transparent 85%);mask-image:linear-gradient(to bottom,black,transparent 85%)}
button,input,select{font:inherit}
button{cursor:pointer}
a{color:var(--cyan);text-decoration:none}
code{background:rgba(103,232,249,.08);border:1px solid rgba(103,232,249,.14);padding:1px 6px;border-radius:7px;font-size:.92em;color:#d6f7ff}
button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:2px solid var(--cyan);outline-offset:3px}
.hidden{display:none!important}
.ambient{position:fixed;inset:0;pointer-events:none;overflow:hidden;z-index:-1}
.orb{position:absolute;border-radius:50%;filter:blur(3px);opacity:.36;will-change:transform}
.orb.one{width:360px;height:360px;left:-150px;top:25%;background:radial-gradient(circle,rgba(34,211,238,.26),transparent 70%)}
.orb.two{width:480px;height:480px;right:-170px;top:3%;background:radial-gradient(circle,rgba(167,139,250,.25),transparent 70%)}
.orb.three{width:330px;height:330px;right:22%;bottom:-200px;background:radial-gradient(circle,rgba(59,130,246,.16),transparent 70%)}
.app{max-width:1480px;margin:auto;padding:28px 32px 42px}
.topbar{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:32px}
.brand{display:flex;align-items:center;gap:13px;min-width:0}
.logo{width:44px;height:44px;border:1px solid rgba(103,232,249,.35);border-radius:15px;display:grid;place-items:center;background:linear-gradient(145deg,rgba(103,232,249,.18),rgba(167,139,250,.13));box-shadow:inset 0 1px rgba(255,255,255,.18),0 8px 30px rgba(34,211,238,.09);color:#bdf3ff}
.logo svg{width:25px;height:25px}
.brand h1{font-size:17px;letter-spacing:-.025em;margin:0;font-weight:720}
.brand p{font-size:11px;color:var(--muted);margin:2px 0 0;letter-spacing:.12em;text-transform:uppercase}
.acc{color:var(--cyan)}
.top-actions{display:flex;align-items:center;gap:10px}
.connection{display:flex;align-items:center;gap:8px;border:1px solid var(--line);background:rgba(10,24,40,.58);padding:9px 12px;border-radius:999px;color:#b9cce0;font-size:12px;white-space:nowrap}
.dot{width:7px;height:7px;border-radius:50%;background:var(--green);box-shadow:0 0 12px rgba(134,239,172,.6);flex:0 0 auto}
.connection.warn .dot{background:var(--amber);box-shadow:0 0 12px rgba(252,213,139,.45)}
.connection.bad .dot{background:var(--red);box-shadow:0 0 12px rgba(251,113,133,.45)}
.icon-btn{width:38px;height:38px;border-radius:13px;border:1px solid var(--line);background:rgba(255,255,255,.045);color:#d7e7f6;display:grid;place-items:center;transition:.2s;text-decoration:none}
.icon-btn:hover{border-color:var(--line-hi);background:rgba(255,255,255,.09);transform:translateY(-2px)}
.icon-btn svg{width:17px;height:17px}
.layout{display:grid;grid-template-columns:220px minmax(0,1fr);gap:26px;align-items:start}
.sidebar{position:sticky;top:24px;padding:13px;border:1px solid var(--line);border-radius:24px;background:var(--panel-2);backdrop-filter:blur(18px) saturate(130%);-webkit-backdrop-filter:blur(18px) saturate(130%);box-shadow:var(--shadow)}
.nav-label{font-size:10px;text-transform:uppercase;letter-spacing:.16em;color:var(--dim);padding:10px 12px 9px}
.nav{display:flex;align-items:center;gap:12px;padding:12px;border-radius:14px;color:#91a8c0;text-decoration:none;margin:3px 0;transition:.2s;border:1px solid transparent;font-size:13px}
.nav svg{width:17px;height:17px;opacity:.88;flex:0 0 auto}
.nav:hover{color:#eaf8ff;background:rgba(255,255,255,.04)}
.nav.active{color:#eafaff;background:linear-gradient(105deg,rgba(103,232,249,.14),rgba(103,232,249,.035));border-color:rgba(103,232,249,.16);box-shadow:inset 3px 0 0 var(--cyan)}
.nav.active svg{color:var(--cyan)}
.sidebar-foot{margin:26px 4px 4px;padding:14px 12px;border-top:1px solid var(--line);color:var(--muted);font-size:11px;word-break:break-word}
.sidebar-foot strong{display:block;color:#d7e7f6;font-size:12px;margin-bottom:3px}
main{min-width:0}
.page-heading{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin:0 0 23px;flex-wrap:wrap}
.eyebrow{color:var(--cyan);font-size:10px;letter-spacing:.19em;text-transform:uppercase;font-weight:750;margin-bottom:8px}
h2{font-size:clamp(24px,3vw,33px);letter-spacing:-.045em;line-height:1.15;margin:0;font-weight:730}
.subtitle{color:var(--muted);margin:9px 0 0;font-size:13px}
.primary-btn,.soft-btn,.outline-btn,.danger-btn{border-radius:13px;padding:10px 14px;border:1px solid var(--line-hi);color:#d9efff;background:rgba(255,255,255,.045);display:inline-flex;align-items:center;justify-content:center;gap:8px;transition:transform .18s,border-color .18s,background .18s;white-space:nowrap;font-size:13px;text-decoration:none}
.primary-btn{border-color:rgba(103,232,249,.45);background:linear-gradient(135deg,rgba(103,232,249,.23),rgba(103,232,249,.08));box-shadow:inset 0 1px rgba(255,255,255,.16),0 8px 24px rgba(34,211,238,.08)}
.primary-btn:hover{transform:translateY(-2px)}
.soft-btn:hover,.outline-btn:hover{transform:translateY(-2px);border-color:rgba(103,232,249,.48);background:rgba(103,232,249,.07)}
.primary-btn svg,.soft-btn svg,.outline-btn svg{width:16px;height:16px}
.danger-btn{border-color:rgba(251,113,133,.4);background:linear-gradient(135deg,rgba(251,113,133,.2),rgba(251,113,133,.07));color:#ffd9de}
.danger-btn svg{width:16px;height:16px}
.primary-btn:disabled,.soft-btn:disabled,.outline-btn:disabled,.danger-btn:disabled{opacity:.45;cursor:not-allowed;transform:none}
.banner{padding:11px 14px;border-radius:14px;margin-bottom:14px;font-size:12.5px;border:1px solid;display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}
.banner.warn{background:rgba(252,213,139,.07);border-color:rgba(252,213,139,.3);color:var(--amber)}
.banner.err{background:rgba(251,113,133,.07);border-color:rgba(251,113,133,.3);color:#fda4af}
.glass{background:linear-gradient(135deg,rgba(22,42,64,.72),rgba(8,20,35,.56));border:1px solid var(--line);backdrop-filter:blur(26px) saturate(135%);-webkit-backdrop-filter:blur(26px) saturate(135%);box-shadow:var(--shadow),inset 0 1px rgba(255,255,255,.075);border-radius:var(--radius);position:relative;overflow:hidden;isolation:isolate}
.glass>*{position:relative;z-index:1}
.glass:before{content:"";position:absolute;inset:0;pointer-events:none;background:linear-gradient(125deg,rgba(255,255,255,.065),transparent 28%,transparent 70%,rgba(103,232,249,.035));z-index:0}
.glass:after{content:"";position:absolute;inset:0;pointer-events:none;border-radius:inherit;border:1px solid rgba(255,255,255,.035);box-shadow:inset 0 1px rgba(255,255,255,.04);z-index:0}
.glass .sheen{position:absolute;z-index:0;inset:0;pointer-events:none;opacity:0;background:radial-gradient(420px circle at var(--mx,50%) var(--my,50%),rgba(103,232,249,.12),transparent 42%);transition:opacity .25s}
.glass:hover .sheen{opacity:1}
@supports not ((backdrop-filter:blur(1px)) or (-webkit-backdrop-filter:blur(1px))){.glass{background:linear-gradient(135deg,rgba(22,42,64,.96),rgba(8,20,35,.94))}.sidebar{background:rgba(11,24,41,.97)}.modal{background:rgba(14,28,46,.99)}}
body.no-blur .glass,body.no-blur .sidebar,body.no-blur .modal,body.no-blur .toast,body.no-blur .modal-backdrop{backdrop-filter:none!important;-webkit-backdrop-filter:none!important}
body.no-blur .glass{background:linear-gradient(135deg,rgba(19,38,61,.97),rgba(8,20,35,.96))}
body.no-blur .sidebar{background:rgba(11,24,41,.97)}
.card-top{display:flex;align-items:center;justify-content:space-between;gap:12px;position:relative;flex-wrap:wrap}
.section-tag{display:flex;align-items:center;gap:8px;color:#9fb5cc;font-size:11px;text-transform:uppercase;letter-spacing:.13em;font-weight:700}
.section-tag svg{width:16px;height:16px;color:var(--cyan);flex:0 0 auto}
.status-chip{display:inline-flex;align-items:center;gap:7px;border:1px solid rgba(134,239,172,.2);background:rgba(134,239,172,.07);color:#b6f7ca;padding:6px 9px;border-radius:999px;font-size:10px;letter-spacing:.04em;white-space:nowrap}
.status-chip.mid{border-color:rgba(252,213,139,.28);background:rgba(252,213,139,.07);color:var(--amber)}
.status-chip.bad{border-color:rgba(251,113,133,.28);background:rgba(251,113,133,.07);color:#fda4af}
.status-chip .dot{width:6px;height:6px}
.count{font-size:11px;color:#9cb2c8;border:1px solid var(--line);background:rgba(255,255,255,.035);border-radius:9px;padding:6px 9px;white-space:nowrap}
.hero-grid{display:grid;grid-template-columns:minmax(0,1.55fr) minmax(260px,.85fr);gap:18px;margin-bottom:18px}
.hero{padding:25px 26px 23px;min-height:250px}
.hero-body{display:flex;align-items:center;justify-content:space-between;gap:24px;margin-top:27px;position:relative}
.hero h3{font-size:23px;letter-spacing:-.035em;margin:0;font-weight:680}
.hero-copy{color:#91a9c1;font-size:12px;max-width:400px;margin:8px 0 18px;line-height:1.6;word-break:break-word}
.hero-buttons{display:flex;gap:9px;flex-wrap:wrap}
.power-orb{flex:0 0 140px;height:140px;border-radius:50%;display:grid;place-items:center;position:relative;background:radial-gradient(circle at 32% 24%,rgba(255,255,255,.28),rgba(103,232,249,.13) 26%,rgba(22,80,105,.22) 52%,rgba(7,17,31,.42) 70%);border:1px solid rgba(160,235,255,.32);box-shadow:inset 0 2px 3px rgba(255,255,255,.19),inset 0 -18px 30px rgba(0,0,0,.22),0 0 55px rgba(34,211,238,.15);animation:orbFloat 6s ease-in-out infinite;transition:.35s}
@keyframes orbFloat{0%,100%{translate:0 0}50%{translate:0 -5px}}
.power-orb:before{content:"";position:absolute;inset:10px;border-radius:50%;border:1px solid rgba(103,232,249,.25);box-shadow:inset 0 0 22px rgba(103,232,249,.1);animation:orbSpin 14s linear infinite}
@keyframes orbSpin{to{transform:rotate(360deg)}}
.power-orb:after{content:"";position:absolute;inset:0;border-radius:50%;background:conic-gradient(from 210deg,transparent 0 25%,rgba(103,232,249,.58) 40%,rgba(167,139,250,.48) 56%,transparent 74%);-webkit-mask:radial-gradient(circle,transparent 65%,#000 67%,#000 68%,transparent 70%);mask:radial-gradient(circle,transparent 65%,#000 67%,#000 68%,transparent 70%);opacity:.9;animation:orbSpin 18s linear infinite reverse}
.power-orb svg{width:48px;height:48px;color:#b4f7ff;filter:drop-shadow(0 0 12px rgba(103,232,249,.6))}
.hero:hover .power-orb{transform:translateY(-3px) rotate(2deg)}
.metric-panel{padding:22px 22px 18px}
.metric-panel h3{font-size:15px;margin:0;font-weight:650}
.small-copy{color:var(--muted);font-size:11px;margin-top:4px}
.metrics{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:18px}
.metric{border:1px solid rgba(205,230,255,.09);background:rgba(255,255,255,.025);border-radius:15px;padding:13px}
.metric-head{display:flex;align-items:center;justify-content:space-between;color:#9bb0c7;font-size:10px}
.metric-head svg{width:15px;height:15px;color:var(--cyan);flex:0 0 auto}
.metric-value{font-size:23px;font-weight:720;letter-spacing:-.04em;margin-top:5px}
.metric-value .unit{font-size:11px;color:#8fa6be;font-weight:500}
.metric-foot{color:#7790aa;font-size:10px;margin-top:2px;word-break:break-word}
.content-section{margin-top:27px}
.section-heading{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:14px;flex-wrap:wrap}
.section-heading h3{font-size:17px;letter-spacing:-.025em;margin:0;font-weight:680}
.section-heading p{color:var(--muted);font-size:11px;margin:4px 0 0}
.pc-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:13px}
.action-card{padding:17px;min-height:145px;transition:transform .22s,border-color .22s}
.action-card:hover{transform:translateY(-5px);border-color:rgba(103,232,249,.3)}
.action-icon{width:38px;height:38px;border:1px solid rgba(103,232,249,.17);background:linear-gradient(145deg,rgba(103,232,249,.12),rgba(167,139,250,.08));border-radius:13px;display:grid;place-items:center;color:var(--cyan);box-shadow:inset 0 1px rgba(255,255,255,.1)}
.action-icon svg{width:19px;height:19px}
.action-card h4{font-size:13px;margin:13px 0 2px;font-weight:650}
.action-card p{font-size:10px;color:var(--muted);margin:0}
.action-bottom{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:15px}
.mini-status{font-size:10px;color:#7f99b2;display:flex;align-items:center;gap:6px}
.mini-status .dot{width:5px;height:5px;box-shadow:none;background:#7890a9}
.mini-status.ok{color:var(--green)}
.mini-status.ok .dot{background:var(--green);box-shadow:0 0 9px rgba(134,239,172,.4)}
.mini-status.warn{color:var(--amber)}
.mini-status.warn .dot{background:var(--amber)}
.run-btn{width:30px;height:30px;display:grid;place-items:center;border-radius:10px;background:rgba(103,232,249,.07);border:1px solid rgba(103,232,249,.15);color:#a7f3fc;transition:.2s}
.run-btn:hover{background:rgba(103,232,249,.17);transform:scale(1.06)}
.run-btn:disabled{opacity:.4;cursor:not-allowed;transform:none}
.run-btn svg{width:14px;height:14px}
.action-card.danger .action-icon{color:#fca5a5;border-color:rgba(251,113,133,.18);background:rgba(251,113,133,.06)}
.action-card.danger .run-btn{color:#fda4af;border-color:rgba(251,113,133,.2);background:rgba(251,113,133,.06)}
.relay-toolbar{display:flex;align-items:center;gap:9px;flex-wrap:wrap}
.relay-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}
.empty-card{padding:22px;margin-top:0}
.relay-card{padding:18px;transition:border-color .2s,transform .2s}
.relay-card:hover{border-color:rgba(167,139,250,.31);transform:translateY(-3px)}
.relay-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}
.relay-name-wrap{display:flex;align-items:center;gap:12px;min-width:0}
.relay-icon{width:43px;height:43px;border-radius:14px;display:grid;place-items:center;background:linear-gradient(145deg,rgba(167,139,250,.17),rgba(103,232,249,.07));border:1px solid rgba(167,139,250,.2);color:#c4b5fd;flex:0 0 auto}
.relay-icon svg{width:20px;height:20px}
.relay-name{font-size:14px;font-weight:650;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.relay-id{color:#7891aa;font-size:10px;margin-top:2px}
.state-label{display:flex;align-items:center;gap:6px;font-size:10px;color:#8da5bd;white-space:nowrap}
.state-label.on{color:var(--green)}
.state-label .dot{background:#6d8298;box-shadow:none}
.state-label.on .dot{background:var(--green);box-shadow:0 0 10px rgba(134,239,172,.45)}
.relay-mid{display:flex;align-items:center;justify-content:space-between;gap:15px;margin:20px 0 17px}
.relay-state{font-size:25px;font-weight:730;letter-spacing:-.045em}
.relay-state.on{color:#a7f3c0;text-shadow:0 0 25px rgba(134,239,172,.19)}
.relay-state small{display:block;font-size:10px;color:#7891aa;letter-spacing:.07em;text-transform:uppercase;font-weight:550;margin-top:2px}
.relay-card:has(.state-label.on){border-color:rgba(134,239,172,.16)}
.relay-card:has(.state-label.on) .relay-icon{color:#a7f3c0;border-color:rgba(134,239,172,.22)}
.switch{width:53px;height:30px;border-radius:99px;border:1px solid rgba(172,194,218,.18);background:rgba(6,15,27,.7);padding:3px;display:flex;align-items:center;transition:.25s;box-shadow:inset 0 2px 5px rgba(0,0,0,.3);flex:0 0 auto}
.switch span{width:22px;height:22px;border-radius:50%;background:linear-gradient(145deg,#c6d5e5,#748ba3);box-shadow:0 2px 7px rgba(0,0,0,.35),inset 0 1px rgba(255,255,255,.7);transition:.25s}
.switch[aria-checked="true"]{background:linear-gradient(100deg,rgba(34,197,94,.25),rgba(103,232,249,.18));border-color:rgba(134,239,172,.35)}
.switch[aria-checked="true"] span{transform:translateX(22px);background:linear-gradient(145deg,#d7ffe3,#7be7a1)}
.switch:disabled{opacity:.45;cursor:not-allowed}
.relay-meta{display:flex;align-items:center;gap:7px;flex-wrap:wrap;padding-top:13px;border-top:1px solid rgba(208,229,250,.08)}
.meta-chip{display:flex;align-items:center;gap:5px;border:1px solid rgba(208,229,250,.09);background:rgba(255,255,255,.025);color:#92a9c1;border-radius:8px;padding:5px 7px;font-size:10px}
.meta-chip b{color:#d5e6f5;font-weight:600}
.logic{font-size:9px;padding:4px 7px;border-radius:7px;border:1px solid rgba(103,232,249,.15);background:rgba(103,232,249,.055);color:#a5eaf4;white-space:nowrap}
.logic.low{border-color:rgba(167,139,250,.2);background:rgba(167,139,250,.07);color:#d1c4ff}
.relay-actions{display:flex;gap:8px;margin-top:14px}
.relay-actions button{flex:1;border-radius:10px;padding:8px 10px;font-size:11px;border:1px solid rgba(205,225,245,.12);background:rgba(255,255,255,.035);color:#b3c4d7;transition:.2s}
.relay-actions .on-btn{border-color:rgba(134,239,172,.22);background:rgba(134,239,172,.07);color:#b8f7ca}
.relay-actions .on-btn:hover{background:rgba(134,239,172,.14)}
.relay-actions .off-btn:hover{background:rgba(255,255,255,.08)}
.relay-actions .config-btn{flex:0 0 auto;width:38px;padding:8px;display:grid;place-items:center}
.relay-actions .config-btn svg{width:14px;height:14px}
.relay-actions button:disabled{opacity:.4;cursor:not-allowed}
.lower-grid{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(0,.9fr);gap:15px;margin-top:20px}
.lower-card{padding:20px;margin-bottom:14px}
.lower-card h3{font-size:14px;margin:0;font-weight:650}
.lower-card .desc{font-size:11px;color:var(--muted);margin:4px 0 16px}
.gpio-table{width:100%;border-collapse:collapse}
.gpio-table th{text-align:left;color:var(--dim);font-size:9px;text-transform:uppercase;letter-spacing:.12em;font-weight:650;padding:0 8px 10px}
.gpio-table td{padding:10px 8px;border-top:1px solid rgba(208,229,250,.075);font-size:11px;color:#b3c7dc}
.gpio-table td:first-child{color:#edf7ff;font-weight:650}
.pin{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;color:#a5f3fc!important}
.event-list{display:flex;flex-direction:column;max-height:330px;overflow:auto}
.event{display:grid;grid-template-columns:30px minmax(0,1fr) auto;align-items:center;gap:10px;padding:11px 0;border-top:1px solid rgba(208,229,250,.075)}
.event:first-child{border-top:0}
.event-icon{width:29px;height:29px;border-radius:10px;background:rgba(103,232,249,.07);border:1px solid rgba(103,232,249,.11);display:grid;place-items:center;color:var(--cyan)}
.event-icon svg{width:14px;height:14px}
.event strong{display:block;font-size:11px;font-weight:600}
.event small{display:block;color:#718aa4;font-size:10px;margin-top:2px;word-break:break-word}
.event time{color:#7289a2;font-size:10px;white-space:nowrap}
.modal-backdrop{position:fixed;inset:0;background:rgba(1,7,14,.68);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);z-index:30;display:none;align-items:center;justify-content:center;padding:18px}
.modal-backdrop.open{display:flex}
.modal{width:min(520px,100%);border:1px solid rgba(207,231,255,.18);border-radius:23px;padding:22px;background:linear-gradient(145deg,rgba(19,38,61,.97),rgba(8,20,35,.97));box-shadow:0 30px 100px rgba(0,0,0,.55);max-height:calc(100vh - 40px);overflow:auto}
.modal-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:18px;gap:12px}
.modal-head h3{margin:0;font-size:18px}
.modal p{font-size:13px;color:#a9bfd6;line-height:1.6;margin:0 0 12px}
.form-grid{display:grid;grid-template-columns:1fr 1fr;gap:13px}
.field{display:flex;flex-direction:column;gap:6px}
.field.full{grid-column:1/-1}
.field label,.frm label{font-size:11px;color:#9bb2ca}
.field input,.field select,.frm input,.frm select,.txt{width:100%;border:1px solid rgba(208,229,250,.14);border-radius:11px;background:#0b192b;color:#eaf7ff;padding:10px 11px;font-size:13px}
.field input:disabled,.txt:disabled{opacity:.55}
.frm{display:flex;flex-direction:column;gap:6px;margin-bottom:11px;max-width:430px}
.form-note{color:#7f98b2;font-size:10px;margin:14px 0;line-height:1.6}
.modal-actions{display:flex;justify-content:flex-end;gap:9px;margin-top:18px;flex-wrap:wrap}
.switch-row{display:flex;align-items:center;justify-content:space-between;gap:14px;padding:10px 12px;border:1px solid rgba(208,229,250,.12);border-radius:11px;background:rgba(255,255,255,.025);font-size:12px}
.settings-list{display:flex;flex-direction:column;gap:11px}
.setting-row{display:flex;justify-content:space-between;align-items:center;gap:16px;padding:12px 0;border-bottom:1px solid rgba(208,229,250,.08)}
.setting-row:last-child{border:0}
.setting-row strong{font-size:12px;display:block}
.setting-row small{color:#8199b2;font-size:10px;display:block;margin-top:3px}
.chkrow{display:flex;gap:10px;align-items:center;padding:7px 0;font-size:13px;cursor:pointer}
.chkrow input{width:16px;height:16px;accent-color:#22d3ee}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.toast{position:fixed;right:24px;bottom:24px;z-index:40;max-width:min(360px,calc(100vw - 32px));padding:13px 16px;border:1px solid rgba(103,232,249,.24);background:rgba(10,25,42,.92);backdrop-filter:blur(18px);-webkit-backdrop-filter:blur(18px);box-shadow:var(--shadow);border-radius:15px;color:#dff9ff;font-size:13px;opacity:0;transform:translateY(12px);pointer-events:none;transition:.25s}
.toast.show{opacity:1;transform:translateY(0)}
.toast.err{border-color:rgba(251,113,133,.35);color:#ffd9de}
.spin{display:inline-block;width:11px;height:11px;border:2px solid var(--cyan);border-top-color:transparent;border-radius:50%;animation:sp 1s linear infinite;vertical-align:middle;margin-right:6px}
@keyframes sp{to{transform:rotate(360deg)}}
.hint{font-size:11px;color:var(--muted);margin:8px 0 0;line-height:1.6}
footer.footer{display:flex;align-items:center;justify-content:space-between;gap:15px;color:#647d98;font-size:10px;margin-top:24px;padding:0 2px;flex-wrap:wrap;line-height:1.6}
.footer .safe{display:flex;align-items:center;gap:7px}
.footer svg{width:13px;height:13px;color:var(--green);flex:0 0 auto}
@keyframes riseIn{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:translateY(0)}}
.page-heading,.hero-grid,.content-section,.lower-grid{animation:riseIn .65s cubic-bezier(.2,.7,.2,1) both}
.hero-grid{animation-delay:.06s}.content-section{animation-delay:.12s}.lower-grid{animation-delay:.18s}
@media(max-width:1100px){.layout{grid-template-columns:74px minmax(0,1fr);gap:18px}.sidebar{padding:10px}.nav{justify-content:center;padding:13px 8px}.nav span,.nav-label,.sidebar-foot{display:none}.nav svg{width:19px;height:19px}.pc-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.app{padding:22px}}
@media(max-width:760px){.app{padding:17px 14px 30px}.topbar{margin-bottom:25px}.layout{display:block}.sidebar{position:sticky;top:8px;z-index:10;display:flex;gap:4px;overflow-x:auto;padding:7px;margin-bottom:24px;border-radius:16px}.nav{flex:1;min-width:43px;margin:0;border-radius:11px}.nav.active{box-shadow:inset 0 -2px 0 var(--cyan)}.page-heading{align-items:flex-start}.hero-grid{grid-template-columns:1fr}.hero{min-height:0}.metric-panel{padding:19px}.power-orb{flex-basis:100px;height:100px}.power-orb svg{width:36px;height:36px}.hero h3{font-size:21px}.lower-grid{grid-template-columns:1fr}.relay-grid{grid-template-columns:1fr 1fr}.relay-card{padding:14px}.relay-state{font-size:22px}.footer{flex-direction:column;align-items:flex-start}.connection{font-size:11px}.event-list{max-height:260px}}
@media(max-width:480px){.topbar{gap:8px}.logo{width:39px;height:39px}.top-actions{gap:6px}.connection{padding:8px 9px}.page-heading{display:block}.page-heading .outline-btn{margin-top:14px}.hero{padding:20px}.power-orb{flex-basis:78px;height:78px}.pc-grid{grid-template-columns:1fr}.relay-grid{grid-template-columns:1fr}.form-grid{grid-template-columns:1fr}.field.full{grid-column:auto}.gpio-table th,.gpio-table td{padding-left:5px;padding-right:5px}}
@media(prefers-reduced-motion:reduce){*,*::before,*::after{scroll-behavior:auto!important;transition:none!important;animation:none!important}.orb{transform:none!important}}
/* ---- ISUI Space scene (Ætherium + Galaxy Drift) ----
 * Effective FINAL style of reference/ISUI_SPACE_SCENE_REFERENCE.html (the
 * reference stacks several design passes; only the resulting style is
 * reproduced here). Scoped under html.scene-space so the classic glass
 * scene stays available via the Interface Settings toggle. */
html.scene-space{--bg:#03030b;--bg2:#080719;--panel:rgba(16,10,34,.34);--panel-strong:rgba(11,8,27,.62);--line:rgba(224,194,255,.19);--line-hi:rgba(255,215,255,.48);--text:#f5efff;--muted:#a5a0c6;--cyan:#70fff0;--violet:#bd72ff;--green:#a4ffd7;--amber:#ffe0a1;--red:#ff6da8;--shadow:0 28px 80px rgba(0,0,0,.56);background:#02030a}
html.scene-space body{background:radial-gradient(ellipse at 50% 55%,rgba(18,20,69,.12),transparent 50%),linear-gradient(180deg,#010209,#030514 65%,#010208)}
html.scene-space body:before,html.scene-space body:after{display:none}
html.scene-space #galaxyCanvas{position:fixed;inset:0;width:100%;height:100%;z-index:0;pointer-events:none;display:block;opacity:1}
html.scene-space #webglBackdrop{position:fixed;inset:0;width:100%;height:100%;z-index:2;pointer-events:none;display:block;opacity:.30;animation:alienPulse 12s ease-in-out infinite}
html.scene-space .ambient{z-index:2;opacity:.22}
html.scene-space .orb.one,html.scene-space .orb.two,html.scene-space .orb.three{opacity:.18;filter:blur(48px)}
html.scene-space .app{position:relative;z-index:3}
html.scene-space .glass,html.scene-space .action-card,html.scene-space .relay-card,html.scene-space .metric-panel{position:relative;z-index:3}
html.scene-space .glass{background:linear-gradient(145deg,rgba(19,23,48,.34),rgba(7,9,25,.24) 50%,rgba(13,8,32,.30));backdrop-filter:blur(15px) saturate(135%);-webkit-backdrop-filter:blur(15px) saturate(135%);border-color:rgba(191,211,255,.19)}
html.scene-space .glass:before{background:linear-gradient(125deg,rgba(255,255,255,.17),transparent 15%,transparent 54%,rgba(0,255,220,.065) 76%,rgba(250,76,255,.12)),radial-gradient(440px circle at var(--mx,30%) var(--my,0%),rgba(255,242,255,.18),transparent 46%)}
html.scene-space .action-card,html.scene-space .relay-card,html.scene-space .metric-panel{border-radius:19px;overflow:visible;isolation:isolate;transform-style:preserve-3d;transform:perspective(1250px) rotateX(8deg) rotateY(-1deg) translateZ(0);background:linear-gradient(145deg,rgba(25,32,59,.66),rgba(7,10,27,.80) 65%,rgba(10,7,29,.87));border-top:1px solid rgba(210,229,255,.46);border-left:1px solid rgba(228,204,255,.28);border-right:1px solid rgba(99,231,255,.42);border-bottom:1px solid rgba(180,105,255,.42);box-shadow:inset 0 2px 0 rgba(255,255,255,.22),inset 1px 0 0 rgba(255,245,255,.10),inset 0 -3px 0 rgba(0,0,0,.52),0 2px 0 rgba(0,255,227,.30),0 5px 0 rgba(36,19,71,.98),0 8px 0 rgba(13,8,34,.98),0 11px 0 rgba(5,4,20,.96),0 16px 0 rgba(2,2,12,.86),0 25px 30px rgba(0,0,0,.58),0 42px 60px rgba(0,0,0,.44),-8px 14px 36px rgba(0,255,220,.045),8px 18px 38px rgba(244,45,255,.045)}
html.scene-space .action-card:before,html.scene-space .relay-card:before,html.scene-space .metric-panel:before{content:"";position:absolute;inset:0;z-index:0;pointer-events:none;border-radius:inherit;background:linear-gradient(118deg,rgba(255,255,255,.24),rgba(255,255,255,.045) 5%,transparent 19%,transparent 66%,rgba(0,255,224,.10) 83%,rgba(239,63,255,.16)),radial-gradient(350px circle at var(--mx,18%) var(--my,5%),rgba(255,244,255,.18),transparent 54%)}
html.scene-space .action-card:after,html.scene-space .relay-card:after,html.scene-space .metric-panel:after{content:"";position:absolute;left:5px;right:-2px;bottom:-12px;height:14px;z-index:-1;border-radius:0 0 13px 13px;background:linear-gradient(180deg,rgba(53,43,94,.98),rgba(13,11,34,.99) 52%,rgba(2,2,11,.99));border:1px solid rgba(0,255,224,.22);border-top:1px solid rgba(225,163,255,.44);box-shadow:0 0 12px rgba(0,255,224,.10),0 9px 13px rgba(0,0,0,.4),0 17px 24px rgba(0,0,0,.36);transform:skewX(-1.5deg);pointer-events:none}
html.scene-space .action-card>*,html.scene-space .relay-card>*,html.scene-space .metric-panel>*{position:relative;z-index:1}
html.scene-space .action-card:hover,html.scene-space .relay-card:hover,html.scene-space .metric-panel:hover{transform:perspective(1250px) rotateX(3deg) rotateY(-1deg) translateY(-11px) translateZ(18px);border-top-color:rgba(255,244,255,.75);border-right-color:rgba(0,255,230,.68);border-bottom-color:rgba(240,95,255,.72);box-shadow:inset 0 2px 0 rgba(255,255,255,.30),0 2px 0 rgba(0,255,227,.42),0 6px 0 rgba(36,19,71,.98),0 10px 0 rgba(13,8,34,.98),0 14px 0 rgba(5,4,20,.96),0 19px 0 rgba(2,2,12,.86),0 32px 42px rgba(0,0,0,.64),0 50px 72px rgba(0,0,0,.48),-8px 0 35px rgba(0,255,220,.10),8px 0 35px rgba(244,45,255,.10)}
html.scene-space .hero{background:linear-gradient(145deg,rgba(218,249,255,.13),rgba(26,55,78,.22) 18%,rgba(7,19,35,.50) 62%,rgba(47,25,76,.24))}
html.scene-space .action-card .action-icon,html.scene-space .relay-card .relay-icon{background:linear-gradient(145deg,rgba(230,255,255,.20),rgba(28,33,76,.58));border:1px solid rgba(141,255,244,.44);color:#9dfff5;box-shadow:inset 0 1px 0 rgba(255,255,255,.35),0 3px 0 rgba(3,2,16,.9),0 9px 18px rgba(0,0,0,.38),0 0 22px rgba(0,255,220,.12)}
html.scene-space .run-btn{border-color:rgba(212,171,255,.38);background:linear-gradient(145deg,rgba(118,69,164,.48),rgba(24,13,48,.92));color:#e6d9ff}
html.scene-space .run-btn:hover{background:linear-gradient(145deg,rgba(148,92,196,.62),rgba(34,18,64,.95))}
html.scene-space .primary-btn{background:linear-gradient(135deg,#67ffe7,#8a8dff 55%,#e778ff);color:#0b0920;border:1px solid rgba(255,255,255,.65);box-shadow:0 0 20px rgba(0,255,221,.22),0 5px 0 rgba(31,18,65,.95),0 12px 24px rgba(0,0,0,.38)}
html.scene-space .soft-btn,html.scene-space .outline-btn{border-color:rgba(201,153,255,.35);background:linear-gradient(145deg,rgba(95,57,132,.28),rgba(11,9,30,.7));color:#eadcff}
html.scene-space .relay-actions button{border-color:rgba(212,171,255,.30);background:linear-gradient(145deg,rgba(118,69,164,.36),rgba(24,13,48,.85));color:#dfd2f7}
html.scene-space .relay-actions .on-btn{color:#b6ffe9;border-color:rgba(0,255,224,.38);background:linear-gradient(145deg,rgba(24,120,110,.36),rgba(9,32,40,.85))}
html.scene-space .relay-actions .on-btn:hover{background:linear-gradient(145deg,rgba(32,158,142,.5),rgba(12,44,54,.9))}
html.scene-space .status-chip,html.scene-space .connection,html.scene-space .count{border-color:rgba(203,157,255,.24);background:rgba(16,9,36,.52)}
html.scene-space .dot{background:#86ffe2;box-shadow:0 0 12px rgba(0,255,220,.8)}
html.scene-space .connection.warn .dot{background:var(--amber);box-shadow:0 0 12px rgba(255,224,161,.6)}
html.scene-space .connection.bad .dot{background:var(--red);box-shadow:0 0 12px rgba(255,109,168,.6)}
html.scene-space .switch{box-shadow:0 4px 0 rgba(0,0,0,.42),inset 0 1px 0 rgba(255,255,255,.2),0 8px 16px rgba(0,0,0,.25)}
html.scene-space .power-orb{isolation:isolate;overflow:visible;background:radial-gradient(circle at 28% 18%,rgba(255,255,255,.6),rgba(0,255,225,.2) 12%,rgba(99,54,180,.30) 35%,rgba(6,5,23,.92) 72%);border-color:rgba(173,255,247,.65);box-shadow:inset 0 3px 8px rgba(255,255,255,.34),inset 0 -24px 32px rgba(0,0,0,.62),0 0 35px rgba(0,255,220,.25),0 0 90px rgba(213,50,255,.16),0 28px 36px rgba(0,0,0,.46)}
html.scene-space #webglPowerOrb{position:absolute;inset:-38%;width:176%;height:176%;z-index:2;pointer-events:none;display:block}
html.scene-space .power-orb .power-fallback{position:relative;z-index:3}
html.scene-space.orb-gl .power-orb .power-fallback{opacity:.18;filter:drop-shadow(0 0 10px rgba(103,232,249,.8))}
@media (pointer:fine) and (prefers-reduced-motion:no-preference){
html.scene-space .glass[data-tilt]{transform:perspective(1100px) rotateX(var(--tilt-y,0deg)) rotateY(var(--tilt-x,0deg)) translateZ(0);transition:transform .16s ease-out,box-shadow .25s ease,border-color .25s ease;will-change:transform}
html.scene-space .glass[data-tilt]>*:not(.sheen){transform:translateZ(18px)}
html.scene-space .glass[data-tilt] .card-top{transform:translateZ(26px)}
html.scene-space .glass[data-tilt] .power-orb{transform:translateZ(46px);filter:drop-shadow(0 22px 22px rgba(0,0,0,.25))}
html.scene-space .glass[data-tilt] .action-icon,html.scene-space .glass[data-tilt] .relay-icon{transform:translateZ(34px)}
html.scene-space .glass[data-tilt] .run-btn,html.scene-space .glass[data-tilt] button{transform:translateZ(28px)}
html.scene-space .action-card[data-tilt]:hover,html.scene-space .relay-card[data-tilt]:hover{box-shadow:0 38px 75px rgba(0,0,0,.45),0 0 36px rgba(83,220,255,.12),inset 0 1px 0 rgba(255,255,255,.2)}
}
@keyframes alienPulse{0%,100%{filter:hue-rotate(0deg) brightness(1)}50%{filter:hue-rotate(18deg) brightness(1.09)}}
@media(max-width:760px),(prefers-reduced-motion:reduce){html.scene-space #webglBackdrop{animation:none}}
html.scene-space .planetary-scene{position:fixed;inset:0;z-index:1;pointer-events:none;overflow:hidden;perspective:1200px}
html.scene-space .planet{position:absolute;border-radius:50%;transform-style:preserve-3d}
html.scene-space .planet-giant{width:clamp(330px,42vw,690px);height:clamp(330px,42vw,690px);left:-10vw;top:17vh;opacity:.84;background:radial-gradient(ellipse at 27% 28%,rgba(240,219,255,.82) 0%,rgba(133,105,205,.55) 5%,transparent 18%),radial-gradient(ellipse at 30% 34%,#554a91 0%,#292e69 24%,#171a43 48%,#080d26 72%,#020611 100%);box-shadow:inset -35px -22px 68px rgba(0,0,0,.88),inset 12px 8px 30px rgba(205,187,255,.18),0 0 38px rgba(106,104,255,.13),0 0 100px rgba(82,70,255,.12);animation:planetDrift 34s ease-in-out infinite}
html.scene-space .planet-giant:before{content:"";position:absolute;inset:-2%;border-radius:50%;background:radial-gradient(circle at 25% 30%,transparent 45%,rgba(132,116,255,.15) 63%,rgba(94,81,255,.06) 70%,transparent 74%);box-shadow:0 0 22px rgba(138,125,255,.30),0 0 55px rgba(93,117,255,.18)}
html.scene-space .planet-atmosphere{position:absolute;inset:-4%;border-radius:50%;border:1px solid rgba(153,165,255,.17);box-shadow:0 0 35px rgba(114,121,255,.12),inset 0 0 20px rgba(112,120,255,.10)}
html.scene-space .planet-clouds{position:absolute;inset:0;border-radius:50%;overflow:hidden;opacity:.68;background:repeating-linear-gradient(171deg,transparent 0 17px,rgba(190,160,255,.10) 20px,rgba(47,60,133,.22) 27px,transparent 39px,transparent 57px);-webkit-mask-image:radial-gradient(ellipse at 40% 40%,black 25%,transparent 72%);mask-image:radial-gradient(ellipse at 40% 40%,black 25%,transparent 72%);mix-blend-mode:screen}
html.scene-space .planet-ringed{width:clamp(145px,18vw,260px);height:clamp(145px,18vw,260px);right:7vw;top:13vh;transform:rotateX(68deg) rotateZ(-20deg);animation:ringPlanetFloat 25s ease-in-out infinite}
html.scene-space .planet-body{position:absolute;inset:15%;border-radius:50%;z-index:2;transform:rotateX(-68deg);background:radial-gradient(ellipse at 30% 22%,rgba(255,242,213,.88),transparent 13%),repeating-linear-gradient(165deg,rgba(255,211,153,.30) 0 4px,rgba(79,45,112,.16) 6px 11px,rgba(31,37,82,.45) 14px 20px),radial-gradient(circle at 33% 28%,#9b80b9,#40305e 42%,#10132e 75%);box-shadow:inset -22px -12px 30px rgba(0,0,0,.9),inset 8px 5px 18px rgba(255,233,202,.22),0 0 28px rgba(195,115,255,.20)}
html.scene-space .planet-ring{position:absolute;inset:-10% -37%;border-radius:50%;z-index:1;border:clamp(8px,1.4vw,18px) solid rgba(165,140,255,.25);transform:rotateX(66deg) rotateZ(-16deg);box-shadow:0 0 8px rgba(177,149,255,.25),inset 0 0 8px rgba(248,199,255,.18);background:linear-gradient(90deg,transparent,rgba(107,231,255,.18),rgba(240,173,255,.28),rgba(178,151,255,.12),transparent)}
html.scene-space .planet-ring:after{content:"";position:absolute;inset:-14px -8px;border-radius:50%;border:2px solid rgba(205,187,255,.14)}
html.scene-space .planet-glow{position:absolute;inset:-25%;border-radius:50%;z-index:0;background:radial-gradient(circle,rgba(173,100,255,.15),transparent 65%);filter:blur(16px)}
html.scene-space .planet-moon{width:clamp(35px,5vw,68px);height:clamp(35px,5vw,68px);left:61%;top:23%;background:radial-gradient(circle at 28% 25%,#e6e2ff,#777da7 22%,#303655 57%,#0a1022 80%);box-shadow:inset -10px -8px 16px rgba(0,0,0,.85),0 0 16px rgba(154,178,255,.22);animation:moonOrbit 36s linear infinite}
html.scene-space .planet-distant{width:clamp(60px,8vw,112px);height:clamp(60px,8vw,112px);right:25%;bottom:12%;background:radial-gradient(circle at 30% 25%,#e8d8ff,#7961b8 24%,#29224f 60%,#08091a 85%);box-shadow:inset -14px -10px 20px rgba(0,0,0,.85),0 0 28px rgba(190,94,255,.18);opacity:.72;animation:farPlanet 42s ease-in-out infinite}
html.scene-space .space-streaks{position:absolute;inset:0;opacity:.42;background:radial-gradient(ellipse at 76% 46%,rgba(76,255,229,.12),transparent 20%),radial-gradient(ellipse at 66% 70%,rgba(153,72,255,.13),transparent 25%);filter:blur(22px)}
@keyframes planetDrift{0%,100%{transform:translate3d(0,0,-20px)}50%{transform:translate3d(28px,-15px,10px)}}
@keyframes ringPlanetFloat{0%,100%{translate:0 0;rotate:0deg}50%{translate:-14px 18px;rotate:5deg}}
@keyframes moonOrbit{0%{transform:translate3d(0,0,0)}50%{transform:translate3d(55px,35px,20px)}100%{transform:translate3d(0,0,0)}}
@keyframes farPlanet{0%,100%{transform:translate3d(0,0,0)}50%{transform:translate3d(-25px,-18px,0)}}
html.scene-space .action-card,html.scene-space .relay-card,html.scene-space .metric-panel{outline:1px solid rgba(117,205,237,.055);outline-offset:3px}
@media(max-width:760px){html.scene-space .planet-giant{left:-210px;top:20vh;opacity:.5}html.scene-space .planet-ringed{right:-65px;top:11vh;opacity:.65}html.scene-space .planet-moon{left:65%;top:28%}html.scene-space .planet-distant{right:7%;bottom:8%;opacity:.45}}
@media(max-width:760px),(prefers-reduced-motion:reduce){html.scene-space .action-card,html.scene-space .relay-card,html.scene-space .metric-panel,html.scene-space .hero{transform:none;box-shadow:0 2px 0 rgba(255,255,255,.2) inset,0 5px 0 rgba(8,20,34,.9),0 12px 24px rgba(0,0,0,.3)}html.scene-space .action-card:after,html.scene-space .relay-card:after,html.scene-space .metric-panel:after{height:8px;bottom:-7px}}
body.no-blur .glass,body.no-blur .sidebar,body.no-blur .modal,body.no-blur .toast{backdrop-filter:none!important;-webkit-backdrop-filter:none!important}
)CSSCSS";

/* Shared ISUI helpers: toast, ambient orbs, card sheen, UI preferences.
 * Loaded by send_ui_page() BEFORE each page's own script, so pages can use
 * window.isuiToast() / the preference helpers from their first frame. */
static const char PCCTL_UI_JS[] = R"JSJS(
'use strict';
window.isuiToast = function(message, isErr){
  var el = document.getElementById('isuiToast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'isuiToast';
    el.className = 'toast';
    el.setAttribute('role', 'status');
    el.setAttribute('aria-live', 'polite');
    document.body.appendChild(el);
  }
  el.textContent = message;
  el.className = 'toast show' + (isErr ? ' err' : '');
  clearTimeout(window.isuiToast._t);
  window.isuiToast._t = setTimeout(function(){ el.className = 'toast'; }, 3400);
};
(function(){
  var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var fine = window.matchMedia('(pointer: fine)').matches;
  var prefs = { blur: true, parallax: !reduced };
  try {
    var b = localStorage.getItem('pcctl_ui_blur');
    var p = localStorage.getItem('pcctl_ui_parallax');
    if (b !== null) prefs.blur = (b === '1');
    if (p !== null) prefs.parallax = (p === '1') && !reduced;
  } catch (e) {}
  function applyBlur(){ document.body.classList.toggle('no-blur', !prefs.blur); }
  window.isuiGetPrefs = function(){ return { blur: prefs.blur, parallax: prefs.parallax, reduced: reduced }; };
  window.isuiSetBlur = function(on){
    prefs.blur = !!on;
    try { localStorage.setItem('pcctl_ui_blur', prefs.blur ? '1' : '0'); } catch (e) {}
    applyBlur();
  };
  window.isuiSetParallax = function(on){
    prefs.parallax = !!on && !reduced;
    try { localStorage.setItem('pcctl_ui_parallax', prefs.parallax ? '1' : '0'); } catch (e) {}
    if (!prefs.parallax) {
      var orbs = document.querySelectorAll('.orb');
      for (var i = 0; i < orbs.length; i++) orbs[i].style.transform = '';
    }
  };
  applyBlur();
  if (fine) {
    document.addEventListener('pointermove', function(e){
      var t = e.target;
      var card = (t && t.closest) ? t.closest('.glass') : null;
      if (!card) return;
      if (!card.querySelector(':scope > .sheen')) {
        var s = document.createElement('span');
        s.className = 'sheen';
        s.setAttribute('aria-hidden', 'true');
        card.prepend(s);
      }
      var rect = card.getBoundingClientRect();
      card.style.setProperty('--mx', (e.clientX - rect.left) + 'px');
      card.style.setProperty('--my', (e.clientY - rect.top) + 'px');
    }, { passive: true });
  }
  if (fine && !reduced) {
    var scheduled = false, lastX = 0, lastY = 0;
    document.addEventListener('pointermove', function(e){
      if (!prefs.parallax || scheduled) return;
      lastX = (e.clientX / window.innerWidth - .5);
      lastY = (e.clientY / window.innerHeight - .5);
      scheduled = true;
      requestAnimationFrame(function(){
        var o = document.querySelectorAll('.orb');
        if (o[0]) o[0].style.transform = 'translate3d(' + (lastX * -13) + 'px,' + (lastY * -10) + 'px,0)';
        if (o[1]) o[1].style.transform = 'translate3d(' + (lastX * 17) + 'px,' + (lastY * 12) + 'px,0)';
        if (o[2]) o[2].style.transform = 'translate3d(' + (lastX * -8) + 'px,' + (lastY * -14) + 'px,0)';
        scheduled = false;
      });
    }, { passive: true });
  }
})();
)JSJS";

/* Space-scene JS (ported from ISUI_SPACE_SCENE_REFERENCE.html). Served by
 * send_ui_page() after PCCTL_UI_JS: galaxy nebula (WebGL shader + 2D
 * starfield fallback), CSS planetary scene injection, WebGL prism backdrop
 * and power-orb sphere/rings, pointer tilt. Real device APIs are untouched;
 * demo behavior from the reference is not copied. Scene toggle (Space /
 * Glass classic) is stored in localStorage "pcctl_ui_scene". */
static const char PCCTL_UI_SPACE_JS[] = R"SPJS(
'use strict';
(function(){
var html = document.documentElement;
var mods = [];
var running = false;
window.__isuiSpaceAdd = function(mod){ mods.push(mod); };
window.isuiGetScene = function(){
  try { var s = localStorage.getItem('pcctl_ui_scene'); if (s === 'glass') return 'glass'; } catch (e) {}
  return 'space';
};
window.isuiSetScene = function(name){
  var scene = (name === 'glass') ? 'glass' : 'space';
  try { localStorage.setItem('pcctl_ui_scene', scene); } catch (e) {}
  apply(scene);
};
function buildDom(){
  if (document.getElementById('galaxyCanvas')) return;
  var gc = document.createElement('canvas');
  gc.id = 'galaxyCanvas';
  gc.setAttribute('aria-hidden', 'true');
  document.body.appendChild(gc);
  var ps = document.createElement('div');
  ps.className = 'planetary-scene';
  ps.setAttribute('aria-hidden', 'true');
  ps.innerHTML = '<div class="planet planet-giant"><div class="planet-atmosphere"></div><div class="planet-clouds"></div></div>' +
    '<div class="planet planet-ringed"><div class="planet-ring"></div><div class="planet-body"></div><div class="planet-glow"></div></div>' +
    '<div class="planet planet-moon"></div><div class="planet planet-distant"></div><div class="space-streaks"></div>';
  document.body.appendChild(ps);
  var wb = document.createElement('canvas');
  wb.id = 'webglBackdrop';
  wb.setAttribute('aria-hidden', 'true');
  document.body.appendChild(wb);
  var orb = document.querySelector('.power-orb');
  if (orb && !document.getElementById('webglPowerOrb')) {
    var oc = document.createElement('canvas');
    oc.id = 'webglPowerOrb';
    oc.setAttribute('aria-hidden', 'true');
    orb.appendChild(oc);
  }
}
function removeDom(){
  ['galaxyCanvas', 'webglBackdrop', 'webglPowerOrb'].forEach(function(id){
    var el = document.getElementById(id);
    if (el && el.parentNode) el.parentNode.removeChild(el);
  });
  var ps = document.querySelector('.planetary-scene');
  if (ps && ps.parentNode) ps.parentNode.removeChild(ps);
}
function pauseAll(hidden){
  for (var i = 0; i < mods.length; i++) { if (mods[i].visibility) mods[i].visibility(hidden); }
}
function apply(scene){
  var isSpace = (scene === 'space');
  html.classList.toggle('scene-space', isSpace);
  if (isSpace) {
    buildDom();
    if (!running) {
      running = true;
      for (var i = 0; i < mods.length; i++) { try { mods[i].start(); } catch (e) {} }
    }
  } else {
    if (running) {
      running = false;
      for (var j = 0; j < mods.length; j++) { try { mods[j].stop(); } catch (e) {} }
    }
    removeDom();
    html.classList.remove('webgl-ready', 'webgl-fallback', 'orb-gl');
  }
}
window.__isuiSpaceBoot = function(){
  document.addEventListener('visibilitychange', function(){ pauseAll(document.hidden); });
  apply(window.isuiGetScene());
};
})();
/* Pointer-driven 3D tilt (CSS owns rendering; JS only updates transform values). */
(function(){
'use strict';
var fine = window.matchMedia('(pointer:fine)').matches;
var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
var sel = '.glass.hero, .glass.metric-panel, .glass.action-card, .glass.relay-card, .glass.lower-card';
var on = false, raf = 0, target = null, px = 0, py = 0;
function update(){
  raf = 0;
  var t = target;
  if (!t) return;
  var r = t.getBoundingClientRect();
  var nx = Math.max(-1, Math.min(1, (px - r.left) / r.width * 2 - 1));
  var ny = Math.max(-1, Math.min(1, (py - r.top) / r.height * 2 - 1));
  var strength = t.classList.contains('hero') ? 8 : 6.5;
  t.style.setProperty('--tilt-x', (nx * strength) + 'deg');
  t.style.setProperty('--tilt-y', (ny * -strength) + 'deg');
  t.style.setProperty('--mx', (px - r.left) + 'px');
  t.style.setProperty('--my', (py - r.top) + 'px');
}
function onMove(e){
  var card = (e.target && e.target.closest) ? e.target.closest(sel) : null;
  if (target && target !== card) {
    target.style.setProperty('--tilt-x', '0deg');
    target.style.setProperty('--tilt-y', '0deg');
  }
  target = card;
  if (!card) return;
  if (!card.hasAttribute('data-tilt')) card.setAttribute('data-tilt', '');
  px = e.clientX; py = e.clientY;
  if (!raf) raf = requestAnimationFrame(update);
}
function reset(){
  if (!target) return;
  target.style.setProperty('--tilt-x', '0deg');
  target.style.setProperty('--tilt-y', '0deg');
  target = null;
}
function onOut(e){ if (target && !target.contains(e.relatedTarget)) reset(); }
window.__isuiSpaceAdd({
  start: function(){
    if (on || !fine || reduced) return;
    on = true;
    document.addEventListener('pointermove', onMove, { passive: true });
    document.addEventListener('pointerleave', reset);
    document.addEventListener('pointerout', onOut);
  },
  stop: function(){
    if (!on) return;
    on = false;
    document.removeEventListener('pointermove', onMove);
    document.removeEventListener('pointerleave', reset);
    document.removeEventListener('pointerout', onOut);
    if (raf) { cancelAnimationFrame(raf); raf = 0; }
    reset();
    var cards = document.querySelectorAll(sel);
    for (var i = 0; i < cards.length; i++) cards[i].removeAttribute('data-tilt');
  }
});
})();
/* GALAXY DRIFT: procedural nebula + stars (WebGL1 fragment shader, ported
 * from the reference) with a lightweight 2D-canvas starfield fallback. */
(function(){
'use strict';
var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
var canvas = null, ctx2d = null, gl = null, prog = null;
var uRes = null, uTime = null, uMouse = null, uMotion = null;
var stars = null, raf = 0, t0 = 0, mx = 0, my = 0, started = false, paused = false;
var VS = 'attribute vec2 aPos;varying vec2 vUv;void main(){vUv=aPos*.5+.5;gl_Position=vec4(aPos,0.,1.);}';
var FS = `precision highp float;
varying vec2 vUv;
uniform vec2 uRes;
uniform float uTime;
uniform vec2 uMouse;
uniform float uMotion;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123);}
float noise(vec2 p){
  vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
  return mix(mix(hash(i),hash(i+vec2(1.,0.)),f.x),
             mix(hash(i+vec2(0.,1.)),hash(i+vec2(1.,1.)),f.x),f.y);
}
float fbm(vec2 p){
  float v=0.,a=.5;
  for(int i=0;i<5;i++){v+=noise(p)*a;p=p*2.03+vec2(17.1,9.2);a*=.5;}
  return v;
}
void main(){
  vec2 uv=(gl_FragCoord.xy-.5*uRes)/uRes.y;
  vec2 p=uv;
  float t=uTime;
  p += uMouse*vec2(.13,.09);
  float neb1=fbm(p*2.15+vec2(t*.012,-t*.018));
  float neb2=fbm(p*3.8+vec2(-t*.009,t*.014)+neb1*.55);
  float cloud=smoothstep(.43,.82,neb1*.68+neb2*.42);
  vec3 col=vec3(.006,.008,.025);
  col+=vec3(.018,.045,.16)*cloud;
  col+=vec3(.055,.012,.13)*smoothstep(.54,.85,fbm(p*2.5+vec2(.8,-.3)+t*.008));
  col+=vec3(.005,.12,.17)*smoothstep(.61,.88,fbm(p*4.4+vec2(-.6,.7)-t*.006));
  float band=exp(-pow((p.y-(.18*sin(p.x*1.35+t*.025)+.06*sin(p.x*3.0-t*.018)))*3.6,2.));
  float bandNoise=fbm(p*vec2(3.2,7.5)+vec2(t*.008,0.));
  col+=vec3(.10,.14,.42)*band*bandNoise*.60;
  col+=vec3(.19,.035,.28)*band*bandNoise*.23;
  vec2 grid1=gl_FragCoord.xy/vec2(3.4,3.4);
  vec2 cell1=floor(grid1),f1=fract(grid1);
  float h1=hash(cell1);
  vec2 star1=vec2(hash(cell1+13.7),hash(cell1+41.3));
  float d1=length(f1-star1);
  float s1=smoothstep(.055,.0,d1)*step(.992,h1);
  vec2 grid2=gl_FragCoord.xy/vec2(13.,13.);
  vec2 cell2=floor(grid2),f2=fract(grid2);
  float h2=hash(cell2+77.7);
  vec2 star2=vec2(hash(cell2+8.2),hash(cell2+23.9));
  float d2=length(f2-star2);
  float s2=smoothstep(.12,.015,d2)*step(.986,h2);
  float twinkle=.76+.24*sin(t*1.7+h1*40.);
  col+=vec3(.32,.48,.95)*s1*1.2;
  col+=vec3(.62,.82,1.)*s2*twinkle*1.5;
  float bright=step(.9975,hash(floor(gl_FragCoord.xy/vec2(41.))));
  vec2 sf=fract(gl_FragCoord.xy/41.)-.5;
  float spike=exp(-abs(sf.x)*100.)+exp(-abs(sf.y)*100.);
  col+=vec3(.34,.72,1.)*bright*spike*.38;
  vec2 center=uRes*.5;
  vec2 delta=gl_FragCoord.xy-center;
  float radial=length(delta)/uRes.y;
  float rays=0.;
  if(uMotion>.5){
    float phase=fract(radial*8.0-t*.045);
    rays=smoothstep(.015,0.,abs(phase-.5)-.48)*smoothstep(.05,.7,radial)*.10;
  }
  col+=vec3(.10,.27,.55)*rays;
  float vignette=1.-smoothstep(.35,1.15,length(uv));
  col*=.58+.42*vignette;
  gl_FragColor=vec4(col,1.);
}`;
function initGL(){
  gl = canvas.getContext('webgl', { alpha: true, antialias: true, premultipliedAlpha: false });
  if (!gl) return false;
  function sh(type, src){
    var s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { return null; }
    return s;
  }
  var v = sh(gl.VERTEX_SHADER, VS), f = sh(gl.FRAGMENT_SHADER, FS);
  if (!v || !f) return false;
  prog = gl.createProgram();
  gl.attachShader(prog, v); gl.attachShader(prog, f); gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) return false;
  gl.useProgram(prog);
  var buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1,1,-1,-1,1,1,1]), gl.STATIC_DRAW);
  var pos = gl.getAttribLocation(prog, 'aPos');
  gl.enableVertexAttribArray(pos);
  gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);
  uRes = gl.getUniformLocation(prog, 'uRes');
  uTime = gl.getUniformLocation(prog, 'uTime');
  uMouse = gl.getUniformLocation(prog, 'uMouse');
  uMotion = gl.getUniformLocation(prog, 'uMotion');
  return true;
}
function drawGL(now){
  var dpr = Math.min(window.devicePixelRatio || 1, 1.5);
  var w = Math.max(1, Math.floor(window.innerWidth * dpr));
  var h = Math.max(1, Math.floor(window.innerHeight * dpr));
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  gl.viewport(0, 0, w, h);
  gl.disable(gl.DEPTH_TEST); gl.disable(gl.BLEND);
  gl.uniform2f(uRes, w, h);
  gl.uniform1f(uTime, reduced ? 0 : (now - t0) / 1000);
  gl.uniform2f(uMouse, mx, my);
  gl.uniform1f(uMotion, reduced ? 0 : 1);
  gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}
function init2D(){
  ctx2d = canvas.getContext('2d');
  if (!ctx2d) return false;
  stars = [];
  for (var i = 0; i < 110; i++) {
    stars.push({
      x: Math.random(), y: Math.random(),
      r: .4 + Math.random() * 1.5,
      p: Math.random() * 6.28, s: .5 + Math.random() * 1.6,
      tint: (i % 9 === 0) ? '140,255,230' : ((i % 13 === 0) ? '190,165,255' : '205,225,255')
    });
  }
  return true;
}
function draw2D(now){
  var dpr = Math.min(window.devicePixelRatio || 1, 1.5);
  var w = Math.max(1, Math.floor(window.innerWidth * dpr));
  var h = Math.max(1, Math.floor(window.innerHeight * dpr));
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  ctx2d.setTransform(1, 0, 0, 1, 0, 0);
  ctx2d.clearRect(0, 0, w, h);
  var t = reduced ? 0 : (now - t0) / 1000;
  for (var i = 0; i < stars.length; i++) {
    var st = stars[i];
    var tw = reduced ? .8 : (.45 + .55 * (.5 + .5 * Math.sin(t * st.s + st.p)));
    var x = st.x * w + mx * 14 * st.r;
    var y = st.y * h + my * 10 * st.r;
    ctx2d.beginPath();
    ctx2d.arc(x, y, st.r * dpr, 0, 6.2832);
    ctx2d.fillStyle = 'rgba(' + st.tint + ',' + (tw * .85).toFixed(3) + ')';
    ctx2d.fill();
  }
}
function onMouse(e){
  mx = (e.clientX / window.innerWidth - .5) * 2;
  my = (e.clientY / window.innerHeight - .5) * 2;
}
function frame(now){
  if (gl) drawGL(now); else draw2D(now);
  raf = requestAnimationFrame(frame);
}
window.__isuiSpaceAdd({
  start: function(){
    if (started) return;
    canvas = document.getElementById('galaxyCanvas');
    if (!canvas) return;
    gl = null; ctx2d = null; prog = null;
    var ok = false;
    try { ok = initGL(); } catch (e) { ok = false; }
    if (!ok) { gl = null; try { ok = init2D(); } catch (e2) { ok = false; } }
    if (!ok) { canvas = null; return; }
    started = true;
    t0 = performance.now();
    if (window.matchMedia('(pointer:fine)').matches) {
      document.addEventListener('pointermove', onMouse, { passive: true });
    }
    if (reduced) {
      if (gl) drawGL(t0); else draw2D(t0);
    } else {
      raf = requestAnimationFrame(frame);
    }
  },
  visibility: function(hidden){
    if (!started) return;
    paused = hidden;
    if (hidden) {
      if (raf) { cancelAnimationFrame(raf); raf = 0; }
    } else if (!reduced && !raf) {
      raf = requestAnimationFrame(frame);
    }
  },
  stop: function(){
    if (!started) return;
    started = false; paused = false;
    if (raf) { cancelAnimationFrame(raf); raf = 0; }
    document.removeEventListener('pointermove', onMouse);
    if (gl) {
      var ext = gl.getExtension('WEBGL_lose_context');
      if (ext) { try { ext.loseContext(); } catch (e) {} }
    }
    gl = null; ctx2d = null; stars = null; prog = null; canvas = null;
  }
});
})();
/* Self-contained WebGL1 renderer (no CDN, no external libraries): floating
 * prism solids behind the dashboard plus the hero power-orb sphere/rings.
 * Ported from the reference; each canvas fails independently. */
(function(){
'use strict';
var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
var finePointer = window.matchMedia('(pointer:fine)').matches;
var backdrop = null, orbCanvas = null;
var back = null, orb = null;
var started = false, raf = 0, last = 0, mouseX = 0, mouseY = 0;
var vertexShader = `
  attribute vec3 aPosition;
  attribute vec3 aNormal;
  uniform mat4 uMVP;
  uniform mat4 uModel;
  varying vec3 vNormal;
  varying vec3 vWorld;
  varying vec3 vLocal;
  void main(){
    vec4 world=uModel*vec4(aPosition,1.0);
    vWorld=world.xyz;
    vLocal=aPosition;
    vNormal=normalize(mat3(uModel)*aNormal);
    gl_Position=uMVP*vec4(aPosition,1.0);
  }`;
var fragmentShader = `
  precision mediump float;
  varying vec3 vNormal;
  varying vec3 vWorld;
  varying vec3 vLocal;
  uniform vec3 uColor;
  uniform vec3 uCamera;
  uniform float uGlass;
  uniform float uTime;
  void main(){
    vec3 N=normalize(vNormal);
    vec3 V=normalize(uCamera-vWorld);
    vec3 L1=normalize(vec3(-0.65,0.85,0.9));
    vec3 L2=normalize(vec3(0.8,0.15,-0.6));
    float diff=max(dot(N,L1),0.0);
    float diff2=max(dot(N,L2),0.0);
    float fres=pow(1.0-max(dot(N,V),0.0),3.0);
    vec3 H=normalize(L1+V);
    float spec=pow(max(dot(N,H),0.0),48.0);
    float rim=pow(1.0-abs(dot(N,V)),2.2);
    vec3 base=uColor*(0.20+0.68*diff+0.18*diff2);
    vec3 col=base+vec3(0.35,0.9,1.0)*(spec*1.25+fres*0.7);
    col+=vec3(0.42,0.24,1.0)*(pow(max(dot(N,L2),0.0),4.0)*0.35);
    if(uGlass>0.5){
      col=mix(col,vec3(0.22,0.78,0.94),0.2+fres*0.42);
      col+=vec3(0.25,0.72,1.0)*rim*0.32;
    }
    gl_FragColor=vec4(col,0.60+fres*0.36+spec*0.2);
  }`;
function compile(gl, type, source){
  var s = gl.createShader(type);
  gl.shaderSource(s, source);
  gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { return null; }
  return s;
}
function createRenderer(canvas){
  var gl = canvas.getContext('webgl', { alpha: true, antialias: true, premultipliedAlpha: false, preserveDrawingBuffer: false });
  if (!gl) return null;
  var vs = compile(gl, gl.VERTEX_SHADER, vertexShader);
  var fs = compile(gl, gl.FRAGMENT_SHADER, fragmentShader);
  if (!vs || !fs) return null;
  var program = gl.createProgram();
  gl.attachShader(program, vs); gl.attachShader(program, fs); gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) { return null; }
  gl.useProgram(program);
  var loc = {
    pos: gl.getAttribLocation(program, 'aPosition'),
    normal: gl.getAttribLocation(program, 'aNormal'),
    mvp: gl.getUniformLocation(program, 'uMVP'),
    model: gl.getUniformLocation(program, 'uModel'),
    color: gl.getUniformLocation(program, 'uColor'),
    camera: gl.getUniformLocation(program, 'uCamera'),
    glass: gl.getUniformLocation(program, 'uGlass'),
    time: gl.getUniformLocation(program, 'uTime')
  };
  gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL);
  gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  function mesh(vertices, indices){
    var vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
    var ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(indices), gl.STATIC_DRAW);
    return { vb: vb, ib: ib, count: indices.length };
  }
  function sphere(rows, cols){
    rows = rows || 36; cols = cols || 52;
    var v = [], idx = [];
    for (var y = 0; y <= rows; y++) {
      var lat = y * Math.PI / rows;
      for (var x = 0; x <= cols; x++) {
        var lon = x * 2 * Math.PI / cols;
        var nx = Math.sin(lat) * Math.cos(lon), ny = Math.cos(lat), nz = Math.sin(lat) * Math.sin(lon);
        v.push(nx, ny, nz, nx, ny, nz);
      }
    }
    for (var yy = 0; yy < rows; yy++) for (var xx = 0; xx < cols; xx++) {
      var a = yy * (cols + 1) + xx, b = a + cols + 1;
      idx.push(a, b, a + 1, b, b + 1, a + 1);
    }
    return mesh(v, idx);
  }
  function torus(R, r, segments, sides){
    R = R || 1; r = r || .035; segments = segments || 96; sides = sides || 10;
    var v = [], idx = [];
    for (var i = 0; i <= segments; i++) {
      var u = i * 2 * Math.PI / segments;
      for (var j = 0; j <= sides; j++) {
        var w = j * 2 * Math.PI / sides, c = Math.cos(w), s = Math.sin(w), rad = R + r * c;
        v.push(rad * Math.cos(u), rad * Math.sin(u), r * s, c * Math.cos(u), c * Math.sin(u), s);
      }
    }
    for (var ii = 0; ii < segments; ii++) for (var jj = 0; jj < sides; jj++) {
      var a2 = ii * (sides + 1) + jj, b2 = a2 + sides + 1;
      idx.push(a2, b2, a2 + 1, b2, b2 + 1, a2 + 1);
    }
    return mesh(v, idx);
  }
  function cube(){
    var v = [], idx = [];
    var faces = [
      [[0,0,1],[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]],
      [[0,0,-1],[.5,-.5,-.5],[-.5,-.5,-.5],[-.5,.5,-.5],[.5,.5,-.5]],
      [[1,0,0],[.5,-.5,.5],[.5,-.5,-.5],[.5,.5,-.5],[.5,.5,.5]],
      [[-1,0,0],[-.5,-.5,-.5],[-.5,-.5,.5],[-.5,.5,.5],[-.5,.5,-.5]],
      [[0,1,0],[-.5,.5,.5],[.5,.5,.5],[.5,.5,-.5],[-.5,.5,-.5]],
      [[0,-1,0],[-.5,-.5,-.5],[.5,-.5,-.5],[.5,-.5,.5],[-.5,-.5,.5]]
    ];
    for (var fi = 0; fi < faces.length; fi++) {
      var f = faces[fi];
      var base = v.length / 6;
      for (var k = 1; k <= 4; k++) v.push(f[k][0], f[k][1], f[k][2], f[0][0], f[0][1], f[0][2]);
      idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }
    return mesh(v, idx);
  }
  return { gl: gl, loc: loc, program: program, mesh: { sphere: sphere(), torus: torus(), cube: cube() } };
}
/* Column-major 4x4 math helpers (ported). */
var I = function(){ return new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]); };
function mul(a, b){
  var o = new Float32Array(16);
  for (var c = 0; c < 4; c++) for (var r = 0; r < 4; r++)
    o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  return o;
}
function translate(x, y, z){ var m = I(); m[12] = x; m[13] = y; m[14] = z; return m; }
function scaleM(x, y, z){ var m = I(); m[0] = x; m[5] = y; m[10] = z; return m; }
function rx(a){ var c = Math.cos(a), s = Math.sin(a), m = I(); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; return m; }
function ry(a){ var c = Math.cos(a), s = Math.sin(a), m = I(); m[0] = c; m[2] = -s; m[8] = s; m[10] = c; return m; }
function rz(a){ var c = Math.cos(a), s = Math.sin(a), m = I(); m[0] = c; m[1] = s; m[4] = -s; m[5] = c; return m; }
function perspective(fov, aspect, near, far){
  var f = 1 / Math.tan(fov / 2), nf = 1 / (near - far), m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f; m[10] = (far + near) * nf; m[11] = -1; m[14] = 2 * far * near * nf;
  return m;
}
function lookAt(eye, center, up){
  function norm(v){ var l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }
  function sub(a, b){ return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
  function cross(a, b){ return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function dot(a, b){ return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  var z = norm(sub(eye, center)), x = norm(cross(up, z)), y = cross(z, x), m = I();
  m[0] = x[0]; m[1] = y[0]; m[2] = z[0];
  m[4] = x[1]; m[5] = y[1]; m[6] = z[1];
  m[8] = x[2]; m[9] = y[2]; m[10] = z[2];
  m[12] = -dot(x, eye); m[13] = -dot(y, eye); m[14] = -dot(z, eye);
  return m;
}
function modelAt(x, y, z, sx, sy, sz, rotY, rotX, rotZ){
  rotY = rotY || 0; rotX = rotX || 0; rotZ = rotZ || 0;
  return mul(translate(x, y, z), mul(ry(rotY), mul(rx(rotX), mul(rz(rotZ), scaleM(sx, sy, sz)))));
}
function draw(renderer, mesh, model, vp, color, glass, time, camera){
  var gl = renderer.gl, loc = renderer.loc;
  gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vb);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, mesh.ib);
  gl.enableVertexAttribArray(loc.pos);
  gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 24, 0);
  gl.enableVertexAttribArray(loc.normal);
  gl.vertexAttribPointer(loc.normal, 3, gl.FLOAT, false, 24, 12);
  gl.uniformMatrix4fv(loc.model, false, model);
  gl.uniformMatrix4fv(loc.mvp, false, mul(vp, model));
  gl.uniform3fv(loc.color, color);
  gl.uniform3fv(loc.camera, camera);
  gl.uniform1f(loc.glass, glass ? 1 : 0);
  gl.uniform1f(loc.time, time);
  gl.drawElements(gl.TRIANGLES, mesh.count, gl.UNSIGNED_SHORT, 0);
}
function setupCanvas(canvas, renderer){
  var dpr = Math.min(window.devicePixelRatio || 1, 1.7);
  var w = Math.max(1, Math.floor(canvas.clientWidth * dpr));
  var h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  renderer.gl.viewport(0, 0, w, h);
  return { w: w, h: h, aspect: w / h };
}
function renderBackdrop(t){
  var gl = back.gl, mesh = back.mesh;
  var dim = setupCanvas(backdrop, back), time = t * .001;
  gl.clearColor(0, 0, 0, 0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  var camera = [0, 0, 8.2];
  var view = lookAt(camera, [0, 0, 0], [0, 1, 0]);
  var proj = perspective(Math.PI / 4, dim.aspect, .1, 60);
  var vp = mul(proj, view);
  var drift = reduced ? 0 : time * .18;
  /* Large translucent prism-like solids at the margins; UI stays readable. */
  draw(back, mesh.cube, modelAt(-4.6 + mouseX, -.15 + Math.sin(drift) * .25, -1, 1.35, 1.7, 1.15, drift, .22, .12), vp, [.13, .72, .92], true, time, camera);
  draw(back, mesh.cube, modelAt(4.4 - mouseX, .75 + Math.cos(drift * .8) * .28, -1, .95, 1.25, .85, -drift * .8, .3, -.2), vp, [.52, .27, .92], true, time, camera);
  draw(back, mesh.torus, modelAt(-4.6 + mouseX, -.15, -.8, 1.5, 1.5, 1.5, drift, .8, .25), vp, [.15, .8, 1], true, time, camera);
  draw(back, mesh.torus, modelAt(4.4 - mouseX, .75, -.8, 1.2, 1.2, 1.2, -drift, .6, -.35), vp, [.62, .35, 1], true, time, camera);
  /* Small floating relay-like cuboids at staggered depths. */
  for (var i = 0; i < 5; i++) {
    var x = (i % 2 === 0 ? -1 : 1) * (3.0 + (i % 3) * .58);
    var y = -1.6 + i * .78;
    var z = -.6 - (i % 3) * .5;
    draw(back, mesh.cube, modelAt(x + mouseX * 1.5, y + Math.sin(drift + i) * .12, z, .22, .26, .18, drift + i, .3 + i * .1, .12), vp,
      i % 2 ? [.47, .31, .9] : [.12, .75, .9], true, time, camera);
  }
}
function renderOrb(t){
  var gl = orb.gl, mesh = orb.mesh;
  var dim = setupCanvas(orbCanvas, orb), time = t * .001;
  gl.clearColor(0, 0, 0, 0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  var camera = [0, 0, 4.8];
  var view = lookAt(camera, [0, 0, 0], [0, 1, 0]);
  var proj = perspective(Math.PI / 4, dim.aspect, .1, 30);
  var vp = mul(proj, view);
  var spin = reduced ? 0 : time * .22;
  draw(orb, mesh.sphere, modelAt(0, 0, 0, 1.02, 1.02, 1.02, spin, .12, 0), vp, [.12, .66, .82], false, time, camera);
  draw(orb, mesh.torus, modelAt(0, 0, 0, 1.22, 1.22, 1.22, spin * 1.3, .88, .2), vp, [.23, .92, 1], true, time, camera);
  draw(orb, mesh.torus, modelAt(0, 0, 0, 1.38, 1.38, 1.38, -spin * .8, .35, 1.0), vp, [.55, .34, 1], true, time, camera);
  draw(orb, mesh.torus, modelAt(0, 0, 0, 1.53, 1.53, 1.53, spin * .65, 1.2, -.5), vp, [.15, .8, 1], true, time, camera);
}
function onMove(e){
  mouseX = (e.clientX / window.innerWidth - .5) * .35;
  mouseY = (e.clientY / window.innerHeight - .5) * .2;
}
function frame(t){
  if (!reduced || !last || t - last > 900) {
    if (back) renderBackdrop(t);
    if (orb) renderOrb(t);
    last = t;
  }
  raf = requestAnimationFrame(frame);
}
window.__isuiSpaceAdd({
  start: function(){
    if (started) return;
    backdrop = document.getElementById('webglBackdrop');
    orbCanvas = document.getElementById('webglPowerOrb');
    if (!backdrop) return;
    back = null; orb = null;
    try { back = createRenderer(backdrop); } catch (e) { back = null; }
    if (!back) { document.documentElement.classList.add('webgl-fallback'); return; }
    document.documentElement.classList.add('webgl-ready');
    if (orbCanvas) {
      try { orb = createRenderer(orbCanvas); } catch (e2) { orb = null; }
      if (orb) document.documentElement.classList.add('orb-gl');
      else if (orbCanvas.parentNode) orbCanvas.parentNode.removeChild(orbCanvas);
    }
    started = true;
    last = 0;
    if (finePointer) document.addEventListener('pointermove', onMove, { passive: true });
    raf = requestAnimationFrame(frame);
  },
  visibility: function(hidden){
    if (!started) return;
    if (hidden) {
      if (raf) { cancelAnimationFrame(raf); raf = 0; }
    } else if (!raf) {
      last = 0;
      raf = requestAnimationFrame(frame);
    }
  },
  stop: function(){
    if (!started) return;
    started = false;
    if (raf) { cancelAnimationFrame(raf); raf = 0; }
    document.removeEventListener('pointermove', onMove);
    [back, orb].forEach(function(r){
      if (!r) return;
      var ext = r.gl.getExtension('WEBGL_lose_context');
      if (ext) { try { ext.loseContext(); } catch (e) {} }
    });
    back = null; orb = null;
  }
});
})();
/* Boot the scene core (all modules above are registered by now). */
if (window.__isuiSpaceBoot) window.__isuiSpaceBoot();
)SPJS";
