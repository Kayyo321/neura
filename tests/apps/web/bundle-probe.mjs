// Added by app-check.py to a copy of a built bundle, beside its boot module: waits for the bundled application
// to mount, checks what the bundle should show (the walkthrough's styled counter, which it clicks, or the test
// application's bundled image), and posts a report. The bundle's content-security policy allows it, since it is
// a module of the page's own origin.
const page = globalThis.document;

async function until(predicate, label) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > 20000) {
            throw new Error(`timed out waiting for ${label}: state ${page.documentElement.dataset.neuraState}, `
                            + `failure ${page.getElementById('neura-failure')?.textContent}, `
                            + `DOM ${page.getElementById('app').innerHTML}`);
        }
        await new Promise(resolve => setTimeout(resolve, 5));
    }
}

async function probe() {
    await until(() => page.getElementById('count') || page.getElementById('badge'), 'the first commit');
    const report = { state: page.documentElement.dataset.neuraState, sheets: page.styleSheets.length };
    const badge = page.getElementById('badge');
    if (badge) {
        await until(() => badge.complete && badge.naturalWidth === 16, 'the bundled image');
        report.image = badge.getAttribute('src');
    } else {
        const count = page.getElementById('count');
        report.color = getComputedStyle(count).color;
        page.getElementById('add').click();
        await until(() => count.textContent === '1', 'a click on the bundled counter');
        report.clicked = count.textContent;
    }
    return report;
}

const report = await probe().catch(error => ({ error: error.stack || String(error) }));
await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
