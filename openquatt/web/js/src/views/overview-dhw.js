import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { isFullElectric } from "../core/domain-helpers.js";
import { getEntityValue } from "../core/entity-store.js";
import { getRenderSignature } from "../core/render-signatures.js";
import { escapeHtml } from "../core/html.js";
import { formatNumber, t } from "../i18n/index.js";
import { state } from "../core/state.js";

// OpenQuatt FE: warmwaterkaart op het overzicht. Verschijnt alleen als de
// firmware de DHW-entiteiten heeft.

const DHW_STATE_LABEL_KEYS = Object.freeze({
  IDLE_CV: "overviewDhw.stateIdle",
  DHW_PREPARE: "overviewDhw.statePrepare",
  DHW_HEAT_PUMP: "overviewDhw.stateHeatPump",
  DHW_BOOST: "overviewDhw.stateBoost",
  LEGIONELLA: "overviewDhw.stateLegionella",
  FAULT: "overviewDhw.stateFault",
});

const DHW_FAULT_LABEL_KEYS = Object.freeze({
  SENSOR_IMPLAUSIBLE: "overviewDhw.faultSensor",
  HP_FAULT: "overviewDhw.faultHp",
  VALVE_STUCK_CV: "overviewDhw.faultValveStuck",
  VALVE_MISMATCH: "overviewDhw.faultValveMismatch",
  FLOW_OUT_OF_RANGE: "overviewDhw.faultFlow",
  TIMEOUT: "overviewDhw.faultTimeout",
  LOCKOUT: "overviewDhw.faultLockout",
  ELEMENT_NO_FLOW: "overviewDhw.faultElementNoFlow",
});

const DHW_VALVE_LABEL_KEYS = Object.freeze({
  CV: "overviewDhw.valveCv",
  DHW: "overviewDhw.valveDhw",
});

// Firmwarelabels voor de FE-regelmodi; de rest blijft zoals de firmware hem geeft.
const FE_CONTROL_MODE_LABEL_KEYS = Object.freeze({
  "CM10 - Hot water": "overviewDhw.cm10",
  "CM11 - Element only": "overviewDhw.cm11",
});

export function formatFeControlModeLabel(label) {
  const key = FE_CONTROL_MODE_LABEL_KEYS[String(label || "").trim()];
  return key ? t(key) : label;
}

function rawText(key) {
  return String(getEntityValue(key) ?? "").trim();
}

export function formatDhwState(value) {
  const key = DHW_STATE_LABEL_KEYS[value];
  return key ? t(key) : value || "—";
}

export function formatDhwFault(value) {
  if (!value || value === "NONE") return t("overviewDhw.faultNone");
  const key = DHW_FAULT_LABEL_KEYS[value];
  return key ? t(key) : value;
}

function formatEta(minutes) {
  if (Number.isNaN(minutes)) return "—";
  if (minutes <= 0) return t("overviewDhw.etaReady");
  if (minutes < 60) return t("overviewDhw.etaMinutes", { minutes: formatNumber(Math.round(minutes), 0) });
  const hours = Math.floor(minutes / 60);
  return t("overviewDhw.etaHours", { hours, minutes: String(Math.round(minutes % 60)).padStart(2, "0") });
}

// Zelfde grens als kNvsCriticalEntries in oq_fe_io_logic.h.
export const NVS_CRITICAL_ENTRIES = 20;

export function isNvsCritical() {
  const available = getEntityNumericValue("nvsAvailable");
  return hasEntity("nvsAvailable") && !Number.isNaN(available) && available < NVS_CRITICAL_ENTRIES;
}

function onOff(key) {
  return isEntityActive(key) ? t("overviewDhw.on") : t("overviewDhw.off");
}

