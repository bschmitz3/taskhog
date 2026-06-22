# Taskhog — Especificação de Interface e-Paper

> **Documento pilar para design de UI.** Use este arquivo como contexto ao trabalhar com GPT, Claude ou ferramentas de design para criar layouts, ícones e assets do display.
>
> **Versão:** 1.0 · **Data:** 2026-06-20  
> **Documentos relacionados:** PRD §13/§15.3 · Spec 01 §12 · `docs/hardware/waveshare-esp32-s3-epaper-1.54-v2.md`

---

## 1. Para que serve este documento

Este spec traduz requisitos de produto e limitações de hardware em **regras concretas de layout**, lista **todas as telas e estados** que o firmware deve renderizar, e define **como exportar assets** para o ESP32.

Ao pedir ajuda a um LLM ou designer, anexe ou cole:

1. Este documento inteiro (ou a seção relevante).
2. A tela específica que quer criar (ex.: "T2 Recording").
3. O formato de saída desejado (wireframe ASCII, PNG 200×200 1-bit, descrição de componentes).

**Princípio rector (P6 da PRD):** o usuário deve entender o estado do device num relance — especialmente se está gravando, se a captura foi salva, e se algo falhou **sem parecer que perdeu dados**.

---

## 1.1 Status de implementação (2026-06-22)

A camada de UI foi **reconstruída do zero** para resolver de vez o espelhamento de orientação. Ver `docs/decisions/004-epaper-ui-rebuild.md`.

| Item | Status |
|---|---|
| Orientação correta (não espelhada) | ✅ resolvido — `MIRROR_Y=1`, transform único em `epaper_cfg.h` |
| Fontes | ✅ **SpaceMono** (Regular/Bold) bitmap via `tools/gen_assets.py` |
| Assets (mascote + ícones) | ✅ gerados dos SVG de `ui/assets/` |
| T0 Splash · T1 Home · T2 Recording · T3 Saved · T4 Sync | ✅ implementadas |
| T5 · T6 · T7 · T8 · T9 · T10 · T11 | ⏳ pendentes |
| Partial refresh | ⏳ pendente (só full refresh hoje) |
| Status Wi-Fi / charging na barra | ⏳ sem fonte de dados ainda (mostra offline / sem carga) |

**Mascote por estado:** IDLE c/ fila = `default` · IDLE s/ fila = `sleeping` · RECORDING = `angry` · CONFIRM = `happy`.

**Geração de assets:** `tools/.venv/bin/python tools/gen_assets.py` (Pillow + cairosvg). Edita-se a lista de fontes/imagens no script; os `.c/.h` gerados em `taskhog-fw/components/ui/` são commitados.

---

## 2. Contexto do produto (resumo)

| Item | Valor |
|---|---|
| Produto | Taskhog — captura de tarefas por voz → Todoist via Hub |
| Interação principal | Segurar **REC** (GPIO0/BOOT) → falar → soltar |
| Interação secundária | Tap **NAV** (GPIO18/PWR) → navegar fila/info |
| Touch | **Não** — placa sem touch; só 2 botões físicos |
| Idioma da UI | **Português (BR)** |
| Tom de voz | Calmo, direto, tranquilizador em erros |

---

## 3. Especificações do display

### 3.1 Hardware

| Parâmetro | Valor |
|---|---|
| Painel | Waveshare 1.54" e-Paper V2 |
| Resolução | **200 × 200 px** |
| Profundidade | **1 bpp** — preto e branco apenas |
| Área ativa | 27 × 27 mm (~188 DPI) |
| Controlador (provável) | SSD1681 |
| Framebuffer | 5.000 bytes (`200 × 200 / 8`) |
| Orientação canônica | **Portrait** (200 largura × 200 altura) |
| Backlight | Nenhum — reflexivo, luz ambiente |

### 3.2 Comportamento do e-Paper (impacto no design)

| Característica | Implicação para UI |
|---|---|
| Biestável | Imagem permanece sem energia; **não** precisa "manter ligado" |
| Refresh lento (~0,3–2 s) | Sem animações fluidas; estados discretos |
| Partial refresh | OK para contador mm:ss e relógio; **não** redesenhar tela inteira a cada segundo |
| Ghosting | Intercalar refresh **total** a cada N parciais (default N=10) ou 1×/dia |
| Contraste ~10:1 | Evitar cinzas simulados finos; preferir preto sólido ou branco |
| Sem antialiasing fino | Fontes e ícones devem ser **pixel-aligned** |

