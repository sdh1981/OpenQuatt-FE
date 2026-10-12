# Voorstel fase 5: web-app en HA-dashboard

Status: **voorstel**, nog niet gebouwd (2026-10-11).
Basis: `main` op `c1db511d` (`v0.53.0-fe.6`).

De FE-firmware heeft nu zo'n 70 DHW-entiteiten. Jeroens web-app op `http://openquatt.local` toont daar nog niets van. Ook de CV-ketel staat er nog in, terwijl FE hem vast uitschakelt. Bedienen kan nu alleen via Home Assistant of de REST-API. Fase 5 lost dat op.

---

## 1. Hoe de web-app werkt (relevant voor FE)

| Onderdeel | Wat het betekent voor FE |
|---|---|
| `core/config.js` → `ENTITY_DEFS` | De app spreekt entiteiten aan **op naam**, via de ESPHome-REST-API. Ontbrekende entiteiten mogen ("optional"). FE-entiteiten kunnen er dus bij zonder firmware-API-werk |
| `core/config.js` → `SETTINGS_GROUPS` | Vaste groepen: Installatie, Verwarmen, Koelen, Integraties, Service, Systeem. Een groep "Warm water" erbij is één regel plus de rendering |
| `settings/*.js`, `views/*.js` | Rendering per groep en view, met bestaande veldmodellen (`field-models.js`) en statkaarten (`stat-card.js`) |
| `i18n/nl.js` + `en.js` | **Alle** zichtbare tekst in beide talen; CI controleert de pariteit |
| `SETTINGS_BACKUP_SECTIONS` | Elke schrijfbare entiteit moet in een backup-sectie staan of expliciet uitgesloten zijn (`check-settings-backup.mjs`, CI) |
| `web-budgets.mjs` | Per PR mag de gecomprimeerde JS maar **7,5 KB of 3%** groeien |
| Build | `npm run build:web`, `check:web` en `smoke:web`. Lokaal is er nog geen Node. Mijn voorstel: Node via pip (`nodejs-wheel`) in de scratchpad, net als ESPHome en clang-format |

Upstream's richtlijn (`openquatt/web/README.md`) geldt ook voor FE: bestaande componenten hergebruiken, geen globale CSS-overrides, beide talen, en live updates mogen drafts en focus niet verstoren.

---

## 2. Web-app: wat erbij komt

### 5a. Instellingengroep "Warm water" (en de ketel eruit)

Een nieuwe groep **Warm water** (icoon `water-boiler`) met deze secties, opgebouwd uit de bestaande veldmodellen:

| Sectie | Entiteiten |
|---|---|
| Regeling | start top, HP stop top, HP level, Flow Setpoint DHW, flow min/max, minimum rest, boost after HP + boost target, tijdvenster (aan + start/einde) |
| Niveau | coil-in-mapping (aan + T1/T2/T3 + hysterese), soft start step, single-HP-modus + bump, tweede-HP-assist (aan + max level, min rise, discharge, water out, bottom stop, release) |
| Snelboost | boost now target, boost now HP stop |
| Legionella | doel, HP handover, HP top ceiling, coil circulation, laatste/volgende run |
| Element-only | DHW Element Only Heating, doel, off-delta |
| Tank | tapdetectie (aan + drempel), tankvolume, ruimtetemperatuur (bron + waarde), reset standby-loss learning |

**Ketel-UI verbergen in FE.** FE wordt herkend aan de aanwezigheid van een FE-entiteit ("DHW state"); er komt geen aparte web-build voor FE. Verborgen worden:
- de quickstart-stap "CV-ketel";
- de sectie CV-ketel en de sectie hulprelais (R2) onder Installatie;
- de ketel- en OTB-onderdelen onder Integraties.

**Backup:** een nieuwe backup-sectie "Warm water" met alle DHW-instellingen. De proefschakelaars, de snelboost-knop en de legionella-force worden uitgesloten, net als upstream's eenmalige acties.

### 5b. Warmwaterkaart op het overzicht

Een kaart op **Overzicht**, met de bestaande `stat-card`:

| Weergave | Bron |
|---|---|
| Tank top / tank bottom | DHW Tank Top, DHW Tank Bottom |
| Toestand en storing | DHW state, DHW fault (met vertaling, bijvoorbeeld `DHW_HEAT_PUMP` → "Warmtepomp verwarmt") |
| Klaar over | DHW estimated time to ready (alleen tijdens verwarmen) |
| Element, klep | DHW Element Active, DHW Valve Position |
| Tappingen vandaag | DHW tappingen vandaag |
| Acties | **Snelboost**, **Legionella nu**, **Storing wissen**, **Lockout** (met bevestiging, via de bestaande action-router) |

De CM-namen "CM10 - Hot water" en "CM11 - Element only" krijgen een NL/EN-vertaling in de header en de control-replay.

### 5c. Bankproef onder Service

Een sectie **Bankproef warm water** onder Service:
- de knoppen DHW Test Valve en DHW Test Element, met hun status;
- de klepstand (terugmelding);
- de 1-Wire-adressen.

