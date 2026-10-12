import { getEntityStateText, hasEntity } from "../core/app-shared.js";
import {
  getSettingsStatValue,
  renderNamedActionButton,
  renderSettingsAdvancedDisclosure,
  renderSettingsNumberField,
  renderSettingsSection,
  renderSettingsSelectField,
  renderSettingsStaticField,
  renderSettingsSwitchField,
} from "./controls.js";
import { t } from "../i18n/index.js";

// OpenQuatt FE: instellingengroep "Warm water". Elk veld is optioneel en
// verschijnt alleen als de firmware de entiteit heeft.

function number(key, title, copy) {
  return hasEntity(key) ? renderSettingsNumberField(key, title, copy) : "";
}

function toggle(key, title, copy) {
  return hasEntity(key) ? renderSettingsSwitchField(key, title, copy) : "";
}

function status(key, title, copy, value) {
  return hasEntity(key) ? renderSettingsStaticField(key, title, copy, value) : "";
}

function grid(fields) {
  const content = fields.filter(Boolean).join("");
  return content ? `<div class="oq-settings-grid">${content}</div>` : "";
}

function section(title, copy, body, headerActions = "") {
  return body ? renderSettingsSection(t("settingsDhw.sectionGroup"), title, copy, body, "", "", headerActions) : "";
}

function renderControlSection() {
  const flowGuard = grid([
    number("dhwFlowMin", t("settingsDhw.flowMinTitle"), t("settingsDhw.flowMinCopy")),
    number("dhwFlowMax", t("settingsDhw.flowMaxTitle"), t("settingsDhw.flowMaxCopy")),
  ]);
  return section(t("settingsDhw.controlSection"), t("settingsDhw.controlSectionCopy"), grid([
    toggle("dhwLockout", t("settingsDhw.lockoutTitle"), t("settingsDhw.lockoutCopy")),
    number("dhwStartTop", t("settingsDhw.startTopTitle"), t("settingsDhw.startTopCopy")),
    number("dhwHpStopTop", t("settingsDhw.hpStopTopTitle"), t("settingsDhw.hpStopTopCopy")),
    number("dhwHpLevel", t("settingsDhw.hpLevelTitle"), t("settingsDhw.hpLevelCopy")),
    number("dhwFlowSetpoint", t("settingsDhw.flowSetpointTitle"), t("settingsDhw.flowSetpointCopy")),
    number("dhwMinRest", t("settingsDhw.minRestTitle"), t("settingsDhw.minRestCopy")),
    toggle("dhwBoostAfterHp", t("settingsDhw.boostAfterHpTitle"), t("settingsDhw.boostAfterHpCopy")),
    number("dhwBoostTarget", t("settingsDhw.boostTargetTitle"), t("settingsDhw.boostTargetCopy")),
    toggle("dhwWindowEnable", t("settingsDhw.windowTitle"), t("settingsDhw.windowCopy")),
    number("dhwWindowStart", t("settingsDhw.windowStartTitle"), t("settingsDhw.windowStartCopy")),
    number("dhwWindowEnd", t("settingsDhw.windowEndTitle"), t("settingsDhw.windowEndCopy")),
  ]) + (flowGuard ? renderSettingsAdvancedDisclosure("dhw-flow-guard", t("settingsDhw.flowGuardTitle"), t("settingsDhw.flowGuardCopy"), flowGuard) : ""));
}

function renderLevelSection() {
  const mapping = grid([
    number("dhwCoilMapT1", t("settingsDhw.coilMapT1Title"), t("settingsDhw.coilMapT1Copy")),
    number("dhwCoilMapT2", t("settingsDhw.coilMapT2Title"), t("settingsDhw.coilMapT2Copy")),
    number("dhwCoilMapT3", t("settingsDhw.coilMapT3Title"), t("settingsDhw.coilMapT3Copy")),
    number("dhwCoilMapHyst", t("settingsDhw.coilMapHystTitle"), t("settingsDhw.coilMapHystCopy")),
  ]);
  const assist = grid([
    number("dhwAssistMaxLevel", t("settingsDhw.assistMaxLevelTitle"), t("settingsDhw.assistMaxLevelCopy")),
    number("dhwAssistMinRise", t("settingsDhw.assistMinRiseTitle"), t("settingsDhw.assistMinRiseCopy")),
    number("dhwAssistMaxDischarge", t("settingsDhw.assistMaxDischargeTitle"), t("settingsDhw.assistMaxDischargeCopy")),
    number("dhwAssistMaxWaterOut", t("settingsDhw.assistMaxWaterOutTitle"), t("settingsDhw.assistMaxWaterOutCopy")),
    number("dhwAssistBottomStop", t("settingsDhw.assistBottomStopTitle"), t("settingsDhw.assistBottomStopCopy")),
    number("dhwAssistWaterRelease", t("settingsDhw.assistWaterReleaseTitle"), t("settingsDhw.assistWaterReleaseCopy")),
  ]);
  return section(t("settingsDhw.levelSection"), t("settingsDhw.levelSectionCopy"), grid([
    toggle("dhwCoilMapEnable", t("settingsDhw.coilMapTitle"), t("settingsDhw.coilMapCopy")),
    number("dhwSoftStartStep", t("settingsDhw.softStartTitle"), t("settingsDhw.softStartCopy")),
    toggle("dhwSingleHpMode", t("settingsDhw.singleHpTitle"), t("settingsDhw.singleHpCopy")),
    number("dhwSingleHpBump", t("settingsDhw.singleHpBumpTitle"), t("settingsDhw.singleHpBumpCopy")),
    toggle("dhwAssistEnable", t("settingsDhw.assistTitle"), t("settingsDhw.assistCopy")),
  ])
    + (mapping ? renderSettingsAdvancedDisclosure("dhw-coil-map", t("settingsDhw.coilMapAdvancedTitle"), t("settingsDhw.coilMapAdvancedCopy"), mapping) : "")
    + (assist ? renderSettingsAdvancedDisclosure("dhw-assist", t("settingsDhw.assistAdvancedTitle"), t("settingsDhw.assistAdvancedCopy"), assist) : ""));
}