### 3.3 Política de atualização (firmware)

- Atualizar display **somente quando o estado mudar** ou em elementos dinâmicos autorizados (contador de gravação, relógio na home).
- **Durante RECORDING:** permitido partial refresh só na zona do contador/barra — nunca bloquear o caminho de áudio.
- **Durante SYNC/Wi-Fi:** refresh mínimo; priorizar upload.
- Desligar alimentação do painel (`EPD3V3_EN`) antes de deep sleep.

---

## 4. Sistema de layout

### 4.1 Grid e zonas fixas

Todas as telas (exceto splash e alguns erros full-screen) usam **três zonas horizontais** para maximizar reuso em partial refresh:

```text
┌──────────────────────────── 200 px ────────────────────────────┐
│ ZONA A — Status bar                              altura: 24 px │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│ ZONA B — Conteúdo principal                  altura: ~156 px     │
│                                                                │
├────────────────────────────────────────────────────────────────┤
│ ZONA C — Rodapé / dica de ação               altura: 20 px     │
└────────────────────────────────────────────────────────────────┘
  ↑ margem lateral recomendada: 8 px cada lado (área útil: 184 px)
```

| Zona | Y (px) | Conteúdo |
|---|---:|---|
| A — Status | 0–23 | Hora, Wi-Fi, bateria, contador de fila |
| B — Conteúdo | 24–179 | Mensagem principal, ícone hero, listas |
| C — Rodapé | 180–199 | Hint do botão ("segure ⏺ para gravar") |

**Separador:** linha horizontal 1 px preta entre zona A e B (x: 8–191).

### 4.2 Espaçamento

| Token | Valor | Uso |
|---|---:|---|
| `space-xs` | 4 px | Entre ícone e rótulo inline |
| `space-sm` | 8 px | Margem lateral, padding interno |
| `space-md` | 12 px | Entre blocos de conteúdo |
| `space-lg` | 16 px | Entorno de ícone hero |
| `space-xl` | 24 px | Respiro vertical em telas sparse |

Alinhar todo texto e ícone em **grade de 2 px** (evita blur em 1 bpp).

### 4.3 Hierarquia tipográfica

| Nível | Altura alvo | Peso | Uso |
|---|---|---|---|
| Display | 32–40 px | Bold | Relógio na Home (T1) |
| Título | 24–28 px | Bold | "GRAVANDO", "Gravado!" |
| Subtítulo | 16–18 px | Regular | Data, duração, projeto |
| Corpo | 14–16 px | Regular | Listas, mensagens |
| Caption | 10–12 px | Regular | Status bar, rodapé |
| Micro | 8 px | Regular | Só se legível (evitar < 8 px) |

**Fontes:** preferir fontes bitmap fixas (ex.: IBM Plex Mono, Inter bitmap, ou fonte custom 1 bpp). **Não** usar fontes outline com hinting subpixel.

**Truncagem:** títulos de tarefa max **~22 caracteres** na linha; usar reticências (`…`) no firmware.

### 4.4 Paleta (1 bpp)

| "Cor" | Valor | Uso |
|---|---|---|
| Branco | `#FFFFFF` / bit 0 | Fundo |
| Preto | `#000000` / bit 1 | Texto, ícones, bordas |
| "Cinza" | **Dither 50%** checkerboard 2×2 | Barras de progresso vazias, disabled (usar com moderação) |

**Proibido:** gradientes suaves, sombras, transparência simulada fina, emoji colorido do sistema (substituir por ícones monocromáticos).

### 4.5 Iconografia

- Estilo: **outline 1–2 px** ou **filled sólido** — consistente em todas as telas.
- Tamanhos padrão: 12×12 (status bar), 24×24 (inline), 48×48 ou 64×64 (hero).
- Exportar como PNG 1-bit ou SVG convertido para bitmap.
- Ícones obrigatórios no set mínimo:

