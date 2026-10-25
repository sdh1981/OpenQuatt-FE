import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";

export const ODU_FIRMWARE_PAGE = "/openquatt/odu-firmware";

// A live build capability, never inferred from the hardware or cached entities.
// Missing metadata on older firmware hides the link as well.
export function applyOduFirmwareCapability(payload) {
  state.oduFirmwareAvailable = payload?.capabilities?.odu_firmware === true;
}

export function renderOduFirmwareMenuLink() {
  if (!state.oduFirmwareAvailable) return "";
  return `<a class="oq-helper-hub-action" data-oq-odu-firmware-link href="${ODU_FIRMWARE_PAGE}">${escapeHtml(t("settingsService.oduFirmwareLabel"))}</a>`;
}
