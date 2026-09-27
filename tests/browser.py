"""Optional local Chromium Worker integration; serves only synthetic fixtures."""
import argparse
import functools
import http.server
import json
from pathlib import Path
import threading

from playwright.sync_api import sync_playwright

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--chromium", type=Path)
args = parser.parse_args()


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


server = http.server.ThreadingHTTPServer(
    ("127.0.0.1", 0), functools.partial(Handler, directory=str(root)))
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = f"http://127.0.0.1:{server.server_port}"
try:
    with sync_playwright() as playwright:
        options = {"headless": True}
        if args.chromium:
            options["executable_path"] = str(args.chromium)
        browser = playwright.chromium.launch(**options)
        page = browser.new_page()
        page.goto(base + "/tests/browser.html")
        result = page.evaluate("""async () => {
            const {ScoreAnalyzer} = await import('../dist/web/index.js');
            const req = await (await fetch('./fixtures/api-request.json')).json();
            const master = await (await fetch('./fixtures/api-master.json')).json();
            const chart = new Uint8Array(await (await fetch('./fixtures/api-chart.json')).arrayBuffer());
            const analyzer = new ScoreAnalyzer(new URL('../dist/web/worker.mjs', location.href));
            await analyzer.prepare({master, chart, chartName:'0001_00.json',
                calculation:{...req.calculation, schema_version:1}, deck:{deck:req.deck}, options:req.options});
            const result = await analyzer.run(10000);
            analyzer.dispose();
            let disposed = false;
            try {await analyzer.snapshot();} catch {disposed = true;}
            return {result, disposed};
        }""")
        browser.close()
    assert result["result"]["complete"] and result["disposed"]
    assert result["result"]["results"][0]["expectation"]["status"] == "exact"
    print(json.dumps({"passed": True, "score": result["result"]["results"][0]["maximum"]["lower"]}))
finally:
    server.shutdown()
    server.server_close()
    thread.join()
