# Voorstel: NVS-opslag in OpenQuatt FE

Status: stap 1 gebouwd (fe.12). Stap 2 wacht op de bankmeting.

**Besluiten (13-10):** 1 ja, 2 ja (alleen als de meting erom vraagt), 3 ja (samen met stap 2), 4 ja (na de meting).

## Probleem

De Q heeft een NVS-partitie van 24 KiB: zes pagina's van 126 entries. Eén pagina blijft vrij voor garbage collection, dus er zijn **630 entries** bruikbaar. Sinds v0.54.1 schat `scripts/check_nvs_budget.py` de bezetting als elke instelling één keer is weggeschreven. Upstream houdt minstens 100 entries vrij als marge (`REQUIRED_AVAILABLE_ENTRIES`).

| Doel | Geschat | Vrij | Marge 100 |
|---|---:|---:|---|
| Upstream Q Duo v0.54.1 | 570 | 60 | haalt het niet |
| OpenQuatt FE fe.11 | 720 | **−90** | past niet |

FE voegt 51 bewaarde waarden toe. Een kleine ESPHome-blob kost 3 entries, dus samen ongeveer 153 entries:

| Soort | Aantal | Entries | Waar |
|---|---:|---:|---|
| number | 35 | 105 | `fe_dhw.yaml` (30), `fe_dhw_tank.yaml` (5) |
| switch | 10 | 30 | `fe_dhw.yaml` (8), `fe_dhw_tank.yaml` (2) |
| global | 5 | 15 | legionella-tijdstempel, UA en aantal samples, tappingen vandaag, laatste tapping |
| select | 1 | 3 | DHW ruimtetemperatuur bron |

## Wanneer het echt misgaat

Nagelopen in de ESPHome 2026.9.0-bron (`components/esp32/preferences.cpp` en de componenten):

- Een **number** of **select** schrijft pas bij de eerste wijziging. Een setting die op de standaardwaarde staat, kost niets.
- Een **switch** met `RESTORE_*` schrijft bij elke `publish_state`, dus al bij de eerste boot. De 10 FE-switches staan er altijd.
- Een **global** met `restore_value` schrijft zodra de waarde afwijkt van de beginwaarde.
- **Een back-up terugzetten** schrijft alle instellingen tegelijk. Daarmee is de schatting geen theoretische bovengrens maar een realistisch scenario, bijvoorbeeld bij de ingebruikname of na een reset.

Is de NVS vol, dan faalt `nvs_set_blob`. ESPHome logt dat alleen ("Writing N items: … failed"); in de app zie je niets. De gewijzigde waarde werkt tot de herstart en is daarna terug op de oude stand. Ook systeemrecords (WiFi fast-connect, PHY-kalibratie) staan in deze partitie.

De schatting kan de echte bezetting zowel over- als onderschatten. Fragmentatie, vendorrecords en oude sleutels van eerdere testfirmware tellen niet mee. Upstream mat op een testcontroller 169 vrije entries waar de schatting 60 gaf. Meten op de Q zelf is dus de eerste stap.

## Voorstel in twee stappen

### Stap 1: meten (klein, altijd doen)

- Na het opstarten en daarna elk uur logt FE `nvs_get_stats`: used, free, available, namespaces. De logfunctie bestaat al in upstream (`oq_nvs_cleanup::log_stats`).
- Een diagnostische sensor **"NVS vrije entries"** (`available_entries`), zichtbaar in de web-app onder Service → Bankproef warm water en in HA.
- Boven 50 vrije entries blijft het stil. Daaronder komt er een waarschuwing in het log. Onder 20 komt er een melding op de warmwaterkaart: "Opslag bijna vol: wijzigingen worden mogelijk niet bewaard".

**Meetprotocol op de bank (onderdeel van fase 6):**

