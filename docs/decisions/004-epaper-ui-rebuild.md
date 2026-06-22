# ADR 004 — Reconstrução da UI e-Paper (orientação + assets)

**Data:** 2026-06-22
**Status:** Aceito

## Contexto

O display mostrava conteúdo **espelhado/invertido** de forma persistente, e várias tentativas anteriores de corrigir não resolveram. A camada de UI também usava uma fonte bitmap 8×8 genérica, distante dos mockups (`ui/screens/*.png`, fonte SpaceMono, mascote).

## Causa raiz

O driver antigo (`epaper_drv.c`) tentava corrigir orientação **mexendo nos registradores do SSD1681**: data entry mode `0x11=0x00` (X--, Y--) + janela X invertida + contadores no máximo. Esse "remendo nos registradores" interagia de forma imprevisível com o empacotamento de bits do framebuffer, produzindo combinações de espelho-X/Y/rotação difíceis de raciocinar — daí o ciclo de tentativas sem sucesso.

## Decisão

1. **Init do painel 100% canônico**, sem hacks de orientação: data entry `0x11=0x03`, janelas/contadores RAM começando em 0.
2. **Toda orientação isolada em um único transform de software** no flush (`build_tx_from_logical`), controlado por `components/ui/epaper_cfg.h`:
   - `EPD_MIRROR_X`, `EPD_MIRROR_Y`, `EPD_SWAP_XY` (cobrem as 8 orientações).
   - Calibrado com padrão assimétrico (bloco no canto + "F" + seta) via flag `EPD_CALIBRATION`.
   - **Valor travado para este painel: `MIRROR_Y=1`** (X e rotação corretos; só Y precisava inverter).
3. **Waveform:** o OTP deste painel é não-confiável (`0x22=0xF7` não atualiza). Mantido o caminho comprovado: **LUT custom** (`WF_FULL_1IN54` via `0x32`) + power-on (`0x22=0xB1`) + update (`0x22=0xC7`).
4. **Buffer lógico** com origem topo-esquerda (igual aos mockups); o transform converte para a RAM nativa só no flush.
5. **Fontes = SpaceMono** (Regular/Bold) e **assets = SVG** de `ui/assets/`, ambos rasterizados para C por `tools/gen_assets.py` (Pillow + cairosvg). Saída commitada em `assets_fonts.{c,h}` e `assets_images.{c,h}`.
6. **Camada de canvas reescrita** (`framebuffer.c`): texto com fontes reais, blit de imagem 1-bit, primitivas. `font8x8.{c,h}` removido.

## Escopo desta leva

- Telas: **T0 splash, T1 Home (com/sem fila), T2 Recording, T3 Saved, T4 Sync**.
- Só **full refresh** (partial fica para depois).

## Consequências

- Reorientar o display, se um lote de painéis vier diferente, é trocar **1 knob** em `epaper_cfg.h` — não reescrever driver.
- Geração de assets exige o venv `tools/.venv` (Pillow + cairosvg); regenerar com `tools/.venv/bin/python tools/gen_assets.py`.

## Pendências

- Telas T5–T11; partial refresh; fontes de status de **Wi-Fi** e **charging** para `widget_read_status` (hoje barra mostra offline / sem ícone de carga).

## Referências

- `docs/hardware/HARDWARE_NOTES.md` §e-Paper · `docs/specs/01-device-firmware.md` §12.1.1 · `docs/design/epaper-ui-spec.md` §1.1
- Código: `taskhog-fw/components/ui/`, `tools/gen_assets.py`
