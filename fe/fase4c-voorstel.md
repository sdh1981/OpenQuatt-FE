# Voorstel fase 4c: tapdetectie, standby-loss-lerer en element-only (CM11)

Status: **voorstel**, nog niet gebouwd (2026-10-09).
Basis: `main` op `2b9085f4` (fase 4a, `v0.53.0-fe.2`).

**Afbakening (besloten 2026-10-09):** tarief/PV-sturing en adaptief leren vervallen. Gebouwd worden tapdetectie, de standby-loss-lerer en element-only als CM11.

Alle drie komen uit de LilyGO-build (v0.65.0). Waar ik afwijk, staat dat er expliciet bij.

---

## 1. Tapdetectie

Detecteert een tapping aan de daalsnelheid van tank top terwijl er niet verwarmd wordt. Er is alleen tank top nodig, dus het werkt zonder coil-sensoren.

| Wat | Waarde |
|---|---|
| Meetvenster | elke **60 s** de daalsnelheid van tank top |
| Drempel | daling **≥ 0,4 K/min** = tapping (instelbaar 0,1–3,0). Standby ligt rond 0,02 K/min, een douche rond 1 K/min |
| Einde tapping | na **2 min** zonder daling boven de drempel; twee kranen kort na elkaar tellen als één |
| Niet meten | tijdens een HP-vraag of zolang het element aan staat, of zonder tank top |
| Energie per tapping | (tank top bij start − tank top bij einde) × volume × 1,16 Wh/(L·K). Dit is een ondergrens, door stratificatie |
| Tankvolume | instelbaar, **220 L** |

**Entiteiten** (namen uit de LilyGO-build): DHW tapdetectie (schakelaar, standaard aan), DHW tapdetectie drempel, DHW tank volume, DHW tapping actief, DHW tappingen vandaag (reset om middernacht), DHW laatste tapping energie, DHW tanktop daalsnelheid.

## 2. Standby-loss-lerer

Schat het warmteverlies van de tank (UA, in W/K) uit de afkoeling van tank top in rust.

| Wat | Waarde |
|---|---|
| Wanneer meten | DHW in `IDLE_CV`, element uit, en **> 1 uur** na de laatste tapping |
| Meetvenster | **30 min** rust; daarna begint direct een nieuw venster |
| Geldig monster | daling tussen **0,05 en 5 K** in het venster; tank minstens **5 K** warmer dan de ruimte (vast **20 °C**) |
| UA-monster | (daling × volume × 1,16) / uren / (T_tank − 20 °C) |
| Plausibiliteit | monster tussen **0,2 en 15 W/K**, anders weggooien |
| Middeling | eerste 4 monsters gewoon gemiddeld, daarna EMA met **τ = 7 dagen** |
| Opslag | UA en aantal monsters blijven bewaard over een herstart |

**Entiteiten:** DHW tank standby loss (W/K), DHW UA samples, DHW reset standby-loss learning (knop).

**Gebruik van de UA-waarde:** in de LilyGO-build voedt hij twee ETA-sensoren:
- **DHW estimated time to ready:** minuten tot "DHW HP stop top". Netto vermogen = HP-warmtevermogen + 3 kW als het element aan staat − UA × (T_top − 20 °C).
- **DHW legionella ETA:** het element tot het HP-plafond, de rest tot 68 °C, en de hold van 15 min.

Zonder die sensoren is de lerer alleen informatie. Zie beslispunt 1.

**Afwijking:** in de LilyGO-build telde "HP-warmtevermogen" de waarde die de DHW-lus zelf bijhield. In FE gebruik ik upstream's `hp1_heat_power` + `hp2_heat_power`, die al uit flow × ΔT per unit komen.

## 3. Element-only (CM11)

Met een schakelaar verwarmt alleen het 3 kW-element de tank. De warmtepompen staan dan stil. Handig bij een HP-storing, onderhoud, stille uren of vakantie.