1. Flash met wissen (web.esphome.io → "Erase device"). Zo verdwijnen oude sleutels van fe.3–fe.11.
2. Configureer WiFi en lees "NVS vrije entries" af. Dat is de **basis**.
3. Zet een back-up terug waarin alle instellingen afwijken van de standaard, inclusief alle DHW-instellingen. Herstart en lees opnieuw af. Dat is de **volle bezetting**.

**Beslisregel:**

- Volle bezetting met **≥ 100 vrij**: klaar. Stap 2 vervalt; de meting blijft als bewaking.
- **Tussen 20 en 100 vrij**: stap 2 bouwen vóór de ingebruikname.
- **Minder dan 20 vrij**, of fouten in het log: de Q niet in gebruik nemen tot stap 2 er is.

### Stap 2: DHW-instellingen bundelen (alleen als de meting dat vraagt)

Hiervoor gebruiken we upstreams eigen ontwerpregels (`docs/development/nvs-persistence.md`, "Vervolgontwerp voor gebundelde opslag"): bundel alleen waarden met dezelfde eigenaar en dezelfde levenscyclus.

- **Eén record "DHW-instellingen"** voor de 35 numbers, 10 switches en 1 select. Header (magic, versie, lengte, CRC) van 12 bytes, plus 35 × 4 + 10 + 1 bytes = 163 bytes, dus **8 entries in plaats van 138**. Dat is ongeveer 130 entries winst.
- De entities blijven dezelfde: dezelfde namen, dezelfde web-app, hetzelfde HA-dashboard. Alleen `restore_value` / `restore_mode` gaan uit. Een FE-header herstelt het record vóór de eerste DHW-tick en schrijft alleen bij een echte wijziging, gebundeld: na 5 s rust, zodat een back-up terugzetten één write wordt.
- Is het record ongeldig (CRC of versie), dan gelden de standaardwaarden en komt er een melding in het log. Er worden geen half-geldige waarden geladen.
- De globals blijven los. Het zijn runtime-tellers met een eigen ritme. **Tappingen vandaag** en **laatste tapping** kunnen naar RAM (6 entries). Na een herstart begint de dagteller dan op nul.

**Geschat resultaat:** FE gaat van 720 naar ongeveer **584 entries (46 vrij)**. Dat is beter dan nu, maar nog onder de marge van 100. Daarom blijft de meting uit stap 1 de echte toets.

**Migratie:** niet nodig, omdat de Q nog niet in gebruik is. Bij de ingebruikname flash je met wissen en stel je de instellingen in. Bouwen we stap 2 pas ná de ingebruikname, dan is wél een migratie nodig volgens de upstreamregels: eerst het nieuwe record schrijven en teruglezen, pas daarna de oude sleutels wissen. Dat is meer werk. Een reden om de beslissing vóór de ingebruikname te nemen.

## Bewust niet

- **Grotere NVS-partitie:** de partitietabel kan niet via OTA veranderen. Elke update zou dan via USB moeten, en FE wijkt af van upstreams tabel.
- **DHW-instellingen in `openquatt_data`:** dat is upstreams historiepartitie met een eigen layout en eigen bewaarbeleid. Je eigen records daarin zetten is kwetsbaar bij elke upstream-update.
- **Minder instellingen bewaren:** alle 35 numbers zijn echte afstelwaarden. Ze na een herstart kwijtraken is erger dan het probleem.

## Upstream

Upstream Q Duo haalt zelf ook de marge van 100 niet (60 vrij). Het meetdeel uit stap 1 is ook voor upstream bruikbaar. Na de bankmeting kan ik voor Jeroen een korte melding met de cijfers maken, als je dat wilt.

## Keuzes

1. Stap 1 bouwen (meten en de sensor)? **Advies: ja.** Klein en zonder risico.
2. Stap 2 alleen bouwen als de bankmeting volgens de beslisregel daarom vraagt? **Advies: ja.**
3. Tappingen vandaag en laatste tapping naar RAM (na een herstart begint de dagteller op nul)? **Advies: ja**, maar alleen samen met stap 2.
4. De cijfers na de meting aan Jeroen melden?
