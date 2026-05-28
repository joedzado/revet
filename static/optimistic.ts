const el = (id: string) => document.getElementById(id) as HTMLElement;
const saveText = (id: string, mutate: (e: HTMLElement) => void) => {
  const e = el(id); e.dataset.prev = e.textContent ?? ''; mutate(e);
};
const num = (id: string, by: number, parse: (s: string) => number) =>
  saveText(id, e => { e.textContent = String(parse(e.textContent ?? '0') + by); });
const pending = (id: string, html: string, insert: (c: HTMLElement, n: HTMLElement) => void) => {
  const c = el(id), t = document.createElement('template');
  t.innerHTML = html;
  const n = t.content.firstElementChild as HTMLElement | null;
  if (n) { n.dataset.optimisticPending = '1'; insert(c, n); }
};

(window as any).revet = { opt: {
  incrInt:       (id: string, by: number) => num(id,  by, s => parseInt(s, 10)),
  decrInt:       (id: string, by: number) => num(id, -by, s => parseInt(s, 10)),
  incrFloat:     (id: string, by: number) => num(id,  by, parseFloat),
  decrFloat:     (id: string, by: number) => num(id, -by, parseFloat),
  setText:       (id: string, val: string) => saveText(id, e => { e.textContent = val; }),
  restoreText:   (id: string) => { const e = el(id); if (e.dataset.prev !== undefined) e.textContent = e.dataset.prev; },
  toggleClass:   (id: string, cls: string) => { const e = el(id); e.dataset.prev = e.classList.contains(cls) ? '1' : '0'; e.classList.toggle(cls); },
  rollbackClass: (id: string, cls: string) => { const e = el(id); e.dataset.prev === '1' ? e.classList.add(cls) : e.classList.remove(cls); },
  addClass:      (id: string, cls: string) => el(id).classList.add(cls),
  removeClass:   (id: string, cls: string) => el(id).classList.remove(cls),
  appendPending:  (id: string, html: string) => pending(id, html, (c, n) => c.appendChild(n)),
  prependPending: (id: string, html: string) => pending(id, html, (c, n) => c.prepend(n)),
  removePending:  (id: string) => document.querySelectorAll(`#${id}>[data-optimistic-pending]`).forEach(e => (e as HTMLElement).remove()),
}};
