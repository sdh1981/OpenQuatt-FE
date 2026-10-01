# OpenQuatt FE

OpenQuatt FE is upstream [OpenQuatt](https://github.com/OpenQuatt/OpenQuatt) voor een **Full Electric** Quatt Duo op de **Electropaultje Heatpump Controller Q-edition**, aangevuld met een eigen warmwaterregeling (DHW): een boiler met 3-wegklep, een 3 kW-element en twee tanksensoren.

Deze map (`fe/`) bevat alles wat specifiek is voor FE en niet uit upstream komt. De bestanden daarbuiten volgen upstream. Een FE-wijziging aan een upstream-bestand houden we zo klein mogelijk, zodat elke upstream-release makkelijk opnieuw in te voegen is.

## Opbouw

| Pad | Inhoud |
|---|---|
| `configs/heatpump_controller_q/duo_fe.yaml` | Het FE-build-target. Neemt upstream `duo.yaml` ongewijzigd over en zet de FE-onderdelen erbovenop |
| `openquatt/fe/` | FE-packages (YAML), volgt in fase 2 |
| `openquatt/includes/fe/` | FE-logica (C++-headers): `oq_dhw_controller_logic.h` |
| `tests/host/fe_*_test.cpp` | Host-tests voor de FE-logica; draaien in CI met de upstream-tests mee |
| `fe/port-plan.md` | Het portplan, met I/O-keuzes en onderbouwing |

## Aansluitingen (besloten 01-10-2026)

| Klem op de Q | Functie |
|---|---|
| R1 | Contactor van het 3 kW-element. Er is geen CV-ketel; in de firmware staat "Auxiliary heat source connected" uit |
| R2 | 3-wegklep (voorstel) |
| Q-stekker pin 3/4 | Terugmelding van de klep (voorstel). De flow komt uit de buitenunit; de lokale pulsteller vervalt |
| Q-stekker pin 2/5 | PT1000 aanvoertemperatuur (upstream) |
| T | Twee DS18B20-sensoren: tank top en tank bottom |
| M1 | Buitenunits (upstream) |
| M2 | Vrij; de CiC wordt niet gebruikt |
| OTT / OTB | Kamerthermostaat (upstream) / ongebruikt |

## Firmware-updates

De FE-firmware haalt nooit een upstream-image binnen. De OTA-manifesten wijzen naar de releases van deze repo, en de wissel tussen single en duo staat uit. Er bestaat geen FE-single-target.

## Upstream bijwerken

```bash
git fetch upstream --tags
git merge v0.54.0
```

Na een merge controleer je of de FE-haken in upstream-bestanden nog op hun plek zitten. De lijst daarvan staat in `fe/port-plan.md` §4, stap 4.

## Fasen

| Fase | Inhoud | Status |
|---|---|---|
| 1 | Repo, FE-build-target, CI bouwt alleen FE | PR #1 |
| 2 | DHW-logica en host-tests overzetten (`oq_dhw_controller_logic.h`), nog zonder aansturing | PR #2 |
| 3 | I/O: twee DS18B20 op T, klep op R2, terugmelding op GPIO15, element op R1 met arbitrage. Let op: filter de 85,0 °C-opstartwaarde van de DS18B20 weg (zie `port-plan.md` §3) | gepland |
| 4 | Naad met het regelhart: supervisory (DHW = CM6, element-only = CM7), thermal request, flow, Power House, cooling | gepland |
| 5 | Web-app en HA-dashboard | gepland |
| 6 | Bankproef op de Q, daarna overstap van de LilyGO | gepland |

## Versies

`project_version` volgt de upstream-versie met een FE-teller, bijvoorbeeld `v0.53.0-fe.0`. Bump de FE-teller bij elke functionele wijziging.