function renderBoostSection() {
  return section(t("settingsDhw.boostSection"), t("settingsDhw.boostSectionCopy"), grid([
    number("dhwBoostNowTarget", t("settingsDhw.boostNowTargetTitle"), t("settingsDhw.boostNowTargetCopy")),
    number("dhwBoostNowHpStop", t("settingsDhw.boostNowHpStopTitle"), t("settingsDhw.boostNowHpStopCopy")),
    toggle("dhwBoostHpAssist", t("settingsDhw.boostHpAssistTitle"), t("settingsDhw.boostHpAssistCopy")),
    number("dhwBoostAssistBottom", t("settingsDhw.boostAssistBottomTitle"), t("settingsDhw.boostAssistBottomCopy")),
    number("dhwBoostAssistStopTop", t("settingsDhw.boostAssistStopTopTitle"), t("settingsDhw.boostAssistStopTopCopy")),
  ]));
}

function renderLegionellaSection() {
  return section(t("settingsDhw.legionellaSection"), t("settingsDhw.legionellaSectionCopy"), grid([
    number("dhwLegionellaTarget", t("settingsDhw.legionellaTargetTitle"), t("settingsDhw.legionellaTargetCopy")),
    number("dhwLegionellaHandover", t("settingsDhw.legionellaHandoverTitle"), t("settingsDhw.legionellaHandoverCopy")),
    number("dhwLegionellaCeiling", t("settingsDhw.legionellaCeilingTitle"), t("settingsDhw.legionellaCeilingCopy")),
    toggle("dhwLegionellaCoilCirculation", t("settingsDhw.coilCirculationTitle"), t("settingsDhw.coilCirculationCopy")),
    status("dhwLegionellaLastRun", t("settingsDhw.legionellaLastRunTitle"), "", getEntityStateText("dhwLegionellaLastRun", "—")),
    status("dhwLegionellaNextRun", t("settingsDhw.legionellaNextRunTitle"), "", getEntityStateText("dhwLegionellaNextRun", "—")),
  ]));
}

function renderElementOnlySection() {
  return section(t("settingsDhw.elementOnlySection"), t("settingsDhw.elementOnlySectionCopy"), grid([
    toggle("dhwElementOnly", t("settingsDhw.elementOnlyTitle"), t("settingsDhw.elementOnlyCopy")),
    number("dhwElementOnlyTarget", t("settingsDhw.elementOnlyTargetTitle"), t("settingsDhw.elementOnlyTargetCopy")),
    number("dhwElementOnlyOffDelta", t("settingsDhw.elementOnlyOffDeltaTitle"), t("settingsDhw.elementOnlyOffDeltaCopy")),
  ]));
}

function renderTankSection() {
  const reset = hasEntity("dhwResetStandbyLoss") ? renderNamedActionButton("dhwResetStandbyLoss", t("settingsDhw.resetLearningButton")) : "";
  return section(t("settingsDhw.tankSection"), t("settingsDhw.tankSectionCopy"), grid([
    toggle("dhwTapDetectEnable", t("settingsDhw.tapDetectTitle"), t("settingsDhw.tapDetectCopy")),
    number("dhwTapThreshold", t("settingsDhw.tapThresholdTitle"), t("settingsDhw.tapThresholdCopy")),
    number("dhwTankVolume", t("settingsDhw.tankVolumeTitle"), t("settingsDhw.tankVolumeCopy")),
    hasEntity("dhwRoomTempSource") ? renderSettingsSelectField("dhwRoomTempSource", t("settingsDhw.roomTempSourceTitle"), t("settingsDhw.roomTempSourceCopy")) : "",
    number("dhwRoomTemp", t("settingsDhw.roomTempTitle"), t("settingsDhw.roomTempCopy")),
    status("dhwRoomTempActive", t("settingsDhw.roomTempActiveTitle"), "", getSettingsStatValue("dhwRoomTempActive", { decimals: 1 })),
    status("dhwUa", t("settingsDhw.uaTitle"), t("settingsDhw.uaCopy"), getSettingsStatValue("dhwUa", { decimals: 2 })),
    status("dhwUaSamples", t("settingsDhw.uaSamplesTitle"), "", getSettingsStatValue("dhwUaSamples", { decimals: 0 })),
  ]), reset);
}

export function renderSettingsDhwSections() {
  return [
    renderControlSection(),
    renderLevelSection(),
    renderBoostSection(),
    renderLegionellaSection(),
    renderElementOnlySection(),
    renderTankSection(),
  ];
}