| Ícone | Significado |
|---|---|
| ⏺ / mic | Gravar / REC |
| ✓ | Sucesso / salvo |
| ↑N | Fila com N pendentes |
| 📶● / 📶○ | Wi-Fi conectado / desconectado (substituir por pictograma monochrome) |
| 🔋NN% | Bateria |
| 🪫 | Bateria crítica |
| ⚡ | Carregando USB |
| ⚠ | Erro / atenção |
| 🎙️ | Gravando (hero T2) |
| 📁 | Projeto Todoist |

> **Nota para design:** substituir emojis Unicode por **sprites bitmap** — emojis renderizam mal em fontes embedded pequenas.

---

## 5. Status bar (Zona A) — especificação

Presente em **todas as telas operacionais** (T1–T6, T8–T10). Opcionalmente simplificada em T7 (setup) e omitida em T0 (splash).

### 5.1 Layout da status bar

```text
┌──────────────────────────────────────────────────────────────┐
│ HH:MM   [wifi] [batt]                    [queue]             │
│  ←8px→  12px icons                          ↑N alinhado dir. │
└──────────────────────────────────────────────────────────────┘
```

| Slot | Posição | Conteúdo | Atualização |
|---|---|---|---|
| Hora | Esquerda, x≈8 | `HH:MM` 24h | Partial 1/min na Home |
| Wi-Fi | Após hora | ● preenchido = OK; ○ vazio = offline | Em mudança de rede |
| Bateria | Centro-esquerda | Ícone + `%` ou só ícone se crítico | Em mudança / a cada wake |
| Fila | Direita, x≈172 | `↑N` se N>0; omitir se 0 | Após enqueue/dequeue |
| Charging | Sobrepor bateria | `⚡` quando USB | Plug/unplug USB |

### 5.2 Variantes por nível de bateria

| Faixa | Ícone | Comportamento extra |
|---:|---|---|
| > 25% | 🔋 normal | — |
| 10–25% | 🔋 | Opcional: texto "economia" na Home |
| 3–10% | 🪫 | Tela T8 ou banner; sync só com USB |
| < 3% | — | Tela SAFE_OFF; display desliga |

---

## 6. Inventário de telas (T0–T11)

Cada tela é um **estado visual** mapeado à máquina de estados do firmware. IDs são estáveis — use-os em prompts e no código (`screens.c`).

### 6.1 Mapa rápido

| ID | Nome | Estado(s) firmware | Entrada | Duração típica |
|---|---|---|---|---|
| T0 | Boot / Splash | `BOOT` | Power-on | 1–2 s |
| T1 | Home / Idle | `IDLE` | Default após boot/wake | Persistente |
| T2 | Recording | `RECORDING` | REC pressionado | Enquanto segura |
| T3 | Saved / Queued | `CONFIRM` | REC solto | 2–4 s → T4 ou T1 |
| T4 | Syncing | `SYNC` | Auto após T3 ou wake timer | Até fila vazia |
| T5 | Last result | `IDLE` (subtela) | Após sync com resultado | 5–10 s → T1 |
| T6 | Queue / Pending | `IDLE` (subtela) | Tap NAV na T1 | Até tap NAV |
| T7 | Setup | `PROVISION` | Boot sem credenciais / BOOT segurado | Até provisionar |
| T8 | Low battery | overlay / `IDLE` | Bateria 3–10% | Persistente até USB |
| T9 | Charging | overlay / `IDLE`+USB | USB conectado | Enquanto carrega |
| T10 | Error | qualquer + erro ativo | Falha SD/rede/áudio/auth | 5 s ou até dismiss |
| T11 | Info / Diag | `IDLE` (subtela) | Tap NAV na T6 | Até tap NAV |

### 6.2 T0 — Boot / Splash

**Objetivo:** confirmar power-on; marca do produto.

```text
┌────────────────────────────┐
│                            │
│                            │
│        TASKHOG             │  wordmark, 28px bold
│         v1.0.0             │  caption
│                            │
│         [logo]             │  opcional: ícone porco/tarefa 48×48
│                            │
│         · · ·              │  indicador boot (estático, não animar)
└────────────────────────────┘
```

- Sem status bar.
- Refresh: **total**, uma vez.
- Transição: → T1 ou T7 (se provision) ou SAFE_OFF.

---

### 6.3 T1 — Home / Idle

**Objetivo:** relógio de bolso + resumo do dia + convite à gravação.

