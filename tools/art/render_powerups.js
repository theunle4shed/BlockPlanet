// Renders assets/ui/powerups/*.svg to 128 x 128 PNGs beside them (transparent),
// with the installed Chromium: node tools/art/render_powerups.js
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  const dir = path.join(__dirname, '..', '..', 'assets', 'ui', 'powerups');
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 128, height: 128 } });
  for (const file of fs.readdirSync(dir).filter(f => f.endsWith('.svg'))) {
    const svg = fs.readFileSync(path.join(dir, file), 'utf8').replace('<svg ', '<svg width="128" height="128" ');
    await page.setContent(`<html><body style="margin:0;background:transparent">${svg}</body></html>`);
    await page.screenshot({ path: path.join(dir, file.replace('.svg', '.png')), omitBackground: true });
  }
  await browser.close();
})();