| Wat | Waarde |
|---|---|
| Aan | schakelaar **DHW element only** (standaard uit) |
| Thermostaat | op **tank bottom**: element aan onder **doel − off-delta** (45 °C), uit op **doel** (**50 °C**). Doel instelbaar 35–65 °C, off-delta 1–10 K |
| Sensor weg | tank bottom onplausibel → element uit |
| Warmtepompen | standby (CM11 staat niet in upstream's lijst van modi waarin de HP mag draaien) |
| Klep | **CV** (onbekrachtigd). **Afwijking:** in de LilyGO-build bleef de klep in de laatste stand |
| Pomp | **uit** in CM11 |
| Voorrang | CM100 en overrides > **legionella** > CM11 > DHW via de HP (CM10) > koelen > verwarmen |

### Twee aandachtspunten

**1. Legionella tijdens element-only.** In de LilyGO-build werd de DHW-toestandsmachine tijdens CM6 niet getikt. Een geplande legionella-run kon daardoor nooit starten zolang element-only aan stond.

**Voorstel:** de toestandsmachine blijft in CM11 doortikken, met een start-inhibit voor gewone cycli. Legionella mag dus wel starten. Zodra hij start, verlaat de regeling CM11 voor CM10, voert de run uit (klep, HP-fase, element) en keert daarna terug naar CM11.

**2. Veilige overgang.** Upstream laat de pomp in elke modus behalve CM0 en CM100 draaien. Zou CM11 de pomp meteen stoppen terwijl een compressor nog uitloopt, dan draait die zonder flow.

**Voorstel:**
- Vanuit een actieve modus (CM2, CM3, CM5, CM10) gaat de regeling eerst naar **CM1-naloop** (30 s) met `next_after = 11`.
- Ze blijft in CM1 tot beide warmtepompen echt stil staan; upstream's `hold_cm1_until_hp_idle` wordt uitgebreid met CM11.
- Pas daarna volgt CM11, met de pomp uit.

### Upstream-haken (achter `#if OQ_FE_TARGET`)

| Bestand | Haak |
|---|---|
| `control/oq_supervisory_state_runtime.h` | Element-only-vraag → CM11 met de voorrang hierboven; CM1-naloop met `next_after = 11`; vasthouden tot de HP's stil staan; CM-naam "CM11 - Element only" |
| `control/oq_supervisory_state_logic.h` | `hold_cm1_until_hp_idle` ook voor CM11 (één voorwaarde) |
| `control/oq_flow_runtime.h` | CM11 telt als flow-idle |
| contracttests | Regelbudget + FE-delta |

**Entiteiten** (namen uit de LilyGO-build): DHW element only (schakelaar), DHW Element Only Target, DHW Element Only Off Delta.

---

## 4. Testen

1. **Host-tests:**
   - tapdetectie: drempel, einde na 2 min, geen detectie tijdens verwarmen, energie-schatting;
   - lerer: venster, uitsluiting na een tapping, plausibiliteit, middeling;
   - element-only: thermostaat met hysterese, sensor weg, legionella-voorrang;
   - de CM11-voorrang als pure functie in de bridge.
2. **Lokaal** `esphome config` en `--only-generate`; de compile in CI. Het upstream-Duo-target zet ik weer eenmalig aan, als bewijs dat upstream ongewijzigd compileert.
3. **Bankproef:**
   - element-only aan → CM1-naloop → CM11, pomp uit, element schakelt op tank bottom;
   - een geforceerde legionella-run onderbreekt CM11 en keert daarna terug.

---

## 5. Beslispunten

1. **ETA-sensoren:** ook "DHW estimated time to ready" en "DHW legionella ETA" bouwen? Dat geeft de standby-loss-lerer zijn doel.
2. **Klep in CM11:** naar CV (onbekrachtigd, mijn voorstel) of in de laatste stand laten (zoals de LilyGO-build)?
3. **Legionella tijdens CM11:** wel laten starten, zoals in het voorstel (wijkt af van de LilyGO-build)?
4. **Ruimtetemperatuur voor de lerer:** vast 20 °C zoals in de LilyGO-build, of een instelbaar getal (de tank staat bijvoorbeeld in een koude berging)?

Na akkoord bouw ik 4c in één PR, als versie `v0.53.0-fe.3`.