export function getOverviewDhwModel() {
  const dhwState = rawText("dhwState");
  const fault = rawText("dhwFault");
  const valve = rawText("dhwValvePosition");
  const faultActive = dhwState === "FAULT" || (fault !== "" && fault !== "NONE");
  const rows = [
    hasEntity("dhwTankTop") && { label: t("overviewDhw.tankTop"), value: getEntityStateText("dhwTankTop") },
    hasEntity("dhwTankBottom") && { label: t("overviewDhw.tankBottom"), value: getEntityStateText("dhwTankBottom") },
    { label: t("overviewDhw.state"), value: formatDhwState(dhwState) },
    faultActive && hasEntity("dhwFault") && { label: t("overviewDhw.fault"), value: formatDhwFault(fault), tone: "warn" },
    hasEntity("dhwEtaReady") && { label: t("overviewDhw.eta"), value: formatEta(getEntityNumericValue("dhwEtaReady")) },
    hasEntity("dhwElementActive") && { label: t("overviewDhw.element"), value: onOff("dhwElementActive") },
    hasEntity("dhwValvePosition") && { label: t("overviewDhw.valve"), value: DHW_VALVE_LABEL_KEYS[valve] ? t(DHW_VALVE_LABEL_KEYS[valve]) : t("overviewDhw.valveUnknown") },
    hasEntity("dhwTapsToday") && { label: t("overviewDhw.tapsToday"), value: getEntityStateText("dhwTapsToday") },
    isNvsCritical() && { label: t("overviewDhw.nvsLow"), value: t("overviewDhw.nvsLowValue", { entries: Math.round(getEntityNumericValue("nvsAvailable")) }), tone: "warn" },
  ].filter(Boolean);

  const actions = [
    hasEntity("dhwBoostNow") && {
      key: "dhwBoostNow",
      active: isEntityActive("dhwBoostNow"),
      label: isEntityActive("dhwBoostNow") ? t("overviewDhw.boostStop") : t("overviewDhw.boostStart"),
    },
    hasEntity("dhwLegionellaForce") && {
      key: "dhwLegionellaForce",
      active: isEntityActive("dhwLegionellaForce"),
      label: isEntityActive("dhwLegionellaForce") ? t("overviewDhw.legionellaStop") : t("overviewDhw.legionellaStart"),
    },
    hasEntity("dhwLockout") && {
      key: "dhwLockout",
      active: isEntityActive("dhwLockout"),
      label: isEntityActive("dhwLockout") ? t("overviewDhw.lockoutStop") : t("overviewDhw.lockoutStart"),
    },
    faultActive && hasEntity("dhwClearFault") && {
      key: "dhwClearFault",
      active: false,
      label: t("overviewDhw.clearFault"),
    },
  ].filter(Boolean);

  return {
    title: t("overviewDhw.title"),
    copy: isEntityActive("dhwLockout") ? t("overviewDhw.copyLockout") : t("overviewDhw.copy"),
    rows,
    actions,
    busy: state.busyAction,
  };
}

export function getOverviewDhwRenderSignature(model = getOverviewDhwModel()) {
  return getRenderSignature(model);
}

export function renderOverviewDhwPanel() {
  if (!isFullElectric()) return "";
  const model = getOverviewDhwModel();
  const disabled = state.busyAction ? " disabled" : "";
  return `
    <section class="oq-overview-dhw" data-render-signature="${escapeHtml(getOverviewDhwRenderSignature(model))}">
      <div class="oq-overview-system-copy"><h3>${escapeHtml(model.title)}</h3><p>${escapeHtml(model.copy)}</p></div>
      <div class="oq-overview-dhw-list">
        ${model.rows.map((row) => `
          <div class="oq-overview-row${row.tone === "warn" ? " oq-overview-row--warn" : ""}">
            <span>${escapeHtml(row.label)}</span>
            <strong>${escapeHtml(row.value)}</strong>
          </div>
        `).join("")}
      </div>
      ${model.actions.length ? `
        <div class="oq-overview-dhw-actions">
          ${model.actions.map((action) => `
            <button class="oq-overview-controlpanel-toggle${action.active ? " is-active" : ""}" type="button" data-oq-action="dhw-overview-action" data-oq-dhw-key="${escapeHtml(action.key)}"${disabled}>${escapeHtml(action.label)}</button>
          `).join("")}
        </div>
      ` : ""}
    </section>
  `;
}
