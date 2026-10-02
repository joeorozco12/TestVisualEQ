// Renders docs/img/*.svg to PNG at 2x with the bundled Playwright Chromium. Run: node tools/svg2png.js
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const dir = path.join(__dirname, '..', 'docs', 'img');
  const browser = await chromium.launch();
  const page = await browser.newPage({ deviceScaleFactor: 2 });
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.svg'))) {
    const svg = fs.readFileSync(path.join(dir, f), 'utf8');
    const [, w, h] = svg.match(/width="(\d+)" height="(\d+)"/);
    await page.setViewportSize({ width: +w, height: +h });
    await page.setContent(`<html><body style="margin:0">${svg}</body></html>`);
    await page.screenshot({ path: path.join(dir, f.replace('.svg', '.png')), clip: { x: 0, y: 0, width: +w, height: +h } });
    console.log('rendered', f);
  }
  await browser.close();
})();
