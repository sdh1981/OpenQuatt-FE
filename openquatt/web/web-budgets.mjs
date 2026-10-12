// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility, plus the performance invitation.
    // dev baseline: 1,308,178 B; invitation adds ~7.5 kB raw / ~2.6 kB gzip on Node 24.14.1.
    // OpenQuatt FE: +50 kB raw / +13 kB gzip for the hot water UI (phase 5).
    // On v0.54.1 the FE bundle is ~1.341 MB raw / ~375 kB gzip; ceiling leaves ~5 kB headroom.
    // Upstream: raw 1_320_000, gzipBaselineCeiling 362_000. The per-PR growth limit stays.
    raw: 1_370_000,
    gzipBaselineCeiling: 380_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
