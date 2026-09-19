> **Nota:** [Emu4VitaPlus](https://github.com/noword/Emu4VitaPlus) (il progetto originale) è un frontend libretro che compila decine di core diversi (NES, SNES, Genesis, arcade, ecc). Questo fork compila **solo il core gpSP** (Game Boy Advance, con dynarec ARM reale via [libretro/gpsp](https://github.com/libretro/gpsp)) — la vpk risultante è solo l'emulatore GBA con la funzione "Crea bolla", non l'intera suite Emu4VitaPlus.

## gpSP (Emu4VitaPlus) + "Create Vita Bubble"

Questo è il fratello GBA di [pnes-bubble](https://github.com/figarocool/pnes-bubble), [psnes-bubble](https://github.com/figarocool/psnes-bubble) e [bubble-mgba](../bubble-mgba): stessa funzione, "trasforma qualsiasi ROM in una bolla PS Vita indipendente", applicata stavolta a **gpSP** invece che a mGBA.

**Perché gpSP e non mGBA per il GBA?** [bubble-mgba](../bubble-mgba) usa mGBA, che su Vita è un puro interprete C (nessun dynarec) e fatica sui giochi GBA più pesanti (es. Pokémon FireRed gira lento). gpSP invece include un vero dynarec ARM→ARM, lo stesso usato da RetroArch su Vita, quindi le prestazioni sono molto migliori sugli stessi giochi.

### Come si usa
1. Compila (o scarica dalla sezione releases) la vpk e installala
2. Copia `gba_bios.bin` in `ux0:data/EMU4VITAPLUS/system/gba_bios.bin`
3. Copia le tue ROM `.gba` in una cartella qualsiasi (es. `ux0:data/roms/gba/`)
4. Apri l'app, naviga fino alla ROM, selezionala e scegli **"Create Vita bubble"** dal menu
5. Attendi la creazione (serve WiFi per scaricare la copertina)
6. La bolla apparirà sulla LiveArea con icona/copertina del gioco e titolo corretto — si avvia direttamente nel gioco, senza passare dalla UI di Emu4VitaPlus

### Cosa fa dietro le quinte
- Riconosce il nome vero del gioco tramite il **CRC32 della ROM**, e scarica la copertina da [libretro-thumbnails](https://github.com/libretro-thumbnails) ("Nintendo - Game Boy Advance"), convertita in PNG a palette (richiesto da Sony, altrimenti l'installazione fallisce con errore `0x8010113D`)
- Clona l'intero pacchetto dell'app (eboot, assets, overlay) insieme alla ROM scelta in un nuovo pacchetto App
- Genera un `param.sfo` con `TITLE_ID`/`TITLE`/**`CONTENT_ID`** univoci per ogni bolla (il `CONTENT_ID` univoco è necessario: senza, la 2ª bolla creata perde l'icona piccola in LiveArea perché la shell la mette in cache per content-id) e il file `sce_sys/package/head.bin` richiesto dal sistema (senza il quale l'installazione fallisce con `0x8010111C`)
- Si auto-installa come bolla reale tramite `scePromoterUtilityPromotePkgWithRif` — richiede il modulo `SCE_SYSMODULE_INTERNAL_PAF` caricato prima e un eboot compilato con authid elevato (`0x2808000000000000`, lo stesso di SceShell/VitaShell)
- All'avvio la bolla imposta una flag (`gBubbleMode`) che nasconde completamente boot-log e UI grafica di Emu4VitaPlus finché il gioco non è partito — zero flicker, ma il tasto **PS** resta sempre funzionante per aprire il menu in-game (che si apre di default sul tab **Sistema**, non Stato)
- La bolla riconosce da sola la ROM inclusa (`app0:bubble/`) e la carica direttamente, senza passare dalla lista giochi

### Nota tecnica
Questo repo è una versione ridotta del monorepo Emu4VitaPlus: `cmake/cores.cmake` elenca solo la riga `gpsp` (invece delle ~50 righe originali), il top-level `CMakeLists.txt` compila sempre e solo `BUILD=gpsp` (niente modalità `All`/`Arch`, quindi niente cartella `arch/`), e `deps/`/`cores/` includono solo i submodule realmente usati da gpSP: [`gpsp`](https://github.com/libretro/gpsp), `libretro-common`, `simpleini`, `7-Zip`, `lz4`, `rcheevos`, `minizip-ng`, `zlib-ng` (più `libvita2d`/`libvita2d_ext`, vendorizzati come file normali anche a monte).

La funzione bolla è implementata in `frontend/source/bubble_maker.cpp/.h` (nuovo file, non presente in Emu4VitaPlus), agganciata alla UI in `frontend/source/ui/tabs/tab_browser.cpp` (voce di menu "Create Vita bubble") e all'avvio in `frontend/source/main.cpp` (`CheckBubbleRom()`) e `frontend/source/ui/ui.cpp` (flag `gBubbleMode`, gestita in `Ui::Show()`/`NotifyBootResult()`).

`gba_bios.bin` **non** è incluso nel repo (per motivi di copyright, come da BIOS reali) — va copiato manualmente sulla console.

---

## gpSP (Emu4VitaPlus) + "Create Vita Bubble"

> **Scope:** [Emu4VitaPlus](https://github.com/noword/Emu4VitaPlus) (the upstream project) is a libretro-based frontend that builds dozens of different cores (NES, SNES, Genesis, arcade, etc). This fork only builds the **gpSP core** (Game Boy Advance, with a real ARM dynarec via [libretro/gpsp](https://github.com/libretro/gpsp)) — the resulting vpk is just the GBA emulator with the new bubble feature, not the full Emu4VitaPlus suite.

This is the GBA sibling of [pnes-bubble](https://github.com/figarocool/pnes-bubble), [psnes-bubble](https://github.com/figarocool/psnes-bubble) and [bubble-mgba](../bubble-mgba): same "turn any rom into a standalone PS Vita bubble" feature, this time built on **gpSP** instead of mGBA.

**Why gpSP instead of mGBA for GBA?** [bubble-mgba](../bubble-mgba) uses mGBA, which on Vita is a pure C interpreter (no dynarec) and struggles on heavier GBA titles (e.g. Pokémon FireRed runs slow). gpSP ships a real ARM→ARM dynarec — the same one RetroArch uses on Vita — so performance on the same games is much better.

### How to use it
1. Build (or grab from releases) the vpk and install it
2. Copy `gba_bios.bin` to `ux0:data/EMU4VITAPLUS/system/gba_bios.bin`
3. Copy your `.gba` roms anywhere (e.g. `ux0:data/roms/gba/`)
4. Open the app, browse to the rom, select it and choose **"Create Vita bubble"** from the menu
5. Wait for it to build (needs WiFi to download the cover art)
6. The bubble appears on the LiveArea with the game's icon/art and correct title — launches straight into the game, skipping the Emu4VitaPlus UI entirely

### What happens under the hood
- Resolves the game's real title from the rom's **CRC32**, and downloads cover art from [libretro-thumbnails](https://github.com/libretro-thumbnails) ("Nintendo - Game Boy Advance"), converted to indexed-palette PNGs (Sony's validator rejects plain truecolor PNGs with error `0x8010113D`)
- Clones the app's whole package (eboot, assets, overlays) alongside the chosen rom into a new app package
- Generates a `param.sfo` with a unique `TITLE_ID`/`TITLE`/**`CONTENT_ID`** per bubble (the unique `CONTENT_ID` matters: without it, the 2nd bubble created loses its small LiveArea icon because the shell caches grid icons by content-id) and the `sce_sys/package/head.bin` the system requires (missing it fails with `0x8010111C`)
- Self-installs as a real bubble via `scePromoterUtilityPromotePkgWithRif`, which needs the `SCE_SYSMODULE_INTERNAL_PAF` module loaded first and an eboot built with an elevated authid (`0x2808000000000000`, the same one SceShell/VitaShell uses)
- On boot the bubble sets a flag (`gBubbleMode`) that fully hides Emu4VitaPlus's boot log and UI chrome until the game has started — zero flicker, but the **PS button** still works to open the in-game menu (which opens on the **System** tab by default, not State)
- The bubble detects its bundled rom (`app0:bubble/`) on its own and loads it directly, never touching the rom list

### Technical note
This repo is a trimmed-down copy of the Emu4VitaPlus monorepo: `cmake/cores.cmake` lists only the `gpsp` row (instead of the ~50 original rows), the top-level `CMakeLists.txt` always builds `BUILD=gpsp` (no `All`/`Arch` combined mode, so no `arch/` folder), and `deps/`/`cores/` only vendor the submodules gpSP actually needs: [`gpsp`](https://github.com/libretro/gpsp), `libretro-common`, `simpleini`, `7-Zip`, `lz4`, `rcheevos`, `minizip-ng`, `zlib-ng` (plus `libvita2d`/`libvita2d_ext`, vendored as plain files upstream too).

The bubble feature lives in `frontend/source/bubble_maker.cpp/.h` (new file, not present in Emu4VitaPlus), wired into the UI in `frontend/source/ui/tabs/tab_browser.cpp` ("Create Vita bubble" menu entry) and into boot in `frontend/source/main.cpp` (`CheckBubbleRom()`) and `frontend/source/ui/ui.cpp` (the `gBubbleMode` flag, handled in `Ui::Show()`/`NotifyBootResult()`).

`gba_bios.bin` is **not** bundled in this repo (copyright — it's a real console BIOS dump) and must be copied to the device manually.

## Building

Requires [VitaSDK](https://vitasdk.org/). From the project root:

```
git submodule update --init --recursive
mkdir build && cd build
cmake -DBUILD=gpsp ..
make -j$(nproc)
```

The vpk is produced at `build/out/gpSP_Emu4VitaPlus_<version>.vpk`.

## Roms

- Roms can live anywhere reachable from the file browser (e.g. `ux0:data/roms/gba/`) — Emu4VitaPlus remembers the last folder you browsed to.
- `gba_bios.bin` must be placed at `ux0:data/EMU4VITAPLUS/system/gba_bios.bin` or the app will warn about a missing BIOS.