```text
┌────────────────────────────┐
│ 15:30  [wifi][batt]    ↑2  │
│────────────────────────────│
│                            │
│         15:30              │  display 36px
│       qua, 17 jun          │  subtitle 16px
│                            │
│   ✓ 12 hoje    ↑ 2 fila    │  métricas do Hub (best-effort)
│   24°C  55%                │  opcional se show_environment
│────────────────────────────│
│  segure ⏺ para gravar      │
└────────────────────────────┘
```

| Elemento | Fonte de dados | Refresh |
|---|---|---|
| Relógio grande | RTC (PCF85063) | Partial 1/min |
| "✓ N hoje" | `GET /v1/status` cache | Após sync |
| "↑ N fila" | `queue_pending_count()` | Após gravação/sync |
| Temp/umidade | SHTC3 | A cada wake se habilitado |

**Prioridade visual:** relógio > fila pendente > tarefas hoje > ambiente.

---

### 6.4 T2 — Recording ⚠️ CRÍTICA

**Objetivo:** deixar **inconfundível** que o mic está aberto. Esta tela salva o usuário de falar segredos sem querer.

```text
┌────────────────────────────┐
│ 15:31  [wifi][batt]        │
│────────────────────────────│
│                            │
│         [MIC 64px]         │
│        GRAVANDO            │  24px bold, sempre visível
│          0:07              │  contador mm:ss — partial refresh
│   ████████░░░░░░  07/120   │  barra progresso + limite
│────────────────────────────│
│  solte ⏺ para finalizar    │
└────────────────────────────┘
```

| Regra | Detalhe |
|---|---|
| Contraste máximo | Considerar fundo invertido (preto) só nesta tela — **decisão de design pendente** |
| Contador | Atualizar a cada 1 s via **partial** na zona B inferior |
| Barra | Largura útil 184 px; preenchimento proporcional a `elapsed/120s` |
| Latência | Tela pode aparecer **até 300 ms após REC** — primeiro frame pode ser mínimo (só "GRAVANDO") |
| Proibido | Wi-Fi, sync ou refresh total durante gravação |

---

### 6.5 T3 — Saved / Queued

**Objetivo:** confirmação imediata pós-gravação.

```text
┌────────────────────────────┐
│ 15:31  [wifi][batt]    ↑3  │
│────────────────────────────│
│                            │
│           ✓                │  hero 48px
│       Gravado!             │
│   na fila (↑3) ·  0:07     │  duração gravada
│                            │
│────────────────────────────│
│  sincroniza ao reconectar  │
└────────────────────────────┘
```

- Auto-transição: → T4 se rede disponível; senão → T1 após ~3 s.
- Beep duplo (áudio) **acompanha** esta tela — design visual deve reforçar sucesso.

---

### 6.6 T4 — Syncing

**Objetivo:** progresso de upload sem bloquear nova gravação (usuário pode gravar? **Sim**, após sair de SYNC — durante sync o device pode estar ocupado; mostrar estado honesto).

```text
┌────────────────────────────┐
│ 15:32  [wifi][batt]    ↑2  │
│────────────────────────────│
│                            │
│     Sincronizando          │
│        2 / 3               │  job atual / total pendente
│         · · ·              │  indicador estático ou 3 dots alternados lentamente
│                            │
│────────────────────────────│
│  segure ⏺ para gravar      │  se gravação permitida no estado |
└────────────────────────────┘
```

- Variante compacta (só zona B): `Sincronizando 2/3 ···`
- Mostrar nome truncado do job opcional (v2): `0:07 enviando…`

---

### 6.7 T5 — Last result

**Objetivo:** feedback do Hub após processamento — "sua fala virou tarefa".

```text
┌────────────────────────────┐
│ 15:33  [wifi][batt]    ↑0  │
│────────────────────────────│
│  Última tarefa:            │
│  "Criar apresentação BB"   │  max 2 linhas × ~22 chars
│                            │
│  Trabalho  ✓ alta conf     │  projeto + confiança
│────────────────────────────│
│  ⏺ gravar  ·  ▸ fila       │
└────────────────────────────┘
```

| Variante | Quando |
|---|---|
| Alta confiança | `✓ alta conf` + nome do projeto |
| Baixa confiança | `⚠ Inbox (revisar)` |
| Falha LLM | `⚠ revisar no Todoist` |

- Timeout → T1 (~8 s).

