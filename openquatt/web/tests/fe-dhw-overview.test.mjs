import assert from "node:assert/strict";
import test from "node:test";

// OpenQuatt FE: de warmwaterkaart op het overzicht.
globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };

const { ENTITY_DEFS, FAST_OVERVIEW_KEYS, OVERVIEW_DHW_KEYS, OVERVIEW_KEYS } = await import("../js/src/core/config.js");
const { state } = await import("../js/src/core/state.js");
const { formatDhwFault, formatDhwState, formatFeControlModeLabel, getOverviewDhwModel, renderOverviewDhwPanel } = await import("../js/src/views/overview-dhw.js");
const { handleDhwAction, runDhwAction } = await import("../js/src/features/dhw-actions.js");

function withEntities(entities, fn) {
  const previous = state.entities;
  try {
    state.entities = entities;
    return fn();
  } finally {
    state.entities = previous;
  }
}

test("the hot water card keys are optional and loaded with the overview", () => {
  for (const key of OVERVIEW_DHW_KEYS) {
    assert.ok(ENTITY_DEFS[key]?.optional, `${key} is an optional entity`);
    assert.ok(OVERVIEW_KEYS.includes(key), `${key} is an overview key`);
    assert.ok(FAST_OVERVIEW_KEYS.includes(key), `${key} is a fast overview key`);
  }
});

test("the card is hidden without the DHW state entity", () => {
  withEntities({}, () => assert.equal(renderOverviewDhwPanel(), ""));
});

test("DHW states, faults and FE control modes are translated", () => {
  assert.notEqual(formatDhwState("DHW_HEAT_PUMP"), "DHW_HEAT_PUMP");
  assert.notEqual(formatDhwFault("VALVE_MISMATCH"), "VALVE_MISMATCH");
  assert.notEqual(formatFeControlModeLabel("CM10 - Hot water"), "CM10 - Hot water");
  assert.notEqual(formatFeControlModeLabel("CM11 - Element only"), "CM11 - Element only");
  assert.equal(formatFeControlModeLabel("CM2 - Heating"), "CM2 - Heating");
});

test("clear fault only appears while a fault is active", () => {
  const base = {
    dhwState: { state: "DHW_HEAT_PUMP", value: "DHW_HEAT_PUMP" },
    dhwFault: { state: "NONE", value: "NONE" },
    dhwClearFault: { state: "", value: "" },
  };
  withEntities(base, () => {
    assert.ok(!getOverviewDhwModel().actions.some((action) => action.key === "dhwClearFault"));
  });
  withEntities({ ...base, dhwState: { state: "FAULT", value: "FAULT" }, dhwFault: { state: "TIMEOUT", value: "TIMEOUT" } }, () => {
    const model = getOverviewDhwModel();
    assert.ok(model.actions.some((action) => action.key === "dhwClearFault"));
    assert.ok(model.rows.some((row) => row.tone === "warn"));
  });
});

test("a declined confirmation changes nothing", () => {
  const button = { dataset: { oqDhwKey: "dhwBoostNow" } };
  withEntities({ dhwBoostNow: { state: false, value: false } }, () => {
    let asked = "";
    assert.equal(runDhwAction("dhw-overview-action", button, (message) => { asked = message; return false; }), true);
    assert.ok(asked.length > 0);
  });
  assert.equal(handleDhwAction("something-else", button), false);
});

test("CM10/CM11 decision log reasons have their own label", async () => {
  const { getControlReplayIncidentReasonMeta } = await import("../js/src/features/control-replay-incidents.js");
  for (const code of ["dhw_request", "element_only", "dhw_request_cleared"]) {
    const meta = getControlReplayIncidentReasonMeta(code);
    assert.ok(meta, `${code} has a meta entry`);
  }
});

test("a nearly full NVS shows a warning on the hot water card", () => {
  const base = { dhwState: { state: "IDLE_CV", value: "IDLE_CV" } };
  withEntities({ ...base, nvsAvailable: { state: "168", value: 168 } }, () => {
    assert.ok(!getOverviewDhwModel().rows.some((row) => row.tone === "warn"));
  });
  withEntities({ ...base, nvsAvailable: { state: "12", value: 12 } }, () => {
    assert.ok(getOverviewDhwModel().rows.some((row) => row.tone === "warn"));
  });
  withEntities(base, () => {
    assert.ok(!getOverviewDhwModel().rows.some((row) => row.tone === "warn"));
  });
});
