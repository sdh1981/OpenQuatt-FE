import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, setTimeout, clearTimeout,
  localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { applyOduFirmwareCapability, renderOduFirmwareMenuLink } = await import("../js/src/features/odu-firmware.js");
const { renderSettingsOduFirmwareRow } = await import("../js/src/settings/service.js");
const { getHeaderRenderSignature, patchHeaderDom } = await import("../js/src/features/header-status.js");
const { getSettingsRenderSignature } = await import("../js/src/core/render-signatures.js");

test("ODU links are hidden on absent, legacy, false and malformed capabilities", () => {
  for (const payload of [null, {}, { capabilities: {} }, { capabilities: { odu_firmware: false } },
    { capabilities: { odu_firmware: "true" } }, { capabilities: { odu_firmware: 1 } }]) {
    applyOduFirmwareCapability(payload);
    assert.equal(renderOduFirmwareMenuLink(), "");
    assert.equal(renderSettingsOduFirmwareRow(), "");
  }
});

test("NL/EN navigation opens the protected local page without a write action", () => {
  applyOduFirmwareCapability({ capabilities: { odu_firmware: true } });
  for (const [locale, label] of [["nl", "ODU-firmware bijwerken"], ["en", "Update ODU firmware"]]) {
    setLocale(locale, { persist: false });
    for (const html of [renderOduFirmwareMenuLink(), renderSettingsOduFirmwareRow()]) {
      assert.match(html, /href="\/openquatt\/odu-firmware"/);
      assert.ok(html.includes(label));
      assert.doesNotMatch(html, /data-oq-action|\/start|\/image|onclick|<form/);
    }
  }
  // A newer/older firmware response replaces the live capability, not a cache.
  applyOduFirmwareCapability({ entities: {} });
  assert.equal(renderOduFirmwareMenuLink(), "");
  setLocale("nl", { persist: false });
});

test("live capability changes invalidate both menus, including an already open header", () => {
  state.interfacePanelOpen = true;
  state.systemModal = "";
  applyOduFirmwareCapability({});
  const headerBefore = getHeaderRenderSignature();
  const settingsBefore = getSettingsRenderSignature();
  applyOduFirmwareCapability({ capabilities: { odu_firmware: true } });
  assert.notEqual(getHeaderRenderSignature(), headerBefore);
  assert.notEqual(getSettingsRenderSignature(), settingsBefore);
  state.root = { querySelector: () => null };
  assert.equal(patchHeaderDom(), false);
  state.root = null;
  state.interfacePanelOpen = false;
});

test("bulk endpoint advertises the compile capability and entity synchronization consumes it", async () => {
  const [codegen, backend, sync] = await Promise.all([
    readFile(new URL("../../../components/openquatt_odu_firmware/__init__.py", import.meta.url), "utf8"),
    readFile(new URL("../../../components/openquatt_entities/OpenQuattEntities.cpp", import.meta.url), "utf8"),
    readFile(new URL("../js/src/core/entity-sync.js", import.meta.url), "utf8"),
  ]);
  assert.match(codegen, /cg\.add_define\("USE_OPENQUATT_ODU_FIRMWARE"\)/);
  assert.match(backend, /#ifdef USE_OPENQUATT_ODU_FIRMWARE/);
  assert.match(backend, /"odu_firmware":true/);
  assert.match(backend, /"odu_firmware":false/);
  assert.match(sync, /applyOduFirmwareCapability\(payload\)/);
});