---

### 6.8 T6 — Queue / Pending

**Objetivo:** transparência offline-first — usuário vê o que ainda não subiu.

```text
┌────────────────────────────┐
│ 15:31  [wifi][batt]  ↑3 ⚠1 │
│────────────────────────────│
│  Fila: 3 pendentes         │
│────────────────────────────│
│  15:31 · 0:07 · enviando…  │
│  15:28 · 0:12 · na fila    │
│  15:10 · 0:05 · ⚠ falha 3x │
│────────────────────────────│
│  tap NAV → info            │
└────────────────────────────┘
```

| Estado do job | Rótulo |
|---|---|
| `queued` | na fila |
| `uploading` | enviando… |
| `uploaded` | processando… |
| `error` | ⚠ falha (Nx) |

- Max **3–4 linhas** visíveis; scroll não existe (v1) — mostrar os mais recentes.
- Tap NAV → T11.

---

### 6.9 T7 — Setup / Provision

**Objetivo:** instruções para portal cativo — legível no celular **e** no e-Paper.

```text
┌────────────────────────────┐
│       MODO SETUP           │
│────────────────────────────│
│  1. Wi-Fi no celular:      │
│     Taskhog-Setup          │
│     senha: taskhog123      │
│                            │
│  2. Abra no navegador:     │
│     192.168.4.1            │
│────────────────────────────│
│  aguardando configuração   │
└────────────────────────────┘
```

- SSID/senha vêm do firmware (NVS/defaults).
- IP sempre `192.168.4.1` (SoftAP).
- QR code: **fora de escopo v1** (200 px apertado; considerar v2).

---

### 6.10 T8 — Low battery

**Objetivo:** avisar sem alarmismo; **sempre** tranquilizar sobre dados locais.

```text
┌────────────────────────────┐
│ 15:30  [wifi][batt 8%]     │
│────────────────────────────│
│           🪫               │
│     Bateria crítica        │
│                            │
│  Conecte o USB para        │
│  sincronizar.              │
│                            │
│  ✓ Tudo salvo localmente   │
│────────────────────────────│
│  segure ⏺ para gravar      │  ainda permitido 3–10%
└────────────────────────────┘
```

---

### 6.11 T9 — Charging

```text
┌────────────────────────────┐
│ 15:30  [wifi] 🔋45% ⚡     │
│────────────────────────────│
│           ⚡               │
│      Carregando…           │
│        45%                 │
│                            │
│  Sincronizando fila…       │  se sync ativo
│────────────────────────────│
│  segure ⏺ para gravar      │
└────────────────────────────┘
```

- Pode ser **overlay** na T1 em vez de tela dedicada — decisão de implementação; conteúdo igual.

---

### 6.12 T10 — Error

**Objetivo:** explicar o problema + garantir que **nada foi perdido**.

```text
┌────────────────────────────┐
│ 15:30  [wifi][batt]    ↑2  │
│────────────────────────────│
│           ⚠                │
│     Erro de conexão        │  título por código
│                            │
│  ✓ Tudo salvo localmente   │  OBRIGATÓRIO
│  Tentará de novo depois.   │
│────────────────────────────│
│  segure ⏺ para gravar      │
└────────────────────────────┘
```

| Código | Título |
|---|---|
| `E_HUB_UNREACHABLE` | Erro de conexão |
| `E_HUB_AUTH` | Token inválido |
| `E_SD_MOUNT` | Erro no cartão |
| `E_AUDIO_INIT` | Erro de áudio |
| `E_UPLOAD_RETRY` | Falha no envio |

- Linha "Tudo salvo localmente" é **mandatória** em erros de rede/upload.
- Auto-dismiss ~5 s → T1, mantendo ícone ⚠ na fila se aplicável.

---

### 6.13 T11 — Info / Diagnóstico

**Objetivo:** suporte e power-user — sem expor token.

```text
┌────────────────────────────┐
│       INFORMAÇÕES          │
│────────────────────────────│
│  ID: taskhog-01             │
│  FW: 1.0.0                 │
│  🔋 78%  SD: 58 GB livre   │
│  Hub: taskhog.exemplo.com  │  host truncado
│  Último sync: 15:28 ✓      │
│  Hoje: 12  Fila: 2         │
│────────────────────────────│
│  tap NAV → voltar          │
└────────────────────────────┘
```

