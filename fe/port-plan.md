# Portplan: DHW-boilerlaag op upstream OpenQuatt + Heatpump Controller Q

Opgesteld 2026-10-01 als onderzoek, daarna overgenomen als plan voor deze repo.
Basis: upstream `OpenQuatt/OpenQuatt` tag **v0.53.0** (`b6a137a9`, 30-09-2026).
Bron van de DHW-laag: `sdh1981/OpenQuatt-with-DHW-system` (LilyGO-build, `project_version` v0.65.0).

Voortgang per fase staat in [README.md](README.md).

---

## 1. Conclusie

Met de Q valt het grootste probleem weg. De Q is het enige doel dat upstream bouwt en test. Je hoeft dus geen eigen hardwareprofiel te onderhouden, en de Q-functies doen gewoon mee: lokale PT1000, flowpuls, OpenTherm, Ethernet, Power House-learning en de CiC-compatibiliteit op M2.

Twee obstakels blijven, en er komt er één bij:

1. **De naam "boiler" en CM4 botsen** (dit blijft). Bij jou is `oq_boiler_control.yaml` de DHW-regelaar en betekent CM4 warm water. Upstream is "boiler" de CV-ketel via R1 of OpenTherm, en betekent CM4 *boiler-only fallback*.
2. **Upstream heeft de regellogica van YAML-lambda's naar C++-runtimes verplaatst** (dit blijft). Je DHW-haken in supervisory, thermal request, flow, Power House en cooling moet je opnieuw inbouwen.
3. **Nieuw: de I/O van de Q is vol.** De DHW-laag vraagt nu 3 uitgangen/ingangen plus een derde RS485-bus (zie §3). De Q heeft:
   - twee relais (R1 en R2),
   - één 1-Wire-klem (T),
   - twee RS485-poorten: M1 voor de buitenunits en M2 (upstream: CiC-server; bij jou vrij, zie §3),
   - geen vrije GPIO-klemmen. Alleen GPIO8, GPIO17 en GPIO43/44 zijn op de ESP vrij, en het is onbekend of die ergens op de print bereikbaar zijn.

**Inschatting:** het is nog steeds een port en geen merge. Software is het grootste werk (naad met het regelhart en de web-app). Maar eerst moet het I/O-plan vaststaan, want dat bepaalt hoe de DHW-package eruitziet.

---

## 2. Wat "jouw boilerlaag" is

Dit volgt uit `scripts/apply_boiler_overlay.ps1`, plus de CWT-module:

| Bestand in fork | Rol | Upstream-tegenhanger |
|---|---|---|
| `openquatt/oq_boiler_control.yaml` (4453 r.) | DHW-regelaar: klep, element, legionella, snelboost, tapdetectie, instellingen | Zelfde naam, maar is de **CV-ketel** (359 r.) → naambotsing |
| `openquatt/includes/oq_dhw_controller_logic.h` (766 r.) | Pure logica, host-getest | Geen |
| `openquatt/oq_cwt_pt_module.yaml` | CWT-TM-8PT op RS485 #3 (9600 8N1, slave 1): tank top/bodem, coil in/uit, T-aanvoer | Vervalt op de Q (01-10): tank bottom wordt een DS18B20 op de T-klem |
| `openquatt/oq_supervisory_controlmode.yaml` | DHW-vraag → CM4, element-only CM6, ontluchten met klep | C++: `includes/control/oq_hp_supervisory_logic.h`, `oq_supervisory_state_logic.h` |
| `openquatt/oq_thermal_request_control.yaml` | DHW-niveau, single-lead-HP, strategie 4 | `oq_thermal_request_logic.h` / `_runtime.h` |
| `openquatt/oq_flow_control.yaml` | `oq_flow_setpoint_dhw_lph` | `oq_flow_control_logic.h` (geen DHW-setpoint) |
| `oq_power_house_strategy.yaml`, `oq_cooling_strategy.yaml` | DHW-blokkades | Power House- en cooling-runtime in C++ |
| Web-app (`00-config.js`, `20-overview.js`, `40-heatpump.js`) | DHW-kaarten en instellingen | Nieuwe modulaire app met i18n |
| `tests/host/oq_dhw_snelboost_test.cpp` | Host-test | Upstream-testinfra |