Zo heb je voor de bankproef geen HA of `curl` meer nodig.

### Bewust (nog) niet

- **Decision log voor CM10/CM11.** Overgangen naar CM10/CM11 krijgen nu reden "unknown". Een eigen reden vraagt een wijziging in de C++-component `openquatt_decision_log` (nieuwe reason-codes), plus de webkoppeling. Voorstel: later, als aparte stap.
- **Een eigen DHW-grafiek of -trend.** De bestaande trendopslag kent DHW niet; dat voegt flash- en PSRAM-gebruik toe. Pas na een meting.

---

## 3. HA-dashboard

Upstream heeft de HA-dashboards verplaatst naar [OpenQuatt/home-assistant-openquatt](https://github.com/OpenQuatt/home-assistant-openquatt) (`dashboards/duo-nl.yaml`). FE levert daarom alleen aanvullingen, in `fe/dashboard/`:

| Bestand | Inhoud |
|---|---|
| `openquatt_ha_dhw_hotwater_package.yaml` | Uit de LilyGO-build: berekende sensoren (gemiddelde tanktemperatuur, bruikbaar warm water, douchetijd, tanklading). De entity-id's die het gebruikt, bestaan in FE onder dezelfde naam |
| `openquatt_dhw_hotwater_card.yaml` + `…_buttoncard.yaml` | Uit de LilyGO-build: de warmwatervoorraadkaarten (apexcharts-card, mini-graph-card) |
| `fe-warmwater-view.yaml` | **Nieuw:** een complete view "Warm water" om aan het upstream Duo-dashboard toe te voegen. Daarin: status, tanktemperaturen, acties, de belangrijkste instellingen, element-only, legionella en de bankproef |
| `README.md` | Installatie: het package in `packages/`, de view plakken, en de vereiste HACS-kaarten |

Een controle hoort erbij: ik loop elke entity-id in die bestanden na tegen de FE-entiteitnamen (`sensor.openquatt_dhw_…`). Wat in FE anders heet of niet meer bestaat (coil in/uit, tarief, PV), haal ik weg.

**Automatiseringen** uit de LilyGO-build die op CM4/CM6 letten, moeten naar CM10/CM11. Dat zet ik in de README; ik kan ze niet zelf aanpassen.

---

## 4. Volgorde en PR's

Vanwege het groeibudget van 7,5 KB per PR in drie web-PR's plus één HA-PR:

| PR | Inhoud | Verwachte groei (gzip) |
|---|---|---|
| 5a | Groep "Warm water", ketel-UI verbergen, backup-sectie, NL/EN | ~5–7 KB |
| 5b | Warmwaterkaart op het overzicht, CM10/CM11-labels | ~3–4 KB |
| 5c | Bankproef onder Service | ~1–2 KB |
| 5d | HA-bestanden in `fe/dashboard/` | 0 (geen firmware) |

Elke web-PR draait `npm run check:web`, `smoke:web` en `build-assets.mjs --check` lokaal, plus de firmware-compile in CI. Ik vermeld de raw/gzip-groei tegenover `main`. Een browsertest doe ik met de preview-build en de mock-device van upstream (`dev.html`). Op de echte Q test jij, want web-app-writes naar een echt apparaat doe ik niet zonder jouw toestemming.

---

## 5. Beslissingen (2026-10-11)

1. **Plaats:** een instellingengroep **Warm water** plus een kaart op **Overzicht**.
2. **Ketel-UI verbergen** in FE: ja.
3. **Bankproef in de app** onder Service: ja.
4. **Decision log** voor CM10/CM11: nu meenemen (component plus web).
5. **HA:** de kaarten en het package uit de LilyGO-build overnemen, plus een nieuwe view "Warm water".
6. **Lokale Node** via pip in de scratchpad: ja.

### Uitwerkingen tijdens de bouw van 5a

- **Laden van de entiteiten.** De app laadt per instellingengroep alleen de sleutels uit `SETTINGS_GROUP_KEY_MAP`. "Warm water" krijgt daarom een eigen lijst (`DHW_SETTING_KEYS`). `dhwState` staat in `HEADER_ENTITY_KEYS`, zodat de FE-herkenning op elke pagina klopt.
- **Bundlebudget.** De bundle zat al vlak tegen upstream's absolute budget: nog ~3,4 KB raw over. FE verhoogt het budget daarom eenmalig voor heel fase 5: raw 1,29 → 1,34 MB, gzip-plafond 362 → 375 kB. De groeigrens per PR (7,5 KB of 3%) blijft staan. 5a groeit 15,5 KB raw / 4,4 kB gzip.
- **Nederlandse firmwarenamen.** Drie firmwarenamen van DHW-entiteiten zijn Nederlands, zoals "DHW ruimtetemperatuur bron". Ze staan in de uitzonderingenlijst van de i18n-test, net als upstream's "Uurdetail bewaren".
- **Lokale web-checks.** Die vragen een LF-checkout (`core.autocrlf=false`). Eén upstream-test faalt alleen op Windows (`path.relative` geeft backslashes); in CI op Linux slaagt hij.
