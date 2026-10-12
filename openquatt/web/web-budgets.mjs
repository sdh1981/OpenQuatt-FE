// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals and the integrated curve editor.
    // Combined main/dev: ~1.286 MB raw / 358 kB gzip on Node 24.14.1.
    // OpenQuatt FE: +50 kB raw / +13 kB gzip for the hot water UI (phase 5).
    // Upstream: raw 1_290_000, gzipBaselineCeiling 362_000. The per-PR growth limit stays.
    raw: 1_340_000,
    gzipBaselineCeiling: 375_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
