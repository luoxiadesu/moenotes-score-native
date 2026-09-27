import createModule from "./moenotes-score.mjs";
// Install the message handler synchronously. Messages arriving while the WASM
// module is loading must not be discarded before onmessage is registered.
let Module;
const moduleReady = createModule().then(module => { Module = module; });
let prepared = 0, deck = 0, analysis = 0, running = false;
const utf8 = new TextEncoder();
function check(code) { if (code) throw new Error(Module.UTF8ToString(Module._mns_last_error())); }
function memory(bytes) { const ptr = Module._malloc(bytes.length || 1); if (!ptr) throw new Error("Allocation failed"); Module.HEAPU8.set(bytes, ptr); return ptr; }
function json(value) { const bytes = utf8.encode(JSON.stringify(value)); return { ptr: memory(bytes), length: bytes.length }; }
function string(value) { return memory(utf8.encode(value + "\0")); }
function makeHandle(call) { const out = Module._malloc(4); if (!out) throw new Error("Allocation failed"); try { Module.HEAPU32[out >>> 2] = 0; check(call(out)); return Module.HEAPU32[out >>> 2]; } finally { Module._free(out); } }
function snapshot() {
  if (!analysis) throw new Error("No analysis session");
  const ptr = makeHandle(out => Module._mns_analysis_snapshot(analysis, out));
  try { return JSON.parse(Module.UTF8ToString(ptr)); } finally { Module._mns_string_free(ptr); }
}
function clear() { if (analysis) Module._mns_analysis_free(analysis); if (deck) Module._mns_deck_free(deck); if (prepared) Module._mns_prepared_free(prepared); analysis = deck = prepared = 0; }
function prepare(input) {
  if (running) throw new Error("Cancel and await the active run before replacing inputs");
  clear(); const allocations = [];
  try {
    const allocJson = value => { const p = json(value); allocations.push(p.ptr); return p; };
    const master = allocJson(input.master), request = allocJson(input.calculation);
    const chart = memory(input.chart); allocations.push(chart);
    const name = string(input.chartName); allocations.push(name);
    prepared = makeHandle(out => Module._mns_prepare(master.ptr, master.length, chart, input.chart.length, name, request.ptr, request.length, out));
    const deckInput = allocJson(input.deck);
    deck = makeHandle(out => Module._mns_deck_resolve(prepared, deckInput.ptr, deckInput.length, out));
    const options = allocJson(input.options || {});
    analysis = makeHandle(out => Module._mns_analysis_create(prepared, deck, options.ptr, options.length, out));
  } catch (error) { clear(); throw error; }
  finally { for (const ptr of allocations) Module._free(ptr); }
}
async function run(budgetMs) {
  if (!analysis || running) throw new Error("Prepare an idle analysis first");
  if (!Number.isSafeInteger(budgetMs) || budgetMs < 0 || budgetMs > 3600000) throw new Error("budgetMs must be 0..3600000");
  running = true;
  const start = performance.now(); let nextReport = start + 1000;
  try {
    do {
      const remaining = Math.max(0, Math.floor(budgetMs - (performance.now() - start)));
      check(Module._mns_analysis_advance(analysis, Math.min(25, remaining), 0xffffffffffffffffn));
      let current;
      const now = performance.now();
      if (now >= nextReport || !remaining) { current = snapshot(); self.postMessage({ type: "progress", snapshot: current }); nextReport = now + 1000; }
      // Poll completion without requiring a one-second delay for small exact jobs.
      current ||= snapshot();
      if (current.complete || ["cancelled", "memory_limit", "failed"].includes(current.stop_reason) || now - start >= budgetMs) return current;
      await new Promise(resolve => setTimeout(resolve, 0));
    } while (true);
  } finally { running = false; }
}
self.onmessage = async ({ data }) => {
  try {
    await moduleReady;
    let result;
    switch (data.type) {
      case "prepare": prepare(data.input); result = null; break;
      case "run": result = await run(data.budgetMs); break;
      case "resume": if (!analysis) throw new Error("No analysis session"); if (running) throw new Error("Await the active run before resuming"); Module._mns_analysis_resume(analysis); result = await run(data.budgetMs); break;
      case "cancel": if (!analysis) throw new Error("No analysis session"); Module._mns_analysis_cancel(analysis); result = snapshot(); break;
      case "snapshot": result = snapshot(); break;
      default: throw new Error("Unknown worker message");
    }
    self.postMessage({ id: data.id, result });
  } catch (error) { self.postMessage({ id: data.id, error: String(error.message || error) }); }
};
