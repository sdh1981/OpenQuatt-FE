# Voorstel fase 4: DHW koppelen aan het regelhart

Status: **voorstel**, nog niet gebouwd (2026-10-07).
Basis: `main` op `e404f06a` (upstream v0.53.0 + FE fase 1–3).

Fase 3 heeft de I/O neergezet: klep op R2, element op R1, tanksensoren en terugmelding. Fase 4 laat de DHW-regeling de warmtepompen echt aansturen. Dat raakt de regeling van de warmtepomp zelf, dus eerst dit voorstel met concrete drempels.

---

## 1. Afbakening

Fase 4 is te groot voor één stap. Je LilyGO-build heeft zo'n 150 DHW-entiteiten, en de meeste daarvan zijn optimalisaties bovenop de kern. Voorstel: drie stappen.

| Stap | Inhoud |
|---|---|
| **4a (dit voorstel)** | Kern: DHW-vraag → **CM10**, warmtepompen op een vast niveau via een eigen strategie, eigen flow-setpoint, klep en element via de FE-runtime, legionella wekelijks, boost na de HP-fase. Bewaking via de bestaande upstream-lagen. |
| 4b | Snelboost (beide HP's + element) met persgas- en water-uit-bewaking. Single-HP-modus met tweede-HP-assist en zachte aanloop. Tapdetectie. |
| 4c | Tarief- en PV-sturing, adaptief leren, slim legionella-uitstel, element-only-modus **CM11**, HA-dashboard. |

Valt definitief af (geen coil-sensoren meer): coil-in-niveaumapping, en COP en energie per cyclus op basis van coil in/uit. COP kan later via het HP-vermogen terugkomen.

---

## 2. Hoe het gaat werken

```
DHW-toestandsmachine (oq_dhw_controller_logic.h, ongewijzigd)
   │  inputs: tank top/bottom, klepterugmelding, flow, HP-beschikbaarheid, schakelaars
   ▼
FE DHW-runtime (nieuw: openquatt/includes/fe/oq_fe_dhw_runtime.h)
   ├─► oq_fe_io_runtime::set_dhw_requests(klep, element)   → R2 / R1 (fase 3)
   └─► oq_fe_dhw_bridge (nieuw, smalle naad)
          dhw_mode_requested()  dhw_hp_levels()  dhw_flow_setpoint_lph()
             │
             ▼   upstream-haken, alle achter #if OQ_FE_TARGET
   supervisory ──► CM10 (met CM1 voor- en naloop)
   thermal request ──► strategie DHW → niveau per HP
   flow ──► DHW-setpoint
   actuator / bewaking ──► ongewijzigd: harde watertrip, flowfout, startvertraging,
                            minimale looptijd, frequentiebeleid
```

**Waarom een eigen strategie en geen directe niveauschrijfactie:** upstream's automatische pad in `oq_thermal_request_runtime.h` past op elk strategie-niveau al toe:
- het frequentiebeleid (`allowed_`);
- de slew-begrenzing;
- de harde stop bij watertrip, flowfout of koude start;
- de startvertraging na een compressorstop;
- de minimale looptijd.

Loopt DHW via dat pad, dan krijgt het die bescherming vanzelf. Niets ervan hoeft opnieuw gebouwd te worden.

---

## 3. Upstream-haken

Alle haken staan achter `#if OQ_FE_TARGET`. Upstream-targets compileren daardoor exact zoals nu. De nummers staan op één plek (`oq_fe_dhw_bridge.h`: `CM_DHW = 10`, `STRATEGY_DHW = 5`), zodat een andere keuze van Jeroen een wijziging van één regel blijft.

| # | Bestand | Wijziging | Omvang |
|---|---|---|---|
| 1 | `control/oq_supervisory_state_runtime.h` | DHW-vraag telt als thermische vraag; `base_target = 10` met voorrang boven koelen en verwarmen; CM1-voorloop met `next_after = 10`; afloop van CM1 naar CM10; CM10 in de naloop-lijst; CM-naam "CM10 - Hot water" | ~25 regels, 6 plekken |
| 2 | `control/oq_thermal_request_logic.h` | `resolve_mode_context`: CM10 → HP toegestaan, modus verwarmen, `STRATEGY_DHW`. `select_strategy_request`: DHW-tak met de niveaus uit de bridge | ~12 regels |
| 3 | `control/oq_thermal_request_runtime.h` | DHW-niveaus in de `StrategyRequestInput` | ~4 regels |
| 4 | `control/oq_flow_runtime.h` | Setpoint in CM10 = DHW-setpoint | ~5 regels |
| 5 | `control/oq_thermal_actuator_runtime.h` | CM10 in de lijst voor de melding "frequentiegrens blokkeert start" (alleen diagnose) | 1 regel |
| 6 | `control/oq_control_mode_log_logic.h` | CM10 als eigen modus in de decision log in plaats van "onbekend" | ~4 regels |

**Wat bewust ongewijzigd blijft:**
- De andere lezers van de CM-code vallen voor CM10 al goed uit. Power House-leren telt alleen CM0/1/2 (CM10 wordt dus niet meegeleerd). De ketel-dispatch doet niets buiten CM3/CM4. Het hulprelais ziet CM10 als "geen vraag", en R2 is in FE toch van de klep.
- Upstream-CM4 (alleen ketel na storing) komt in FE niet voor, want er is geen bijverwarmer.

---

## 4. Gedrag en drempels (4a)

### Voorrang

```
override / CM100-service  >  DHW (CM10)  >  koelen (CM5)  >  verwarmen (CM2)  >  vorst (CM98)  >  standby (CM0)
```

Dit is dezelfde volgorde als in de LilyGO-build. DHW gaat voor koelen. Upstream houdt een warmtepomp in koelstand tot zijn niveau 0 is (`hold_request_mode_code`), en de CM1-voorloop zit ertussen. De omschakeling van koelen naar verwarmen loopt dus altijd via compressor uit → CM1 → CM10.

### Drempels

| Wat | Waarde | Bron |
|---|---|---|
| Start DHW-cyclus | tank top **< 46 °C** | fork-standaard |
| Stop HP-fase | tank bottom **≥ 52 °C** (vast); zonder bottom-sensor tank top ≥ 49 °C | fork |
| Maximale duur HP-fase | **180 min**, daarna stopt de fase zonder fout | fork |
| Niveau per HP | **4**, op beide HP's (duo) | fork-standaard "DHW HP level" |
| Flow-setpoint in CM10 | **1000 L/h** | fork-standaard |
| Flow-bewaking DHW | buiten **750–1800 L/h** gedurende **30 s** → DHW-fout | fork |
| CM1 voor- en naloop | **30 s** (upstream `oq_cm_prepost_s`) | upstream |
| Klep: omlooptijd / settle / mismatch | **20 s** / **4 s** / **10 s**, 1 herkansing | fork-logica |
| Rust tussen cycli | **20 min** | fork-standaard "DHW minimum rest" |
| Boost na HP-fase | **aan**: element tot tank top **56 °C**, max **90 min** | fork |
| Legionella | elke **7 dagen**, doel **68 °C**, hold **15 min**, max **150 min**. HP-fase tot bottom **53 °C** of top **55 °C** (plafond), daarna alleen element | fork, Inventum-eis |
| Watertemperatuur-grens | upstream ongewijzigd: max **60 °C**, harde trip op **65 °C** (stopt de HP's in elke modus) | upstream |

**Waarom de watergrens past:** DHW stuurt op niveau, niet op aanvoertemperatuur. In de praktijk loopt de aanvoer rond 55 °C, en de HP-fase van legionella stopt op een top van 55 °C. De ruimte tot de trip van 65 °C is dus 10 K. De zachte begrenzing van upstream (vanaf 57 °C) werkt alleen op de stooklijn en Power House, en raakt het vaste DHW-niveau niet.

### Storingen en fail-safe

| Situatie | Gedrag |
|---|---|
| Sensor tank top weg of onplausibel | DHW-fout; klep → CV, element uit, geen HP-vraag. CM valt terug naar verwarmen/koelen |
| Geen HP beschikbaar (upstream `no_hp_available_confirmed`) | DHW-fout `HP_FAULT`. Element-boost en legionella kunnen alleen nog na het wissen van de fout |
| Eén HP in storing (duo) | Upstream's incident-manager haalt die HP eruit; DHW draait door op de andere |
| Klep meldt geen DHW-stand | Na 1 herkansing DHW-fout `VALVE_STUCK_CV`; element blijft uit |
| Harde watertrip of flowfout | Upstream zet de HP-niveaus op 0. De DHW-FSM merkt dat aan flow/tijd en valt eventueel in fout |
| CM100 (service) of een override | Gaat voor; de DHW-runtime trekt zijn klep- en elementvraag in |
| Herstart van de Q | Alles uit; de klep valt onbekrachtigd op CV. Het legionella-tijdstip blijft bewaard (wandklok) |

---

## 5. Entiteiten in 4a

Ik houd de **namen uit de LilyGO-build** aan, zodat de entity-id's in Home Assistant gelijk blijven en je dashboard grotendeels blijft werken.

| Soort | Entiteiten |
|---|---|
| Instellingen | DHW start top · DHW HP stop top · DHW HP level · Flow Setpoint DHW · DHW flow min · DHW flow max · DHW minimum rest · DHW boost target · DHW legionella target · DHW legionella HP handover temp · DHW legionella HP top ceiling · DHW window start/end hour |
| Schakelaars | DHW lockout · DHW boost after HP · DHW window enable · DHW source legionella force · DHW clear fault |
| Status | DHW state · DHW fault · DHW HP request active · DHW block CV priority · DHW legionella laatste/volgende run · DHW target flow temp |

De bankproef-schakelaars uit fase 3 blijven. Ze werken alleen zolang de DHW-FSM in `IDLE_CV` staat.

---

## 6. Testen

1. **Host-tests:**
   - de bridge-logica (CM-keuze, niveaus, setpoint);
   - de supervisory-beslissing als pure functie: DHW boven koelen, CM1 met `next_after = 10`, afloop terug naar CM5/CM2;
   - een scenario van start tot stop met de FSM erbij.
2. **`esphome config`** en **`--only-generate`** lokaal; de compile in CI.
3. **Upstream-targets** (`heatpump_controller_q_duo`) in deze PR eenmalig weer op `enabled`. Dat bewijst dat de haken achter `OQ_FE_TARGET` upstream niet veranderen; daarna weer `planned`.
4. **Bankproef op de Q:** CM10 starten met een gesimuleerde lage tank top, zien dat de klep omgaat en de HP-vraag pas na `valve_ready` komt, de stop op bottom testen en de naloop naar CM0.

---

## 7. Risico's en open punten

1. **Plaats van de aanvoersensor.** De harde watertrip kijkt naar `water_supply_temp_selected` (PT1000 op de Q). Zit die vóór de 3-wegklep, dan bewaakt hij ook de DHW-aanvoer. Zit hij in de CV-tak, dan ziet hij in CM10 stilstaand water. **Controleren bij de installatie.**
2. **Minimale looptijd na het einde van een cyclus.** Upstream houdt een compressor na een start minimaal `oq_min_runtime_min` aan. Stopt de DHW-cyclus eerder, dan gaat de klep naar CV terwijl de HP nog even doorloopt. De warmte gaat dan de CV in. Dat gedrag had de LilyGO-build ook; ik noem het omdat het in CM1-naloop zichtbaar wordt.
3. **Slew-begrenzing.** Het DHW-niveau loopt via de slew van de stooklijn (`steady_up_hold_s`). De start naar niveau 4 duurt daardoor enkele minuten. Dat lijkt op de zachte aanloop uit de LilyGO-build, en is daarom geen bezwaar.
4. **Rebasen.** Zes upstream-bestanden krijgen een paar regels. Bij elke upstream-release moet je die haken nalopen; de lijst staat in `fe/README.md`.
5. **Nummers met upstream afstemmen.** CM10 komt van Jeroen. `STRATEGY_DHW = 5` en CM11 zijn mijn keuze. Upstream zou ze later ook kunnen claimen.

---

## 8. Beslissingen (2026-10-09)

1. **Afbakening:** snelboost en single-HP (met tweede-HP-assist en zachte aanloop) gaan **mee in 4a**. Tapdetectie, tarief/PV, adaptief leren en CM11 blijven voor later.
2. **Niveau:** zoals de LilyGO-build: basisniveau "DHW HP level" (4), teruggeschaald door de **coil-in-mapping** (4→3 bij 40 °C, 3→2 bij 44 °C, 2→1 bij 48 °C, hysterese 0,5 K) en begrensd door de zachte aanloop (3 min per stap). De mapping staat in FE standaard **aan** en leest de uitlaat van de laatste warmtepomp (HP2 in duo), omdat de coil-in-sensor op de Q niet bestaat. Tijdens DHW is dat hetzelfde water dat de spiraal in gaat.
3. **Voorrang:** DHW gaat voor koelen.
4. **Legionella:** standaard aan, wekelijks.
5. **Entiteitnamen:** gelijk aan de LilyGO-build.

Gebouwd in `feat/fe-dhw-regelhart` als `v0.53.0-fe.2`.
