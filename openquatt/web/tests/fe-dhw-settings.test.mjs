import assert from "node:assert/strict";
import test from "node:test";

// OpenQuatt FE: de instellingengroep "Warm water" en het verbergen van de CV-ketel.
globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };

const {
  DHW_SETTING_KEYS,
  ENTITY_DEFS,
  HEADER_ENTITY_KEYS,
  SETTINGS_BACKUP_EXCLUDED_KEYS,
  SETTINGS_BACKUP_KEYS,
  SETTINGS_GROUPS,
  SETTINGS_KEYS,
} = await import("../js/src/core/config.js");
const { state } = await import("../js/src/core/state.js");
const { getSettingsGroupHydrationKeys } = await import("../js/src/core/entity-sync.js");
const { isFullElectric } = await import("../js/src/core/domain-helpers.js");

test("the hot water group exists and loads every DHW entity", () => {
  assert.ok(SETTINGS_GROUPS.some((group) => group.id === "dhw"));
  const hydration = new Set(getSettingsGroupHydrationKeys("dhw"));
  for (const key of DHW_SETTING_KEYS) {
    assert.ok(ENTITY_DEFS[key], `${key} has an entity definition`);
    assert.ok(ENTITY_DEFS[key].optional, `${key} is optional for upstream firmware`);
    assert.ok(hydration.has(key), `${key} is loaded for the hot water group`);
    assert.ok(SETTINGS_KEYS.includes(key), `${key} is a settings key`);
  }
});

test("Full Electric is detected on every page", () => {
  assert.ok(HEADER_ENTITY_KEYS.includes("dhwState"));
});

test("hot water settings are backed up, but operational switches are not", () => {
  assert.ok(SETTINGS_BACKUP_KEYS.includes("dhwStartTop"));
  assert.ok(SETTINGS_BACKUP_KEYS.includes("dhwRoomTempSource"));
  for (const key of ["dhwLockout", "dhwElementOnly"]) {
    assert.ok(SETTINGS_BACKUP_EXCLUDED_KEYS.has(key), `${key} is excluded`);
    assert.ok(!SETTINGS_BACKUP_KEYS.includes(key), `${key} is not in a backup section`);
  }
});

test("Full Electric follows the presence of the DHW state entity", () => {
  const previous = state.entities;
  try {
    state.entities = {};
    assert.equal(isFullElectric(), false);
    state.entities = { dhwState: { state: "IDLE_CV", value: "IDLE_CV" } };
    assert.equal(isFullElectric(), true);
  } finally {
    state.entities = previous;
  }
});

test("the bench test loads under Service and is never backed up", async () => {
  const { DHW_BENCH_KEYS, SETTINGS_BACKUP_KEYS: backupKeys } = await import("../js/src/core/config.js");
  const hydration = new Set(getSettingsGroupHydrationKeys("service"));
  for (const key of DHW_BENCH_KEYS) {
    assert.ok(ENTITY_DEFS[key]?.optional, `${key} is optional`);
    assert.ok(hydration.has(key), `${key} is loaded for Service`);
  }
  for (const key of ["dhwTestValve", "dhwTestElement"]) {
    assert.ok(!backupKeys.includes(key), `${key} is not backed up`);
    assert.ok(!SETTINGS_KEYS.includes(key), `${key} is not a settings key`);
  }
});

test("the bench test section only renders with the test switches", async () => {
  const { renderSettingsDhwBenchSection } = await import("../js/src/settings/dhw.js");
  const previous = state.entities;
  try {
    state.entities = {};
    assert.equal(renderSettingsDhwBenchSection(), "");
  } finally {
    state.entities = previous;
  }
});

test("the element placement and inline limits are hot water settings and backed up", () => {
  for (const key of ["dhwElementPlacement", "dhwInlineMinFlow", "dhwInlineMaxSupply"]) {
    assert.ok(ENTITY_DEFS[key]?.optional, `${key} is optional`);
    assert.ok(DHW_SETTING_KEYS.includes(key), `${key} is a hot water setting`);
    assert.ok(SETTINGS_BACKUP_KEYS.includes(key), `${key} is backed up`);
  }
});
