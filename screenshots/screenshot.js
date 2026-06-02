#!/usr/bin/env node
// screenshot.js — Render screenshots/ui.html in headless Chromium and capture
// the Control (desktop + mobile), Config, and Status tabs of the web UI.
//
// Run `node extract_ui.js` first to (re)generate ui.html from the firmware.

const path = require('path');
const { chromium } = require('playwright');

const DIR = __dirname;
const UI = 'file://' + path.join(DIR, 'ui.html');
const out = (name) => path.join(DIR, name);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Click the tab button whose visible text matches `label`.
async function openTab(page, label) {
    await page.evaluate((lbl) => {
        const btns = Array.from(document.querySelectorAll('.tab-btn'));
        const b = btns.find((x) => x.textContent.trim().toLowerCase() === lbl.toLowerCase());
        if (b) b.click();
    }, label);
}

// Clear transient message banners. The page's inline loadConfig()/pollStatus()
// fire once at parse time — before the injected mock fetch is installed — so a
// stale "Error loading config" banner can linger. Wipe them for a clean shot.
async function clearMsgs(page) {
    await page.evaluate(() => {
        ['cfg-msg', 'ctrl-msg', 'wifi-msg', 'reboot-msg'].forEach((id) => {
            const el = document.getElementById(id);
            if (el) el.innerHTML = '';
        });
    });
}

async function main() {
    const browser = await chromium.launch({
        headless: true,
        args: ['--no-sandbox', '--disable-setuid-sandbox'],
    });
    const context = await browser.newContext();
    const page = await context.newPage();

    await page.goto(UI, { waitUntil: 'load' });
    await sleep(1500); // initial render + mock fetch callbacks

    // A. Control tab — desktop
    await page.setViewportSize({ width: 1000, height: 700 });
    await openTab(page, 'Control');
    await sleep(500);
    await page.screenshot({
        path: out('screenshot_control_desktop.png'),
        clip: { x: 0, y: 0, width: 1000, height: 700 },
    });

    // B. Control tab — mobile
    await page.setViewportSize({ width: 390, height: 844 });
    await openTab(page, 'Control');
    await sleep(500);
    await page.screenshot({
        path: out('screenshot_control_mobile.png'),
        fullPage: true,
    });

    // C. Config tab
    await page.setViewportSize({ width: 1000, height: 700 });
    await openTab(page, 'Config');
    await sleep(500);
    await clearMsgs(page);
    await page.screenshot({
        path: out('screenshot_config.png'),
        clip: { x: 0, y: 0, width: 1000, height: 700 },
    });

    // D. Status tab
    await page.setViewportSize({ width: 1000, height: 700 });
    await openTab(page, 'Status');
    await sleep(1000); // let the 1 s status poll populate the bars
    await page.screenshot({
        path: out('screenshot_status.png'),
        clip: { x: 0, y: 0, width: 1000, height: 700 },
    });

    await browser.close();
    console.log('Screenshots saved.');
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
