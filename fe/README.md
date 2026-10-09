# OpenQuatt FE

OpenQuatt FE is upstream [OpenQuatt](https://github.com/OpenQuatt/OpenQuatt) voor een **Full Electric** Quatt Duo op de **Electropaultje Heatpump Controller Q-edition**, aangevuld met een eigen warmwaterregeling (DHW): een boiler met 3-wegklep, een 3 kW-element en twee tanksensoren.

Deze map (`fe/`) bevat alles wat specifiek is voor FE en niet uit upstream komt. De bestanden daarbuiten volgen upstream. Een FE-wijziging aan een upstream-bestand houden we zo klein mogelijk, zodat elke upstream-release makkelijk opnieuw in te voegen is.

## Opbouw

| Pad | Inhoud |
|---|---|
| `configs/heatpump_controller_q/duo_fe.yaml` | Het FE-build-target. Neemt upstream `duo.yaml` ongewijzigd over en zet de FE-onderdelen erbovenop |
| `openquatt/fe/fe_io.yaml` | FE-I/O: R1/R2-eigenaarschap, tanksensoren, klep-terugmelding, bankproef |
| `openquatt/fe/fe_dhw.yaml` | DHW-regeling: instellingen, status en de 2 s-tick van `oq_fe_dhw_runtime` |
| `openquatt/includes/fe/` | FE-logica (C++-headers): de DHW-toestandsmachine, de niveaulogica, de bridge naar het regelhart en de runtimes |
| `tests/host/fe_*_test.cpp` | Host-tests voor de FE-logica; draaien in CI met de upstream-tests mee |
| `fe/port-plan.md` | Het portplan, met I/O-keuzes en onderbouwing |

## Aansluitingen (besloten 01-10-2026)

| Klem op de Q | Functie |
|---|---|
| R1 | Contactor van het 3 kW-element. Er is geen CV-ketel; in de firmware staat "Auxiliary heat source connected" uit |
| R2 | 3-wegklep (onbekrachtigd = CV-stand) |
| Tweakers-connector boven, GPIO44 + GND | Terugmelding van de klep: het hulpcontact sluit in de DHW-stand. GPIO43 blijft vrij: de ROM-bootloader stuurt die pin bij elke start aan |
| Q-stekker | PT1000 aanvoertemperatuur en flowpuls (upstream) |
| T | Twee DS18B20-sensoren: tank top en tank bottom |
| M1 | Buitenunits (upstream) |
| M2 | Vrij; de CiC wordt niet gebruikt |
| OTT / OTB | Kamerthermostaat (upstream) / ongebruikt |

## Firmware-updates

De FE-firmware haalt nooit een upstream-image binnen. De OTA-manifesten wijzen naar de releases van deze repo, en de wissel tussen single en duo staat uit. Er bestaat geen FE-single-target.

## Hoe R1 en R2 werken

Upstream stuurt R1 (ketel) en R2 (hulprelais) aan via hun eigen runtimes. In het FE-target vervangt `fe_io.yaml` de twee fysieke uitgangen door een template-output met dezelfde id. Upstream schrijft dus nog steeds, maar `oq_fe_io_runtime` beslist wat er op de pin komt:

- **R1:** staat **Auxiliary heat source connected** aan, dan volgt R1 de upstream-ketelvraag. Staat die uit (Full Electric), dan is R1 het DHW-element. Zet die schakelaar dus **uit**; upstream zet hem bij een nieuwe installatie standaard aan.
- **R2:** staat **R2 is DHW valve** aan (standaard), dan is R2 de 3-wegklep, en de instelling van het hulprelais in de web-app heeft dan geen effect. Staat die uit, dan volgt R2 upstream.

Upstream-bestanden blijven daarbij ongewijzigd.

## Bankproef

1. Sluit beide DS18B20's aan op T, en controleer of de Q een pull-up van 4,7 kΩ op DATA heeft.
2. Start de Q. De log van `one_wire` toont de gevonden adressen. Vul ze in bij `fe_dallas_tank_top_address` en `fe_dallas_tank_bottom_address` in `openquatt/fe/fe_io.yaml` en bouw opnieuw. Zolang de adressen niet kloppen, blijft **DHW Tank Top** leeg en mag het element niet proefdraaien.
3. Zet **Auxiliary heat source connected** uit.
4. **DHW Test Valve** zet R2 maximaal 5 minuten aan. **DHW Valve Position** moet naar `DHW` gaan en na het uitzetten terug naar `CV`.
5. **DHW Test Element** zet R1 maximaal 2 minuten aan, en alleen als tank top geldig is en onder 65 °C ligt. **DHW Element Active** toont de stand.

Een verlopen of geweigerde proef zet de schakelaar zelf terug. De reden staat in **DHW Test Valve Status** en **DHW Test Element Status**.

Upstream leest met zijn eigen DS18B20 (`index: 0`) op deze bus een van de tanksensoren. Die sensor is daarom intern gemaakt, en de lokale aanvoerbron wordt steeds teruggezet naar `PT1000`.

## Upstream bijwerken

```bash
git fetch upstream --tags
git merge v0.54.0
```

Na een merge controleer je met `esphome config configs/heatpump_controller_q/duo_fe.yaml` of de id's die FE vervangt of uitbreidt nog bestaan: `boiler_relay_out`, `controller_aux_relay_out`, `water_supply_temp_ds18b20`, `oq_local_temp_bus`, `oq_aux_heat_source_present` en `oq_local_supply_temp_source`. De haken in het regelhart volgen in fase 4 (`fe/port-plan.md` §4, stap 4).

## Fasen

| Fase | Inhoud | Status |
|---|---|---|
| 1 | Repo, FE-build-target, CI bouwt alleen FE | PR #1 |
| 2 | DHW-logica en host-tests overzetten (`oq_dhw_controller_logic.h`), nog zonder aansturing | PR #2 |
| 3 | I/O: twee DS18B20 op T (85,0 °C-opstartwaarde gefilterd), klep op R2, terugmelding op GPIO44, element op R1, bankproef-schakelaars | PR #3 |
| 4a | DHW stuurt de warmtepompen: CM10, eigen strategie en flow-setpoint, coil-in-mapping op de HP-uitlaat, zachte aanloop, single-HP met assist, snelboost met bewaking, legionella | PR #5 |
| 4c | Tapdetectie, standby-loss-lerer, element-only (CM11); tarief/PV en adaptief leren vervallen (besloten 09-10). Voorstel: `fe/fase4c-voorstel.md` | voorstel |
| 5 | Web-app en HA-dashboard | gepland |
| 6 | Bankproef op de Q, daarna overstap van de LilyGO | gepland |

## Haken in upstream-bestanden

Elke haak staat achter `#if OQ_FE_TARGET` of is in een upstream-build onbereikbaar, en is gemarkeerd met `FE`. Loop ze na bij elke upstream-merge:

| Bestand | Haak |
|---|---|
| `control/oq_supervisory_state_runtime.h` | DHW-vraag, basisdoel CM10, CM1-voorloop en -afloop, naloop na CM10, CM-namen |
| `control/oq_thermal_request_logic.h` | CM10 → verwarmstand met `STRATEGY_DHW`; niveaus uit `oq_fe_dhw_bridge` |
| `control/oq_flow_runtime.h` | Flow-setpoint in CM10 |
| `control/oq_thermal_actuator_runtime.h` | CM10 in de melding "frequentiegrens blokkeert start" |
| `scripts/tests/test_supervisory_state_runtime_contract.py`, `test_thermal_request_runtime_contract.py` | Regelbudget + FE-delta (43 / 19) en de `thermal_req`-regel met `dhw_req`. Bij een upstream-merge de delta opnieuw tellen |

## Control modes

Warm water krijgt **CM10**. Dat nummer komt van upstream (Jeroen, 07-10-2026), zodat FE en upstream dezelfde nummering delen. Element-only (legionella of boost zonder warmtepomp) krijgt voorlopig **CM11**; dat nummer is nog niet met upstream afgestemd.

Upstream gebruikt CM0–CM5, CM98 en CM100. CM4 is daar "alleen ketel na een warmtepompstoring" en dus niet bruikbaar voor DHW, zoals in de LilyGO-build. In fase 4 staan de nummers op één plek als constante. HA-dashboards en automatiseringen uit de LilyGO-build (DHW = CM4, element-only = CM6) moeten bij de overstap mee.

## Versies

`project_version` volgt de upstream-versie met een FE-teller, bijvoorbeeld `v0.53.0-fe.0`. Bump de FE-teller bij elke functionele wijziging.
