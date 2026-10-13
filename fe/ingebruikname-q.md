# Ingebruikname van de Q (OpenQuatt FE)

Checklist voor het in gebruik nemen van de Heatpump Controller Q met de FE-firmware: buitenunits, boiler, 3-wegklep en een 3 kW-element. Werk de stappen in volgorde af. Bij elke stap staat wat je moet zien, en wat je doet als dat niet zo is.

## 0. Vooraf

- [ ] **Bankproef afgerond:** R1, R2, klepstand, tanksensoren. Zie `fe/README.md` → Bankproef.
- [ ] **NVS-meting gedaan en beoordeeld** volgens `fe/nvs-voorstel.md`. Vereist: **≥ 20 vrij** na het terugzetten van een volledige back-up. Tussen 20 en 100 komt eerst stap 2 (bundelen).
- [ ] **Brug tussen GPIO44 en GND verwijderd.** Die zat er op de bank; met de brug meldt de klep altijd "Boiler".
- [ ] **Firmware:** de nieuwste FE-versie uit `OpenQuatt-FE-firmware\`. Gebruik bij de eerste installatie `firmware.factory.bin` via USB, **met wissen**.

## 1. Bedrading

| Aansluiting | Naar | Controle |
|---|---|---|
| **R1** (GPIO16, COM/NO) | 3-wegklep | Bekrachtigd = DHW-stand. Spanningsloos moet de klep zelf terug naar CV: veerretour, of een 3-draadsklep met de terugrichting vast aangesloten. |
| **R2** (GPIO3, NO) | Spoel van de contactor van het 3 kW-element | Nooit het element direct op R2. Het element zit op een andere fase (L2) dan de buitenunits (L1). |
| **T** (1-Wire, GPIO18) | Twee DS18B20: tank top en tank bottom | Kabels zijn gelabeld na de bankproef: top bovenin, bottom onderin de tank. |
| **Tweakers-connector boven**, GPIO44 + GND | Hulpcontact van de klep | Contact sluit in de DHW-stand. GPIO43 vrij laten. |
| **Q-stekker** | PT1000 aanvoer en flowpuls | Zoals upstream. |
| **M1** (RS485) | Buitenunits | Zoals upstream. |

## 2. Eerste start, zonder warmwatervraag

1. Zet **Warm water blokkeren** aan (Instellingen → Warm water) vóór de buitenunits verbinding hebben. Zo start er niets terwijl je controleert.
2. Rond de **Quick Start** af (upstream). Er is geen stap voor de ketel; die bestaat op FE niet.
3. Controleer:
   - de HP1/HP2-panelen tonen live data en geen "Active Failures List";
   - **Tank boven** en **Tank onder** zijn geloofwaardig en verschillen niet meer dan een paar graden van elkaar;
   - **Driewegklep = CV**;
   - **Vrije opslag (NVS)** (Service → Bankproef warm water) ligt boven 20.
4. Staat de warmwaterkaart op **Storing / Warmtepompstoring**, dan is dat nog van vóór de verbinding met de buitenunits. Klik **Storing wissen** zodra de panelen geen storing meer tonen.

## 3. Instellingen

1. Stel de groep **Warm water** in: start- en stopgrens, flow, snelboost, legionella, tank.
2. Maak een **back-up** (Instellingen → Systeem). Bewaar die buiten de Q.
3. Lees **Vrije opslag (NVS)** opnieuw af en noteer de waarde.

## 4. Home Assistant

1. Installeer het package en de view "Warm water" uit `fe/dashboard/` (zie de README daar).
2. Controleer dat alle tegels een waarde hebben. Een lege tegel betekent een andere apparaatnaam dan `openquatt`.

## 5. Eerste warmwatercyclus (CM10)

1. Zet **Warm water blokkeren** uit.
2. Start met **Snelboost**, of wacht tot tank top onder de startgrens zakt.
3. Wat je moet zien, in volgorde:

| Moment | Verwacht |
|---|---|
| Start | Status "Klep omzetten", daarna **Driewegklep = Boiler**. Beslislog: "Warmwatervraag". |
| Warmtepompfase | Regelmodus **CM10 · warm water**, status "Warmtepomp verwarmt". De flow volgt het DHW-flowsetpoint en tank top stijgt. |
| Einde | Terug naar CM0/CM1. Beslislog: "Warmwatervraag afgerond". **Driewegklep = CV**. |

4. **Als het anders loopt:**
   - **"Klepstand klopt niet" / "Klep blijft op CV":** controleer de terugmelding op GPIO44 en of R1 de klep echt omzet.
   - **"Flow buiten bereik":** de flow ligt buiten de DHW-flowbewaking of onder upstreams minimum van 400 L/h. Controleer het DHW-flowsetpoint en de pomp.
   - **"Time-out":** de warmtepompfase duurde langer dan 180 min. Vergelijk het vermogen met het tankvolume.
   - **De compressor start niet in CM10:** kijk in het beslislog naar upstreams startblokkades, zoals de minimale rusttijd of de waterbeveiliging.

## 6. Element

1. **Boost na de warmtepomp** (als die aan staat): na de warmtepompfase verwarmt het element verder tot het boostdoel. Op de kaart staat dan **Element = Aan**, en de contactor schakelt hoorbaar.
2. **Alleen element (CM11):** zet de schakelaar aan. Regelmodus **CM11 · alleen element**, circulatiepompen uit; het element houdt tank bottom op het doel. Zet hem daarna weer uit.

## 7. Eerste legionellarun

1. Start met **Legionella nu**, op een moment dat er even geen warm water nodig is.
2. Verwacht:
   - de warmtepomp verwarmt tot de overdrachtstemperatuur, daarna neemt het element het over tot het legionelladoel;
   - met "coil circulation" aan blijft de klep op Boiler tot het eind;
   - **Laatste run** en **Volgende run** worden bijgewerkt.
3. Bij een storing gaat Legionella nu vanzelf uit. Start hem na Storing wissen opnieuw.

## 8. Eerste week

- [ ] **Vrije opslag (NVS)** dagelijks bekijken: blijft hij boven 20? Daalt hij, noteer dan wanneer.
- [ ] **Beslislog:** CM10-cycli en hun aanleiding kloppen met het gebruik.
- [ ] **Tapdetectie:** het aantal tappingen vandaag klopt grofweg.
- [ ] **Stilstandsverlies:** het aantal samples loopt op en de UA-waarde stabiliseert.

## Noodstop en terugval

- **Warm water blokkeren** stopt de warmwaterregeling direct. Tijdens een lopende cyclus gaat de regeling naar storing "Geblokkeerd": warmtepomp en element stoppen en de klep valt terug op CV. Na het uitzetten van de blokkade klik je **Storing wissen**. In rust geeft blokkeren geen storing.
- **Spanningsloos** valt de klep op CV en is het element uit (R2 op NO).
- Verwarmen werkt zonder warm water gewoon door via upstream.
