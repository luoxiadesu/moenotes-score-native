import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import assert from "node:assert/strict";
const root=path.resolve(import.meta.dirname,"..");
const binary=path.resolve(process.argv[2] || path.join(root,"build-wasm/moenotes-score.mjs"));
const {default:create}=await import(pathToFileURL(binary));
const m=await create();
const enc=new TextEncoder();
function alloc(bytes){const p=m._malloc(bytes.length||1);assert(p);m.HEAPU8.set(bytes,p);return p;}
function text(s){return alloc(enc.encode(s));}
function check(n){assert.equal(n,0,m.UTF8ToString(m._mns_last_error()));}
function handle(f){const o=m._malloc(4);m.HEAPU32[o>>>2]=0;check(f(o));const p=m.HEAPU32[o>>>2];m._free(o);return p;}
function snapshot(a){const p=handle(o=>m._mns_analysis_snapshot(a,o));const r=JSON.parse(m.UTF8ToString(p));m._mns_string_free(p);return r;}
const master=fs.readFileSync(path.join(root,"tests/fixtures/api-master.json"));
const chart=fs.readFileSync(path.join(root,"tests/fixtures/api-chart.json"));
const req=JSON.parse(fs.readFileSync(path.join(root,"tests/fixtures/api-request.json")));
const calc=enc.encode(JSON.stringify({...req.calculation,schema_version:1}));
const dreq=enc.encode(JSON.stringify({deck:req.deck}));const opt=enc.encode(JSON.stringify(req.options));
const mp=alloc(master),cp=alloc(chart),rp=alloc(calc),np=text("0001_00.json\0"),dp=alloc(dreq),op=alloc(opt);
const p=handle(o=>m._mns_prepare(mp,master.length,cp,chart.length,np,rp,calc.length,o));
const d=handle(o=>m._mns_deck_resolve(p,dp,dreq.length,o));
const a=handle(o=>m._mns_analysis_create(p,d,op,opt.length,o));
m._mns_prepared_free(p);m._mns_deck_free(d);for(const ptr of [mp,cp,rp,np,dp,op])m._free(ptr);
check(m._mns_analysis_advance(a,1000,5n));assert.equal(snapshot(a).complete,false);
m._mns_analysis_cancel(a);check(m._mns_analysis_advance(a,1000,5n));assert.equal(snapshot(a).stop_reason,"cancelled");m._mns_analysis_resume(a);
check(m._mns_analysis_advance(a,10000,0xffffffffffffffffn));const r=snapshot(a);assert.equal(r.complete,true);assert.equal(r.results[0].expectation.status,"exact");
if(process.argv[3]){const native=JSON.parse(fs.readFileSync(process.argv[3]));assert.deepEqual(r.results,native.results);assert.equal(r.total_power,native.total_power);}
console.log(JSON.stringify(r));m._mns_analysis_free(a);
const bad=enc.encode("{}");const b=alloc(bad),out=m._malloc(4);assert.equal(m._mns_prepare(b,2,b,2,b,b,2,out),1);assert.equal(m.HEAPU32[out>>>2],0);m._free(b);m._free(out);
