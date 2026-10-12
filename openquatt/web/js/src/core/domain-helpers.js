import { getEntityValue, hasEntity } from "./entity-store.js";
import { getBasePath } from "./url-path.js";

  export function buildEntityPath(domain, name, action = "") {
    const suffix = action ? `/${action}` : "";
    return `${getBasePath()}/${domain}/${encodeURIComponent(name)}${suffix}`;
  }

  export function isCurveMode(value = getEntityValue("strategy")) {
    return String(value || "").includes("Water Temperature Control");
  }

  export function isManualFlowMode(value = getEntityValue("flowControlMode")) {
    return String(value || "").toLowerCase().includes("manual");
  }

  // OpenQuatt FE (Full Electric): geen CV-ketel en geen hulprelais; herkend aan de DHW-regeling.
  export function isFullElectric() {
    return hasEntity("dhwState");
  }
