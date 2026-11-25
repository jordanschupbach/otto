// Otto benchmark driver. Injected by otto_bench (bench/otto_bench.cpp) into its
// own script world, alongside Otto's real src/keys.js. It sends keydown events
// into the page, which keys.js handles exactly like real key presses, and times
// how long each one blocks the page's main thread. That's the time the page
// can't scroll, paint or process the next key, so it is what "slow keys" feel
// like. (The timing excludes the hop from the UI process, which keys.js can't
// affect.)
//
// otto_bench prepends `const OTTO_BENCH = { mode, iterations }`:
//   mode "suite": build synthetic pages and measure each scenario
//   mode "page":  measure on whatever real page is loaded
(() => {
  "use strict";
  const { mode, iterations, page: latencyPage } = OTTO_BENCH;
  const post = (msg) => window.webkit.messageHandlers.bench.postMessage(msg);

  // --- measurement ------------------------------------------------------------

  function key(k, extra = {}) {
    const target = document.activeElement || document.body || document.documentElement;
    const event = new KeyboardEvent("keydown", { key: k, bubbles: true, cancelable: true, ...extra });
    target.dispatchEvent(event);
    return event.defaultPrevented;
  }

  // WebKit rounds performance.now() to whole milliseconds, so each sample times
  // a batch of BATCH operations and divides. A scenario is { op, setup?, teardown? };
  // only `op` is timed.
  const BATCH = 20;

  function sample({ op, setup, teardown }) {
    if (setup) setup();
    const start = performance.now();
    for (let i = 0; i < BATCH; i++) op(i);
    const ms = (performance.now() - start) / BATCH;
    if (teardown) teardown();
    return ms;
  }

  function measure(scenario, n = iterations) {
    for (let i = 0; i < 3; i++) sample(scenario);  // warm up the JIT and caches
    const samples = [];
    for (let i = 0; i < n; i++) samples.push(sample(scenario));
    return stats(samples);
  }

  // Alternate directions so repeated presses don't hit the end of the page.
  const alternate = (down, up, extra) => ({ op: (i) => key(i % 2 ? up : down, extra) });

  const scenarios = {
    // A key keys.js ignores: the fixed cost every keystroke pays.
    "other key (x)": { op: () => key("x") },
    "j/k": alternate("j", "k"),
    "ctrl+d/u": alternate("d", "u", { ctrlKey: true }),
    // Show hints, then hide them again.
    "f + Esc": { op: () => { key("f"); key("Escape"); } },
    // One keystroke while hints are up. Every keystroke re-renders all labels;
    // Backspace does that without risking following a link on a real page.
    "hint keystroke": { setup: () => key("f"), op: () => key("Backspace"), teardown: () => key("Escape") },
  };

  function report(page, name, stats) {
    post({ type: "result", page, scenario: name, ...stats });
  }

  // --- synthetic pages ------------------------------------------------------------

  const words = "lorem ipsum dolor sit amet consectetur adipiscing elit sed do".split(" ");
  const text = (i, n) => Array.from({ length: n }, (_, j) => words[(i + j) % words.length]).join(" ");

  // Each returns { html, css }. Element counts are roughly what real pages have:
  // a long article (~3k elements) up to a heavy app or feed (~20-50k).
  const pages = {
    "article (3k elements, page scrolls)": () => ({
      html: Array.from({ length: 1000 }, (_, i) =>
        `<p>${text(i, 12)} <a href="#a${i}">link ${i}</a> <em>${text(i + 3, 4)}</em></p>`).join(""),
    }),
    "feed (40k elements, page scrolls)": () => ({
      html: Array.from({ length: 2000 }, (_, i) =>
        `<article><header><a href="#u${i}">user ${i}</a><time>${i}m</time></header>` +
        `<div><p>${text(i, 20)}</p><p>${text(i + 5, 10)}</p></div>` +
        `<footer><button>like</button><button>reply</button><a href="#p${i}">share</a>` +
        `<span>${i}</span><span>${i * 3}</span></footer></article>`).join(""),
    }),
    // Like Gmail/Slack/docs sites: the document doesn't scroll, an inner pane does.
    "app (40k elements, inner pane scrolls)": () => ({
      css: `html, body { height: 100%; margin: 0; overflow: hidden; }
            nav { position: absolute; left: 0; top: 0; bottom: 0; width: 220px; overflow-y: auto; }
            main { position: absolute; left: 220px; right: 0; top: 0; bottom: 0; overflow-y: auto; }`,
      html: `<nav>${Array.from({ length: 300 }, (_, i) => `<div><a href="#n${i}">folder ${i}</a></div>`).join("")}</nav>` +
        `<main>${Array.from({ length: 2000 }, (_, i) =>
          `<div class="row"><input type="checkbox"><span>${text(i, 3)}</span>` +
          `<a href="#m${i}"><b>${text(i, 6)}</b> — <i>${text(i + 2, 10)}</i></a>` +
          `<span><span>${i}</span><span>attach</span></span><time>${i}:00</time>` +
          `<div><span>${text(i, 2)}</span><span>${text(i + 1, 2)}</span></div></div>`).join("")}</main>`,
    }),
    // Lots of links on screen at once (link-heavy indexes, tables).
    "link grid (1500 visible links)": () => ({
      css: `body { margin: 0; display: grid; grid-template-columns: repeat(30, 1fr); gap: 2px; font: 11px sans-serif; }`,
      html: Array.from({ length: 1500 }, (_, i) => `<a href="#g${i}">l${i}</a>`).join(""),
    }),
  };

  function load({ html, css = "" }) {
    document.head.querySelectorAll("style[data-bench]").forEach((s) => s.remove());
    const style = document.createElement("style");
    style.dataset.bench = "";
    style.textContent = `body { font: 14px sans-serif; } ${css}`;
    document.head.append(style);
    document.body.innerHTML = html;
    window.scrollTo(0, 0);
    document.body.getBoundingClientRect();  // settle layout before timing
  }

  // Give the page a frame between scenarios so pending work (scroll events,
  // style recalcs) doesn't land inside the next measurement.
  const nextFrame = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));

  // Time between frames: 16.7 ms is 60 fps, 4.2 ms is 240 fps. `perFrame` runs
  // at the start of each frame (e.g. a j press, to scroll like holding j).
  function frameIntervals(frames, perFrame) {
    return new Promise((resolve) => {
      const samples = [];
      let last = null;
      const tick = (now) => {
        if (last !== null) samples.push(now - last);
        last = now;
        if (samples.length >= frames) return resolve(stats(samples));
        if (perFrame) perFrame(samples.length);
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    });
  }

  function stats(samples) {
    samples.sort((a, b) => a - b);
    const pick = (q) => samples[Math.min(samples.length - 1, Math.floor(q * samples.length))];
    return { median: pick(0.5), p95: pick(0.95), max: samples[samples.length - 1], n: samples.length };
  }

  async function runScenarios(page) {
    for (const [name, scenario] of Object.entries(scenarios)) {
      await nextFrame();
      report(page, name, measure(scenario));
    }
    await nextFrame();
    report(page, "frame interval (idle)", await frameIntervals(120));
    // Scroll down for half the frames, then back up.
    report(page, "frame interval (j held)", await frameIntervals(120, (i) => key(i < 60 ? "j" : "k")));
  }

  // --- latency (mode "latency") -------------------------------------------------------
  // otto_bench sends real key presses (XTest) and times how long until the page
  // renders the scroll: scroll events fire in the rendering update that shows it.
  // "busy" pages keep the main thread occupied like a heavy site's scripts do.
  async function startLatency() {
    const [base, busy] = latencyPage.split("+");
    load(pages[base]());
    await nextFrame();
    const doc = document.scrollingElement;
    doc.scrollTop = (doc.scrollHeight - doc.clientHeight) / 2;  // room to scroll both ways
    if (busy) {
      // 30 ms of script every 50 ms: about what a heavy feed or web app does.
      setInterval(() => { const end = performance.now() + 30; while (performance.now() < end); }, 50);
    }
    addEventListener("scroll", () => post({ type: "scrolled" }), { passive: true });
    await nextFrame();
    post({ type: "ready", page: latencyPage });
  }

  async function main() {
    try {
      if (mode === "latency") return startLatency();
      if (mode === "suite") {
        for (const [page, build] of Object.entries(pages)) {
          load(build());
          await nextFrame();
          await runScenarios(page);
        }
      } else {
        await new Promise((r) => setTimeout(r, 2000));  // let the site finish its startup work
        const page = `${location.host || location.href} (${document.getElementsByTagName("*").length} elements)`;
        await runScenarios(page);
      }
      post({ type: "done" });
    } catch (e) {
      post({ type: "error", message: `${e}\n${e.stack || ""}` });
    }
  }

  if (document.readyState === "complete") main();
  else addEventListener("load", main, { once: true });
})();
