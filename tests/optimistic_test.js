#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict");

// ── Minimal DOM mock ──────────────────────────────────────────────────────────

const elements = {};

function makeEl(text = "", classes = []) {
  const cls = new Set(classes);
  const children = [];
  return {
    get textContent() { return text; },
    set textContent(v) { text = String(v); },
    dataset: {},
    classList: {
      contains: c => cls.has(c),
      toggle:   c => cls.has(c) ? cls.delete(c) : cls.add(c),
      add:      c => cls.add(c),
      remove:   c => cls.delete(c),
    },
    children,
    appendChild(n) { children.push(n); },
    prepend(n)     { children.unshift(n); },
  };
}

function makeTemplate(child) {
  return {
    set innerHTML(_) {},
    content: { firstElementChild: child ?? null },
  };
}

global.window   = {};
global.document = {
  getElementById:  id  => elements[id],
  createElement:   tag => tag === "template" ? makeTemplate(makeEl()) : makeEl(),
  querySelectorAll: sel => {
    const id = sel.match(/#([\w-]+)/)?.[1];
    const parent = id ? elements[id] : null;
    const kids = parent?.children ?? [];
    const pending = kids.filter(n => n.dataset?.optimisticPending);
    return { forEach: fn => pending.forEach(n => fn({
      ...n, remove: () => { const i = kids.indexOf(n); if (i >= 0) kids.splice(i, 1); }
    }))};
  },
};

require("../static/optimistic.js");
const { opt } = window.revet;

// ── Text content ──────────────────────────────────────────────────────────────

elements.n = makeEl("10");
opt.incrInt("n", 3);
assert.equal(elements.n.textContent, "13");
assert.equal(elements.n.dataset.prev, "10");

opt.restoreText("n");
assert.equal(elements.n.textContent, "10");

opt.decrInt("n", 4);
assert.equal(elements.n.textContent, "6");

opt.restoreText("n");
assert.equal(elements.n.textContent, "10");

elements.f = makeEl("1.5");
opt.incrFloat("f", 0.5);
assert.equal(elements.f.textContent, "2");
assert.equal(elements.f.dataset.prev, "1.5");

opt.decrFloat("f", 0.25);
assert.equal(elements.f.textContent, "1.75");

elements.s = makeEl("hello");
opt.setText("s", "world");
assert.equal(elements.s.textContent, "world");
assert.equal(elements.s.dataset.prev, "hello");

opt.restoreText("s");
assert.equal(elements.s.textContent, "hello");

// ── CSS classes ───────────────────────────────────────────────────────────────

elements.b = makeEl("", []);
opt.toggleClass("b", "active");
assert(elements.b.classList.contains("active"));
assert.equal(elements.b.dataset.prev, "0");

opt.rollbackClass("b", "active");
assert(!elements.b.classList.contains("active"));

elements.b2 = makeEl("", ["active"]);
opt.toggleClass("b2", "active");
assert(!elements.b2.classList.contains("active"));
assert.equal(elements.b2.dataset.prev, "1");

opt.rollbackClass("b2", "active");
assert(elements.b2.classList.contains("active"));

elements.c = makeEl();
opt.addClass("c", "highlight");
assert(elements.c.classList.contains("highlight"));

opt.removeClass("c", "highlight");
assert(!elements.c.classList.contains("highlight"));

// ── Containers ────────────────────────────────────────────────────────────────

elements.feed = makeEl();
opt.appendPending("feed", "<div>msg</div>");
assert.equal(elements.feed.children.length, 1);
assert.equal(elements.feed.children[0].dataset.optimisticPending, "1");

opt.removePending("feed");
assert.equal(elements.feed.children.length, 0);

elements.feed2 = makeEl();
opt.prependPending("feed2", "<div>first</div>");
assert.equal(elements.feed2.children.length, 1);
assert.equal(elements.feed2.children[0].dataset.optimisticPending, "1");

console.log("✔ optimistic.js — all assertions passed");
