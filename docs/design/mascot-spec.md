# Taskhog — Mascote (Coelho)

> **Personagem:** coelho pixel art 1-bit, tom **divertido/leve**, versões por status.  
> **Versão:** 1.0 · **Data:** 2026-06-20  
> **Uso v1:** splash (T0) + estados vazios (Home sem atividade, fila vazia)

---

## 1. Identidade

| Atributo | Valor |
|---|---|
| Espécie | Coelho |
| Nome interno | `bunny` (código/assets) |
| Estilo | **Pixel art 1-bit** — sem antialiasing, sem cinza |
| Tom | Playful — expressivo mas legível em 48×48 |
| Inspiração | Tamagotchi / ícones Game Boy — formas simples, olhos grandes |

O coelho **não substitui** ícones funcionais críticos (mic em T2, ⚠ em T10). Ele aparece onde o device está “ocioso” ou precisa de companhia visual sem distrair da ação principal.

---

## 2. Tamanhos de exportação

| Token | Px | Uso |
|---|---:|---|
| `bunny_xs` | 12×12 | Reservado v2 (status bar) — **não usar v1** |
| `bunny_sm` | 24×24 | Inline em listas vazias |
| `bunny_md` | 48×48 | T0 splash, estados vazios na Home |
| `bunny_lg` | 64×64 | Hero em telas vazias dedicadas (opcional) |

Todos os sprites devem ser **pixel-aligned** (coordenadas inteiras, espessura 1 px).

---

## 3. Anatomia base (silhueta comum)

Elementos constantes em todas as variantes:

```text
       ██  ██          ← orelhas (2 px largura mínima)
      ████████         ← topo da cabeça
     ██      ██        ← bochechas
     █   ██   █        ← olhos (2×2 px cada)
      █ ████ █         ← focinho
       ██  ██          ← dentes opcionais (playful)
    ████████████       ← corpo compacto
    ██        ██       ← pés/patas
```

| Parte | Regra |
|---|---|
| Orelhas | Sempre visíveis; inclinação comunica emoção |
| Olhos | 2×2 px sólidos; pupil sem brilho (1 bpp) |
| Focinho | Triângulo ou “Y” invertido, 3–5 px |
| Corpo | Oval baixo; max 60% da altura do sprite |
| Patas | 2 px largura; opcional na variante “sleep” |

**Contraste:** desenhar sempre preto sobre branco, exceto se a tela for invertida (T2 — **sem mascote**).

---

## 4. Variantes por status

### 4.1 Mapa variante → tela/estado

| ID | Nome | Quando | Expressão | Wireframe |
|---|---|---|---|---|
| `V0` | boot | T0 power-on | Acenando / orelhas retas, olhos abertos | `t00_boot.png` |
| `V1` | idle_calm | T1 Home, fila=0, sem destaque | Sentado, relaxado | `t01_home_empty.png` |
| `V2` | queue_empty | T6 fila vazia | Ombros encolhidos, “nada aqui” | `t06_queue_empty.png` |
| `V3` | saved_happy | T3 pós-gravação | **v2** — pulo com ✓ | — |
| `V4` | sync_wait | T4 sincronizando | **v2** — olhar para cima / relógio | — |
| `V5` | error_worry | T10 erro | **v2** — orelha caída | — |
| `V6` | battery_tired | T8 crítico | **v2** — deitado | — |
| `V7` | charging | T9 USB | **v2** — sentado com raio | — |

**v1 implementa:** V0, V1, V2. Demais variantes documentadas para iteração futura.

### 4.2 Descrição visual por variante (v1)

#### V0 — boot
- Orelhas para cima, simétricas.
- Olhos abertos, leve “smile” (1 px boca curva).
- Uma pata levantada (aceno).
- Posição T0: centro Y≈110, 48×48 abaixo do wordmark.

#### V1 — idle_calm (Home vazia)
- Orelhas inclinadas para fora (relaxado).
- Olhos meio-fechados (linha horizontal 2 px).
- Corpo sentado; texto substitui contadores: “Nenhuma tarefa pendente”.
- Mantém relógio grande — coelho **abaixo** da data, menor (32×32) se faltar espaço.

#### V2 — queue_empty
- Orelhas uma erguida, uma caída (curioso/vazio).
- Boca “o” pequena (círculo 2 px).
- Texto: “Fila vazia” + “Grave algo!”

---

## 5. Regras de composição em telas

| Tela | Mascote? | Posição | Notas |
|---|---|---|---|
| T0 | Sim V0 | Centro | Wordmark acima |
| T1 (com dados) | Não | — | Mostra métricas Hub |
| T1 (vazia) | Sim V1 | Zona B inferior | Relógio permanece hero |
| T2 | **Nunca** | — | Mic + invertido; zero distração |
| T3–T5 | Não v1 | — | Feedback de tarefa |
| T6 (com jobs) | Não | — | Lista ocupa espaço |
| T6 (vazia) | Sim V2 | Centro zona B | — |
| T7–T11 | Não v1 | — | Texto/instrução densos |

---

## 6. Paleta e export

- Fundo: branco `#FFFFFF`
- Tinta: preto `#000000`
- Formato: PNG 1-bit ou sprite sheet horizontal com variantes nomeadas

```text
assets/mascot/
  bunny_v0_boot_48.png
  bunny_v1_idle_calm_48.png
  bunny_v2_queue_empty_48.png
  bunny_sheet_v1.png          # opcional: 3 frames em linha
```

Conversão firmware: array C `const uint8_t bunny_v0_boot_48[]` MSB-first, 48×48/8 bytes por linha.

---

## 7. Copy associado (estados vazios)

| Variante | Título | Subtítulo |
|---|---|---|
| V1 | — (relógio domina) | `Nada na fila` |
| V2 | `Fila vazia` | `Segure REC e fale` |

Tom playful, imperativo curto — alinhado a `epaper-ui-spec.md` §9.

---

## 8. Prompts para gerar sprites (GPT / Claude / pixel tool)

### Sprite único
```text
Crie um coelho pixel art monochrome 48×48 px, estilo Game Boy 1-bit.
Variante: [boot acenando | idle relaxado | fila vazia curioso].
Fundo branco, linhas pretas 1 px, sem antialiasing.
Entregue: grid ASCII (# preto, . branco) 48 linhas + PNG se possível.
Personagem: Taskhog — playful, olhos 2×2 px, orelhas longas.
```

### Sheet completa v1
```text
Leia docs/design/mascot-spec.md. Gere 3 sprites 48×48 em pixel art 1-bit:
V0 boot, V1 idle_calm, V2 queue_empty.
Mesma anatomia base; só muda expressão/orelhas.
Layout: 3 colunas em PNG 144×48.
```

---

## 9. Critérios de aceite

- [ ] Reconhecível como coelho em 48×48 a ~188 DPI
- [ ] Três variantes v1 (V0, V1, V2) consistentes (mesma proporção de cabeça/corpo)
- [ ] Legível em e-Paper sem blur
- [ ] Não aparece em T2 (gravação)
- [ ] Wireframes T0, T1 empty, T6 empty atualizados

---

## 10. Referências

- [`epaper-ui-spec.md`](epaper-ui-spec.md) — layout e telas
- [`wireframes/`](wireframes/) — PNGs gerados
- [`generate_wireframes.py`](generate_wireframes.py) — regenerar após ajuste de sprite
