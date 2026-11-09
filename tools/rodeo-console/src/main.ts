// The generic console's page: the console, with nothing added.

import { mount } from './console.ts';

let hide: ReturnType<typeof setTimeout> | undefined;

function toast(message: string, bad = false) {
  const t = document.getElementById('toast');
  if (!t) return;
  t.textContent = message;
  t.className = bad ? 'toast bad' : 'toast';
  t.hidden = false;
  clearTimeout(hide);
  hide = setTimeout(() => (t.hidden = true), bad ? 6000 : 3500);
}

mount(document.getElementById('app')!, { toast }); // index.html has it
