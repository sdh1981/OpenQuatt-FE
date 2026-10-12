import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { commitSwitch, triggerNamedButton } from "../core/entity-write-actions.js";
import { t } from "../i18n/index.js";

// OpenQuatt FE: knoppen op de warmwaterkaart. Elke actie vraagt eerst om bevestiging.

const DHW_SWITCH_CONFIRM_KEYS = Object.freeze({
  dhwBoostNow: ["overviewDhw.confirmBoostStart", "overviewDhw.confirmBoostStop"],
  dhwLegionellaForce: ["overviewDhw.confirmLegionellaStart", "overviewDhw.confirmLegionellaStop"],
  dhwLockout: ["overviewDhw.confirmLockoutStart", "overviewDhw.confirmLockoutStop"],
});

export function handleDhwAction(action, button) {
  return runDhwAction(action, button, (message) => window.confirm(message));
}

export function runDhwAction(action, button, confirm) {
  if (action !== "dhw-overview-action") {
    return false;
  }
  const key = String(button?.dataset.oqDhwKey || "");
  if (!hasEntity(key)) {
    return true;
  }
  if (key === "dhwClearFault") {
    if (confirm(t("overviewDhw.confirmClearFault"))) {
      void triggerNamedButton(key);
    }
    return true;
  }
  const messages = DHW_SWITCH_CONFIRM_KEYS[key];
  if (!messages) {
    return true;
  }
  const active = isEntityActive(key);
  if (confirm(t(active ? messages[1] : messages[0]))) {
    void commitSwitch(key, !active);
  }
  return true;
}
