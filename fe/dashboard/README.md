# Home Assistant — warm water (OpenQuatt FE)

Bestanden voor Home Assistant bij de FE-firmware (Q Duo Full Electric). Ze komen uit
de LilyGO-fork (`docs/dashboard/`) en zijn nagelopen tegen de entiteitsnamen van FE.

| Bestand | Wat |
|---|---|
| `openquatt_ha_dhw_hotwater_package.yaml` | Package met berekende sensoren: gemiddelde tanktemperatuur, warm water in de tank, tapbaar mengwater (V40), energie-inhoud, douchetijd en lading. Plus vier `input_number`s voor de aannames. |
| `openquatt_dhw_hotwater_buttoncard.yaml` | Geanimeerde tank (custom:button-card). |
| `openquatt_dhw_hotwater_card.yaml` | Losse kaart met lading-gauge, kerncijfers, stratificatie en aannames. |
| `fe-warmwater-view.yaml` | Complete view "Warm water": tank, status, acties en doelen. |

## Installatie

1. Zet packages aan in `configuration.yaml`:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
2. Kopieer `openquatt_ha_dhw_hotwater_package.yaml` naar `<config>/packages/` en herstart Home Assistant.
3. Installeer via HACS: `button-card` en `mini-graph-card` (de losse kaart gebruikt ook `apexcharts-card`).
4. Dashboard bewerken → ⋮ → *Ruwe configuratie-editor* → plak de inhoud van `fe-warmwater-view.yaml` als nieuw item onder `views:`.

## Entity-ids

De ids gaan uit van apparaatnaam `openquatt` (standaard in `oq_substitutions_common.yaml`), bijvoorbeeld
`sensor.openquatt_dhw_tank_top`. Heet je apparaat anders, vervang dan het voorvoegsel `openquatt_`.
Alle ids in deze map zijn gecontroleerd tegen de `name:`-velden in de FE-firmware.

## Van de LilyGO-fork naar FE

| Fork | FE |
|---|---|
| CM4 — warm water | **CM10** — warm water |
| CM6 — alleen element | **CM11** — alleen element |
| CWT-TM-8PT voor de tanktemperatuur | twee Dallas-sensoren (tank top en tank bottom) |
| CV-ketel als naverwarming | geen ketel; het 3 kW-element op R2 |

Automatiseringen die op `sensor.openquatt_control_mode_label` letten met "CM4" of "CM6" moeten naar
"CM10 - Hot water" en "CM11 - Element only". Tariefsturing en adaptief leren uit de fork zitten niet in FE;
tapdetectie, element-only en het geleerde stilstandsverlies wel.
