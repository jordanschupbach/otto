// Otto link hints, Vimium-style.
//
//   f  show a hint label on every clickable thing in view; typing a label
//      follows it in this tab (links navigate, buttons click, fields focus)
//   F  same, but links open in a new background tab
//   Esc cancels, Backspace un-types a character.
//
// Injected by Otto into an isolated script world: it shares the page's DOM but
// not its JavaScript globals, so the page can neither see nor call
// window.webkit.messageHandlers.otto (which can open tabs), nor tamper with
// this code. Injected at document start so our keydown listener runs before
// the page's own.
(() => {
  "use strict";

  // Home-row first; all labels in one session have the same length, so no
  // label is a prefix of another.
  const ALPHABET = "sadfjklewcmpgh";

  const CLICKABLE = [
    "a[href]", "area[href]", "button", "input:not([type=hidden])", "select", "textarea",
    "summary", "label[for]", "[onclick]", "[contenteditable='']", "[contenteditable='true']",
    "[role=button]", "[role=link]", "[role=checkbox]", "[role=radio]", "[role=tab]",
    "[role=menuitem]", "[role=option]", "[role=switch]", "[tabindex]:not([tabindex='-1'])",
  ].join(",");

  const NON_TEXT_INPUTS = new Set([
    "button", "checkbox", "color", "file", "hidden", "image", "radio", "range", "reset", "submit",
  ]);

  const STYLE = `
    :host { all: initial !important; }
    .hint {
      position: fixed;
      font: bold 12px/1.2 ui-monospace, monospace;
      text-transform: uppercase;
      padding: 1px 4px;
      border-radius: 3px;
      background: #ffd76e;
      color: #302505;
      border: 1px solid #c38a22;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.35);
      white-space: nowrap;
    }
    .newtab .hint { background: #a8d1ff; color: #0b2545; border-color: #3b7ac4; }
    .typed { opacity: 0.4; }
  `;

  let session = null;  // { hints: [{ label, el, node }], typed, newTab, host }

  // --- helpers ------------------------------------------------------------

  function deepActiveElement() {
    let el = document.activeElement;
    while (el && el.shadowRoot && el.shadowRoot.activeElement) el = el.shadowRoot.activeElement;
    return el;
  }

  function isEditable(el) {
    if (!el) return false;
    if (el.isContentEditable) return true;
    if (el.tagName === "TEXTAREA" || el.tagName === "SELECT") return true;
    if (el.tagName === "INPUT") return !NON_TEXT_INPUTS.has((el.type || "").toLowerCase());
    return false;
  }

  // The first on-screen, unobscured box of `el`, or null.
  function visibleRect(el) {
    for (const r of el.getClientRects()) {
      if (r.width < 1 || r.height < 1) continue;
      if (r.bottom <= 0 || r.right <= 0 || r.top >= innerHeight || r.left >= innerWidth) continue;
      // Something else on top (a modal, a sticky header)? Probe a few points.
      const points = [
        [r.left + r.width / 2, r.top + r.height / 2],
        [r.left + 2, r.top + 2],
      ];
      for (const [x, y] of points) {
        const cx = Math.min(Math.max(x, 0), innerWidth - 1);
        const cy = Math.min(Math.max(y, 0), innerHeight - 1);
        const hit = document.elementFromPoint(cx, cy);
        if (hit && (hit === el || el.contains(hit) || hit.contains(el))) return r;
      }
    }
    return null;
  }

  function makeLabels(count) {
    const k = ALPHABET.length;
    let length = 1;
    while (k ** length < count) length++;
    const labels = [];
    for (let i = 0; i < count; i++) {
      let label = "";
      for (let j = 0, x = i; j < length; j++, x = Math.floor(x / k)) label += ALPHABET[x % k];
      labels.push(label);
    }
    return labels;
  }

  function findTargets() {
    const targets = [];
    for (const el of document.querySelectorAll(CLICKABLE)) {
      if (el.disabled) continue;
      // A link already covers whatever clickable bits are inside it.
      if (el.parentElement && el.parentElement.closest("a[href]")) continue;
      const rect = visibleRect(el);
      if (rect) targets.push({ el, rect });
    }
    return targets;
  }

  // --- hint session ------------------------------------------------------------

  function show(newTab) {
    const targets = findTargets();
    if (targets.length === 0) return;

    const host = document.createElement("otto-hints");
    host.style.cssText =
      "all: initial !important; position: fixed !important; inset: 0 !important;" +
      "z-index: 2147483647 !important; pointer-events: none !important;";
    const root = host.attachShadow({ mode: "closed" });
    const style = document.createElement("style");
    style.textContent = STYLE;
    const layer = document.createElement("div");
    if (newTab) layer.className = "newtab";
    root.append(style, layer);

    const labels = makeLabels(targets.length);
    const hints = targets.map(({ el, rect }, i) => {
      const node = document.createElement("span");
      node.className = "hint";
      node.style.left = `${Math.max(0, rect.left)}px`;
      node.style.top = `${Math.max(0, rect.top)}px`;
      layer.append(node);
      return { label: labels[i], el, node };
    });

    (document.documentElement || document.body).append(host);
    session = { hints, typed: "", newTab, host };
    render();
  }

  function hide() {
    if (!session) return;
    session.host.remove();
    session = null;
  }

  function render() {
    for (const { label, node } of session.hints) {
      const matches = label.startsWith(session.typed);
      node.style.display = matches ? "" : "none";
      if (!matches) continue;
      const typed = document.createElement("span");
      typed.className = "typed";
      typed.textContent = session.typed;
      node.replaceChildren(typed, label.slice(session.typed.length));
    }
  }

  function activate(el, newTab) {
    const link = el.closest("a[href], area[href]");
    if (newTab && link && /^(https?|file|ftp):/i.test(link.href)) {
      window.webkit.messageHandlers.otto.postMessage({ type: "open-in-new-tab", url: link.href });
      return;
    }
    if (isEditable(el)) {
      el.focus();
      return;
    }
    // Some sites act on mousedown/mouseup rather than click.
    for (const type of ["mouseover", "mousedown", "mouseup"])
      el.dispatchEvent(new MouseEvent(type, { bubbles: true, cancelable: true, view: window }));
    el.focus({ preventScroll: true });
    el.click();
  }

  // --- keyboard ------------------------------------------------------------------

  function onHintKey(event) {
    event.preventDefault();
    event.stopImmediatePropagation();

    if (event.key === "Escape") return hide();
    if (event.key === "Backspace") {
      session.typed = session.typed.slice(0, -1);
      return render();
    }

    const ch = event.key.toLowerCase();
    if (ch.length !== 1 || !ALPHABET.includes(ch)) return;

    const typed = session.typed + ch;
    const matches = session.hints.filter((h) => h.label.startsWith(typed));
    if (matches.length === 0) return;  // ignore keys that match nothing

    if (matches.length === 1 && matches[0].label === typed) {
      const { el } = matches[0];
      const { newTab } = session;
      hide();
      activate(el, newTab);  // still inside the keypress, so it counts as a user gesture
      return;
    }
    session.typed = typed;
    render();
  }

  window.addEventListener("keydown", (event) => {
    if (session) return onHintKey(event);
    if (event.ctrlKey || event.altKey || event.metaKey || event.repeat) return;

    const active = deepActiveElement();
    // Escape leaves a text field, so f/F work again without the mouse.
    if (event.key === "Escape" && isEditable(active)) {
      active.blur();
      return;
    }
    if (event.key !== "f" && event.key !== "F") return;
    if (isEditable(active)) return;  // typing an "f" into a field
    event.preventDefault();
    event.stopImmediatePropagation();
    show(event.key === "F");
  }, true);

  // Hint positions go stale once the page moves.
  for (const type of ["scroll", "resize", "blur", "pagehide"])
    window.addEventListener(type, hide, true);
})();