- **Nunca** mostrar `device_token` ou PSK Wi-Fi.

---

## 7. Navegação e inputs

### 7.1 Diagrama

```text
                    ┌─────────┐
                    │   T0    │ Boot
                    └────┬────┘
                         ▼
              ┌──────────────────────┐
              │  T1 Home (default)   │◄────────────────┐
              └──────────┬───────────┘                 │
           tap NAV       │ segura REC                  │ tap NAV
                ▼        ▼                            │
         ┌──────────┐  ┌──────────┐                    │
         │  T6 Fila │  │ T2 Grav. │                    │
         └────┬─────┘  └────┬─────┘                    │
      tap NAV  │           │ solta REC                 │
                ▼           ▼                           │
         ┌──────────┐  ┌──────────┐   auto            │
         │ T11 Info │  │ T3 Salvo │──► T4 Sync ──► T5 │
         └──────────┘  └──────────┘        │          │
                │                           └──────────┘
                └──── tap NAV → T1
```

### 7.2 Tabela de inputs

| Botão | Gesto | Ação global | Exceções |
|---|---|---|---|
| REC (BOOT) | Segurar | Inicia gravação → T2 | Bloqueado < shutdown%; ignorado se < anti-tap |
| REC | Soltar | Finaliza → T3 | Anti-toque < 0,5 s descarta |
| REC | Segurar no boot | Entra PROVISION → T7 | — |
| NAV (PWR) | Tap | Cicla T1→T6→T11→T1 | Ignorado durante T2 |
| USB | Plug | T9 overlay + sync se fila | — |

---

## 8. Estados compostos e prioridade de exibição

Quando múltiplos estados competem, aplicar esta **prioridade (maior vence)**:

1. `SAFE_OFF` — display off / mensagem mínima
2. `RECORDING` — T2 (nunca interromper)
3. `PROVISION` — T7
4. Erro crítico SD/áudio — T10
5. `SYNC` — T4
6. Bateria crítica — T8 overlay
7. USB charging — T9 overlay
8. `CONFIRM` — T3
9. Sub-telas T5/T6/T11
10. `IDLE` — T1

---

## 9. Copy e tom de voz (PT-BR)

| Situação | Fazer | Evitar |
|---|---|---|
| Sucesso | Curto: "Gravado!" | "Gravação concluída com sucesso" |
| Erro | Problema + "Tudo salvo localmente" | Códigos hex, stack traces |
| Offline | "sincroniza ao reconectar" | "Sem internet" alarmista |
| Ação | Imperativo: "segure ⏺" | "Você pode pressionar o botão…" |
| Confiança routing | "alta conf" / "revisar" | Percentuais decimais longos |

**Microcopy fixo do rodapé (T1):** `segure ⏺ para gravar`  
**Microcopy T2:** `solte ⏺ para finalizar`

Substituir ⏺ por sprite de botão redondo nos assets finais.

---

## 10. Exportação de assets para firmware

### 10.1 Formatos aceitos

| Tipo | Formato | Notas |
|---|---|---|
| Telas completas | PNG 200×200, 1-bit, sem alpha | Preto = `#000`, branco = `#FFF` |
| Sprites / ícones | PNG monocromático + `.h` C array | Preferir potência de 2 (16, 32, 48) |
| Fontes | Glyph bitmap `.bdf` ou array custom | Incluir ASCII + acentos PT-BR mínimos: `áéíóúãõç` |
| Wireframes | ASCII ou PNG preview | Para revisão humana |

### 10.2 Pipeline sugerido

```text
Design (Figma/Photoshop/AI)
    → PNG 200×200 1-bit (sem antialiasing)
    → opcional: Image2LCD / lcd-image-converter
    → array C `const uint8_t img_t2_recording[]`
    → components/ui/assets/
```

### 10.3 Nomenclatura de arquivos

```text
assets/
  screen_t0_splash.png
  screen_t1_home.png
  screen_t2_recording.png
  ...
  icon_wifi_on_12.png
  icon_battery_12.png
  font_display_36.png  # atlas opcional
```

---

## 11. Configuração UI (`device.json`)

