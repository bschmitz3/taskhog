#pragma once

/*
 * Calibração de orientação do e-Paper — ÚNICO lugar que mexe em orientação.
 *
 * O init do painel é 100% canônico (SSD1681 stock, data entry 0x03). NENHUM
 * hack de orientação acontece nos registradores. Toda correção de
 * espelhamento/rotação acontece no flush do framebuffer, controlada APENAS
 * pelos três knobs abaixo. Há 8 combinações possíveis (4 rotações × espelho).
 *
 * Procedimento de calibração (uma vez):
 *   1. EPD_CALIBRATION = 1, gravar, observar o padrão no display.
 *   2. O padrão correto: quadrado sólido no CANTO SUPERIOR ESQUERDO, a letra
 *      "F" legível (não espelhada, não de cabeça para baixo) e a seta
 *      apontando para a DIREITA no topo.
 *   3. Ajustar os três knobs até bater. Depois EPD_CALIBRATION = 0.
 *
 * Mapeamento lógico→painel (lx,ly = coordenadas de tela, origem topo-esq):
 *   px = EPD_SWAP_XY ? ly : lx
 *   py = EPD_SWAP_XY ? lx : ly
 *   if (EPD_MIRROR_X) px = (W-1) - px
 *   if (EPD_MIRROR_Y) py = (H-1) - py
 */

#define EPD_CALIBRATION 0

#define EPD_MIRROR_X 0
#define EPD_MIRROR_Y 1
#define EPD_SWAP_XY  0
