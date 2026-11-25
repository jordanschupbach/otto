// Otto's page keyboard commands, Vimium-style. Active whenever the page has
// focus and you're not typing into a text field.
//
//   f    show a hint label on every clickable thing in view; typing a label
//        follows it in this tab (links navigate, buttons click, fields focus)
//   F    same, but links open in a new background tab
//        (while hints are up: Esc cancels, Backspace un-types a character)
//   j/k  scroll down/up (hold to keep scrolling)
//   Ctrl+d/Ctrl+u  scroll down/up half a page
//   Esc  leave a text field, so the keys above work again
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
    const vw = innerWidth, vh = innerHeight;
    for (const el of document.querySelectorAll(CLICKABLE)) {
      // Most matches on a long page are off screen: reject those with one cheap
      // box before the per-fragment and hit-testing work in visibleRect().
      const b = el.getBoundingClientRect();
      if (b.width < 1 || b.height < 1 || b.bottom <= 0 || b.right <= 0 || b.top >= vh || b.left >= vw) continue;
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

  // --- scrolling -----------------------------------------------------------------

  const SCROLL_STEP = 60;  // px per j/k press (key repeat makes holding smooth)

  function canScrollY(el) {
    if (el.scrollHeight <= el.clientHeight) return false;
    const overflow = getComputedStyle(el).overflowY;
    return overflow === "auto" || overflow === "scroll" || overflow === "overlay";
  }

  const visibleArea = (el) => {
    const r = el.getBoundingClientRect();
    return Math.max(0, Math.min(r.right, innerWidth) - Math.max(r.left, 0)) *
           Math.max(0, Math.min(r.bottom, innerHeight) - Math.max(r.top, 0));
  };

  // The nearest scrollable container of whatever is in the middle of the view:
  // the panel you're looking at. Walks up through shadow roots too.
  function scrollableAtCenter() {
    let el = document.elementFromPoint(innerWidth / 2, innerHeight / 2);
    while (el && el !== document.body && el !== document.documentElement) {
      if (el.clientHeight >= 50 && canScrollY(el)) return el;
      el = el.parentElement || (el.getRootNode() instanceof ShadowRoot ? el.getRootNode().host : null);
    }
    return null;
  }

  // Last resort: the largest visible scrollable element. This scans the whole
  // page, so the result is kept until it stops being scrollable.
  let largestCache = null;
  function largestScrollable() {
    if (largestCache && largestCache.isConnected && canScrollY(largestCache) && visibleArea(largestCache) > 0)
      return largestCache;
    let best = null, bestArea = 0;
    for (const el of document.querySelectorAll("body *")) {
      if (el.clientHeight < 50 || !canScrollY(el)) continue;
      const area = visibleArea(el);
      if (area > bestArea) { best = el; bestArea = area; }
    }
    return (largestCache = best);
  }

  // The document itself when it scrolls (null = window); otherwise, on app-like
  // pages that scroll an inner panel, that panel.
  function scrollTarget() {
    const doc = document.scrollingElement || document.documentElement;
    if (doc && doc.scrollHeight > doc.clientHeight) return null;
    return scrollableAtCenter() || largestScrollable();
  }

  // `amount(viewHeight)` gives the distance in px; direction is +1 down, -1 up.
  function scroll(direction, amount) {
    const target = scrollTarget();
    const viewHeight = target ? target.clientHeight : innerHeight;
    const options = { top: direction * amount(viewHeight), behavior: "instant" };
    if (target) target.scrollBy(options);
    else window.scrollBy(options);
  }
  const step = () => SCROLL_STEP;
  const halfPage = (viewHeight) => Math.max(SCROLL_STEP, Math.round(viewHeight / 2));

  // --- key dispatch ---------------------------------------------------------------

  window.addEventListener("keydown", (event) => {
    if (session) return onHintKey(event);
    if (event.altKey || event.metaKey) return;

    const active = deepActiveElement();
    if (isEditable(active)) {
      // Escape leaves a text field, so these keys work again without the mouse.
      if (event.key === "Escape" && !event.ctrlKey && !event.repeat) active.blur();
      return;  // otherwise the field gets the key as usual
    }

    if (event.ctrlKey) {
      if (event.shiftKey) return;
      switch (event.key) {
        case "d": scroll(+1, halfPage); break;
        case "u": scroll(-1, halfPage); break;
        default: return;
      }
      event.preventDefault();
      event.stopImmediatePropagation();
      return;
    }

    switch (event.key) {
      case "f":
      case "F":
        if (event.repeat) return;
        show(event.key === "F");
        break;
      case "j":
        scroll(+1, step);
        break;
      case "k":
        scroll(-1, step);
        break;
      default:
        return;
    }
    event.preventDefault();
    event.stopImmediatePropagation();
  }, true);

  // Hint positions go stale once the page moves.
  for (const type of ["scroll", "resize", "blur", "pagehide"])
    window.addEventListener(type, hide, true);
})();