| Campo | Default | Efeito visual |
|---|---|---|
| `ui.show_environment` | `true` | Linha temp/umidade na T1 |
| `ui.full_refresh_every` | `10` | Parciais antes de refresh total |
| `ui.timezone` | `America/Sao_Paulo` | Formato de data/hora |

---

## 12. Critérios de aceite (design)

- [ ] Todas as telas T0–T11 desenhadas em 200×200 1 bpp.
- [ ] T2 distinguível de T1 num relance (< 1 s de percepção).
- [ ] T10 sempre inclui "Tudo salvo localmente" para erros de rede/upload.
- [ ] Status bar alinhada e consistente entre telas.
- [ ] Textos cabem sem overflow em PT-BR nos piores casos (títulos longos truncados).
- [ ] Ícones legíveis em ~188 DPI (sem detalhe sub-pixel).
- [ ] Layout respeita zonas A/B/C para partial refresh.
- [ ] Nenhum asset depende de escala de cinza suave.
- [ ] Rodapé indica ação disponível quando REC está habilitado.

---

## 13. Prompts prontos para GPT / Claude

### 13.1 Criar uma tela

```text
Contexto: Taskhog, device e-Paper 200×200 1bpp preto/branco, sem touch.
Leia a spec em docs/design/epaper-ui-spec.md.

Tarefa: desenhe a tela T2 (Recording) em estilo minimalista monochrome.
Requisitos:
- Zona A status bar 24px, zona C rodapé 20px
- Ícone mic hero 64px, texto "GRAVANDO" bold
- Contador mm:ss e barra de progresso 0–120s
- Alto contraste; pixel-aligned; sem gradientes
Entregue: wireframe ASCII + lista de elementos com posições (x,y) + sugestão de sprites.
```

### 13.2 Criar set de ícones

```text
Crie um set de ícones monochrome 12×12 px para status bar e-Paper:
wifi_on, wifi_off, battery_full, battery_low, battery_critical, queue_up, charging.
Estilo: outline 1px, legível em 188 DPI.
Formato: descrição de cada glyph como grid ASCII 12×12 (# = preto, . = branco).
```

### 13.3 Exportar para C

```text
Converta este layout T1 Home 200×200 para um array C uint8_t framebuffer
no formato row-major MSB-first (1 bit = 1 pixel, 200×200/8 = 5000 bytes).
[anexar PNG ou descrição do layout]
```

### 13.4 Revisar contraste/UX

```text
Revise as telas T2 e T10 do Taskhog contra epaper-ui-spec.md.
Checklist: hierarquia visual, copy PT-BR, tranquilização em erro,
partial refresh zones, acessibilidade em display reflexivo.
Liste problemas e correções.
```

---

## 14. Referências cruzadas

| Tópico | Documento |
|---|---|
| **Wireframes PNG (T0–T11)** | [`wireframes/overview.png`](wireframes/overview.png) · [`wireframes/README.md`](wireframes/README.md) |
| Wireframes originais T1–T7 | Spec 01 §12.2 |
| Wireframes T8–T11 | PRD §15.3 |
| Máquina de estados | Spec 01 §4 |
| Gate de bateria | Spec 01 §7.3, PRD §9.4 |
| Botões REC/NAV | PRD §5.3, `docs/hardware/HARDWARE_NOTES.md` |
| Dados exibidos pós-sync | Spec 03 §5 (resposta Hub) |
| Implementação firmware | `taskhog-fw/components/ui/` |

---

## 15. Decisões de design em aberto

| # | Questão | Opções | Recomendação |
|---|---|---|---|
| D1 | T2 invertida (fundo preto)? | Sim / Não | **Resolvido: Não.** Fundo branco + mascote `angry` + mic, fiel ao mockup |
| D2 | T9 overlay vs tela cheia | Overlay na T1 / Tela dedicada | Overlay se couber sem poluir T1 (não implementado) |
| D3 | Logo T0 | Wordmark only / Ícone+mascote | **Resolvido: wordmark TASKHOG + mascote `default`** |
| D4 | Indicador sync animado | 3 dots alternados / estático | Estático (sem partial ainda); T4 mostra nuvem + nº pendente |
| D5 | QR no T7 | Sim / Não | **Não** v1 — pouco espaço |

---

*Este documento complementa a PRD e Spec 01. Mudanças de conteúdo exibido pós-sync devem alinhar com Spec 03 antes de alterar layouts.*