Kleinere haken zitten in `oq_smart_diagnostics`, `oq_cic_modbus_slave`, `oq_config_snapshot`, `oq_cycling_monitor`, `oq_adaptive_heating`, `oq_HP_io`, `oq_discharge_protection` en `oq_supply_temp_protection`.

---

## 3. I/O-plan op de Q (eerst beslissen)

Wat de DHW-laag nu op de LilyGO gebruikt, en de Q-opties daarvoor:

| DHW-functie | Nu (LilyGO) | Op de Q | Advies |
|---|---|---|---|
| **3-wegklep** (bekrachtigen = boilerpad) | relais GPIO40 | **R2** (wisselrelais NC/COM/NO) | **Besloten (07-10): R2.** FE neemt de fysieke uitgang over via een template-output met dezelfde id; schakelaar **R2 is DHW valve**. Upstream blijft ongewijzigd |
| **Contactorspoel 3 kW-element** | relais GPIO48 | **R1** (COM/NO). Upstream is R1 de CV-ketel. **Besloten (01-10): Full Electric, geen ketel**, dus R1 is vrij | **R1.** Upstream declareert `boiler_relay_out` op GPIO16 altijd (in `oq_boiler_control.yaml`), en ESPHome weigert een tweede output op dezelfde pin. Het DHW-element gebruikt daarom diezelfde output, met één eigenaar: DHW mag R1 alleen schakelen zolang **"Auxiliary heat source connected"** (`oq_aux_heat_source_present`) uit staat. Upstream schakelt dan zelf al assist en fault-fallback uit en zet het ketel-transport slapend (CM3/CM4 komen niet voor). De OTB-klem blijft ongebruikt |
| **Tank bottom** | CWT kanaal 2 | **T-klem**: DS18B20 op 1-Wire, GPIO18 (`+3.3V`/`GND`/`DATA`) | **Besloten (01-10):** DS18B20 op T. De T-klem is daarmee bezet. Upstream gebruikt dezelfde bus voor een optionele DS18B20 als lokale aanvoertemperatuur, dus de DHW-sensor wordt een tweede `dallas_temp`-sensor op de bestaande `one_wire`-bus, met een vast adres. Laat **Lokale aanvoertemperatuur** op `PT1000` staan |
| **Klep-terugmelding** (droog contact, aux) | drive GPIO11 + sense GPIO13 | **Tweakers-connector boven**: `3.3V – GPIO43 – GPIO44 – GND` | **Besloten (07-10): GPIO44 + GND**, interne pull-up, contact gesloten = DHW-stand. GPIO43/44 zijn UART0; GPIO43 (TX) wordt bij elke start door de ROM-bootloader aangestuurd, dus een contact naar GND zou daar kortsluiting geven. De logger staat op USB (`USB_SERIAL_JTAG`, nu ook expliciet in `fe_io.yaml`). GPIO15 en de flowpuls blijven ongemoeid upstream |
| **Tank top** | CWT kanaal 1 | **T-klem**: tweede DS18B20 op dezelfde 1-Wire-bus | **Besloten (01-10):** tweede DS18B20, eigen vast adres. De regellogica blijft ongewijzigd |
| **Coil in/uit** | CWT kanaal 3/4 | — | Vervalt. De logica behandelt `NAN` als "niet geïnstalleerd", dus er gaat niets kapot. Wat je verliest: de coil-ΔT-plausibiliteit en de rendement/tapdetectie op basis van de coil (zie `dhw-rendement-en-tapdetectie-v0.54.md`) |
| **T-aanvoer (PT1000)** | CWT kanaal 5 | Eigen PT1000 op de Q-stekker (pin 2/5) | Upstream Q-route |
| ~~CWT-TM-8PT~~ | RS485 #3 | — | **Vervalt (01-10).** M1 en M2 blijven ongedeeld, en het risico met de comms-watchdog op M1 is weg |
| **M2** (RS485 #2) | CiC-feedback | Upstream: CiC-server | **Besloten (01-10): CiC vervalt, M2 is vrij.** Een reserve voor een eventuele Modbus-module, bijvoorbeeld om de coil in/uit-sensoren later terug te brengen. Let op: de UART op M2 wordt nog door het upstream CiC-package gedeclareerd. Een eigen device op M2 moet die `uart_bus` dus delen of het package vervangen |

**Tank top: besloten (01-10), route A, een tweede DS18B20.** Waarom:
- `tank_top_c` is verplicht in `oq_dhw_controller_logic.h` (anders `SENSOR_IMPLAUSIBLE`). Daarop draaien de start (46 °C), boost, het HP-plafond (55 °C, tegen hoge condensordruk) en de legionella-hold (68 °C / 15 min). Het element zit bij de top.
- Bottom-only zou de legionella-controle onbetrouwbaar maken, meer pendelen geven na het tappen, en betekent een tweede grote verandering tijdens de port.

Montage:
- Beide DS18B20's in de dompelhulzen waar de PT1000's van CWT-kanaal 1 en 2 zaten, met warmtegeleidende pasta.
- Controleer of de Q een pull-up van 4,7 kΩ op DATA heeft.
- Bekabel lineair, geen ster, en kort, vanwege de 3,3 V-bus.

**DS18B20-valkuil (fase 3):** een DS18B20 geeft na een spanningsreset eenmalig **85,0 °C**. De DHW-logica accepteert alles van −10 tot en met 85 °C als plausibel (`temp_min_c` / `temp_max_c`). Die opstartwaarde zou dus als echte tank-top-temperatuur tellen. Gevolg: geen start (de tank lijkt heet), en in het ergste geval een legionella-hold (≥ 68 °C) die onterecht als voltooid wordt geboekt. Oplossing in de YAML-laag, zonder logicawijziging: een filter op beide `dallas_temp`-sensoren dat exact 85,0 weggooit. Een echte 85 °C komt in deze tank niet voor; het legionella-doel is 68 °C.

**Later, na de port, eerst als voorstel:** de top-sensor is een enkel storingspunt. Een mogelijke terugval bij een defecte top: starten en stoppen op bottom, met legionella geblokkeerd tot de top weer plausibel is.

**Advies:** R2 voor de klep, R1 voor het element (Full Electric, dus R1 is vrij), klep-terugmelding op de Q-stekker pin 3/4, en tank top + tank bottom als twee DS18B20's op T.

**Beslispunten:** alle I/O-keuzes liggen vast (07-10). Reserve: GPIO43 en de SPI-connector (5.0V – SDI – SDO – CLK) samen zijn genoeg voor een tweede MAX31865, mocht je later toch PT1000-tanksensoren willen.

---

## 4. Benodigd werk, in volgorde

### Stap 0: Wat neem je mee buiten DHW?
Je fork bevat meer dan de boilerlaag: Power House v0.50-omschakelaar, halve standen, ODU-frequentietabel, EEPROM-dump, EnergyOS-bridge, discharge/pressure protection, adaptive heating en de cycling monitor. Upstream v0.53 heeft daar zelf al een deel van (frequentietabel, EEPROM-dump, compressor-startlimiet, Power House-learning, defrost). Maak een lijst wat je mist; dat is los werk naast DHW.

**Besloten (01-10): de CiC vervalt.** Je eigen `oq_cic_modbus_slave` en `components/openquatt_cic`-feed gaan niet mee. Laat upstream `oq_cic_compatibility*` in het target staan (verwijderen is onnodige afwijking van upstream), maar met **CiC Compatibility Mode** uit en **CiC JSON-feed inlezen** uit. M2 is daarmee een vrije RS485-poort voor later, bijvoorbeeld een Modbus-IO- of sensormodule.

### Stap 1: Eigen build-target bovenop het Q-doel
- Maak een eigen config `configs/heatpump_controller_q/duo_dhw.yaml` die `duo.yaml` volgt en daarna je DHW-bundel insluit. Laat het upstream-profiel `heatpump_controller_q.yaml` ongemoeid.
- Voeg een regel toe in `build_targets.yaml`. `scripts/build_targets.py` en de manifest-tests controleren de matrix.
- Bouw dit eerst **zonder DHW** op de Q, in CI. Dan weet je zeker dat de basis staat voordat je iets toevoegt.

### Stap 2: Naamruimte vrijmaken (de boilerbotsing)
- `oq_boiler_control.yaml` → `oq_dhw_control.yaml`; id's `boiler_relay_out` → `dhw_element_relay_out`, enzovoort.
- Hernoem alle "Boiler …"-entity-namen van de DHW-laag. Upstream maakt zelf "Boiler …", "OTB …" en "Boiler test"-entiteiten aan. Plan een HA-migratie (vergelijk je `fix_entity_registry.ps1`).
- R1-eigendom: geen tweede `output` op GPIO16. Gebruik de bestaande `boiler_relay_out` met een arbitrage in `oq_boiler_output_logic.h` / de R1-adapter: zolang `oq_aux_heat_source_present` aan staat, is R1 van de ketel; staat die uit (Full Electric), dan is R1 van de DHW-runtime. Test erbij: R1 wordt nooit door beide aangestuurd, en het aanzetten van de ketelschakelaar tijdens een DHW-cyclus laat het element eerst vallen (break-before-make).
- R2: voeg in `oq_aux_relay_logic.h` / `oq_aux_relay_control.yaml` een functie "DHW valve" toe die de DHW-runtime volgt.

### Stap 3: DHW als package in de upstream-bundels
- Eigen bundel, bijvoorbeeld `openquatt/packages/35_dhw.yaml`, die alleen jouw config insluit.
- Logica onder `openquatt/includes/dhw/` (`oq_dhw_controller_logic.h` + een nieuwe `oq_dhw_runtime.h`).
- Volg het upstream-patroon met `*_runtime.h`-singletons voor state in plaats van eigen types als global. Je eigen geheugenregel zegt al dat eigen types als global de build breken.
- Sensorbron: tank top en tank bottom als twee `dallas_temp`-sensoren op de upstream `one_wire`-bus (GPIO18), elk met een vast adres. Klep-terugmelding als `binary_sensor` op GPIO15, met de upstream-pulsteller uit dit target verwijderd.

### Stap 4: De naad met het regelhart (het echte werk)
Maak één smalle interface (`oq_dhw_bridge.h`: `dhw_request()`, `dhw_mode_active()`, `dhw_flow_setpoint()`, `dhw_level()`). Elke upstream-runtime leest alleen die interface, zodat je patch per upstream-bestand één of twee regels blijft.

| Haakpunt | Wat DHW nodig heeft | Upstream-plek |
|---|---|---|
| Supervisory | DHW-vraag als thermische vraag; eigen CM-nummer; prioriteit boven koelen; element-only; ontluchten met klepstand | `oq_hp_supervisory_logic.h`, `oq_supervisory_state_logic.h` (+ `_runtime.h`) |
| CM-nummering | CM4 is upstream bezet. Upstream gebruikt CM6–CM11 niet; de enige `case 6`/`case 7` in de code horen bij de OTA-fase en de logniveaus (gecontroleerd 07-10) | **Besloten (07-10):** DHW = **CM10**, opgegeven door upstream (Jeroen). Element-only = **CM11**, voorlopig en nog af te stemmen met upstream. Eén constante |
| Thermal request | Strategie "hot water": niveau 0–10, single-lead-HP, max-boost | `oq_thermal_request_logic.h` / `_runtime.h` |
| Flow | Apart DHW-flowsetpoint | `oq_flow_control_logic.h`, `oq_flow_runtime.h` |
| Power House | Leren/UA-update overslaan tijdens DHW | `oq_power_house_runtime.h`, `oq_ph_learning_runtime.h`. Belangrijk: de Q heeft learning **aan** (`OQ_POWER_HOUSE_LEARNING_TARGET=1`), en DHW-cycli vervuilen anders het huismodel |
| Cooling | Blokkeren tijdens DHW, behalve legionella element-only | `oq_cooling_runtime.h` / `oq_cooling_dispatch_logic.h` |
| Incidents/status-led | DHW-fouten naar de incident manager; rode led bij DHW-fout | `components/openquatt_incident_manager`, `heatpump_controller_q_status_leds.yaml` |

### Stap 5: Web-app en HA-dashboard
- Maak een DHW-view en -instellingen in de nieuwe app (`views/`, `settings/installation.js`, `i18n/nl.js` + `en.js`, mock-fixtures en `*.test.mjs`).
- R2-functie "DHW valve" en de R1-keuze komen onder **Instellingen → Installatie**.
- Houd `check_web_docs_sync.mjs` en `check_style_consistency.py` groen.

### Stap 6: Tests en CI
- Zet de DHW-host-tests om naar de upstream-testinfra.
- Schrijf een test voor de naad: DHW-vraag geeft de juiste CM, flowsetpoint en relaisstand. Test ook dat R1 nooit door ketel en DHW tegelijk wordt aangestuurd.
- CI blijft je enige buildcheck (geen lokale ESPHome).

### Stap 7: Versie, documentatie, ingebruikname
- Versieschema, bijvoorbeeld `v0.53.0-dhw.1`.
- Zet `dhw-instellingen.md` en een nieuwe `hardware-dhw-q.md` over: aansluiting van R1 (element), R2 (klep), T (DS18B20 tank top + bottom) en de Q-stekker pin 3/4 (klep-terugmelding).
- Bankproef vóór de installatie. Noteer je huidige DHW-instellingen; opgeslagen waarden (legionella-epochs, numbers) gaan door het hernoemen verloren.
- De LilyGO blijft tot dan je productiesysteem. De Q kun je parallel op de bank ontwikkelen.

---

## 5. Aanbevolen volgorde
1. **Q met upstream v0.53 standaard flashen** en op de bank of naast de LilyGO leren kennen.
2. **Bankproef van de I/O** (uit §3): klep-terugmelding op GPIO15, twee DS18B20's op T naast de PT1000, R1/R2 schakelen.
3. Stap 1–2: eigen target, hernoemen, R1/R2-eigendom.
4. Stap 3–4: DHW-package en de naad.
5. Stap 5–7: UI, tests, documentatie, overstap.

## 6. Open punten om te verifiëren
- Gebruikt upstream GPIO15 nog ergens anders dan voor de pulsteller (flow-autodetectie, quickstart, usage-telemetry `q_flow_source_select`)? Dat moet je weten voordat je de pin ombestemt.
- Zijn GPIO8, 17 of 43/44 ergens op de Q-print bereikbaar (testpad of header)? Vraag dit aan Electropaultje.
- Leg de schakelvermogens van R1 en R2 naast de contactorspoel en de klepmotor. Upstream noemt alleen "potentiaalvrij".
- Zijn er upstream plekken (quickstart, Power House, web-app) die bij `oq_aux_heat_source_present = uit` nog R1-gerelateerde instellingen tonen of schrijven? Die moeten verborgen worden in het DHW-target.
