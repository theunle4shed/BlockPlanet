const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const dir = process.argv[2], filter = process.argv[3] || '';
  const list = JSON.parse(fs.readFileSync(dir + '/manifest.json')).filter(m => m.name.includes(filter));
  const b = await chromium.launch({ args: ['--allow-file-access-from-files'] });
  const ctx = await b.newContext({ deviceScaleFactor: 1 });
  const p = await ctx.newPage();
  for (const m of list) {
    await p.setViewportSize({ width: m.w, height: m.h });
    await p.goto('file://' + dir + '/' + m.name + '.html');
    await p.evaluate(() => document.fonts.ready);
    await p.screenshot({ path: dir + '/' + m.name + '.png' });
  }
  await b.close();
  console.log(list.length);
})();
