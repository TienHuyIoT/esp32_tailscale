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
