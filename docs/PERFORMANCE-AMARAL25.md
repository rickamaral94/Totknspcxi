# amaral.25 — upstream TOTKDroid 0.4.0 no fork Amaral

Base: amaral.24 (`2ee651b`) + `mamcjr/TOTKDroid` até `6ca0181` (26/11/2026, 0.4.0-beta e
seguintes). Pacote `com.totkrecompandroid`, versionCode 28, `0.4.0-beta-amaral.25`,
assinada com a chave da amaral.24 (certificado `0969fc07…79bf`, `SIGNING-AMARAL.md`):
**instala por cima da .24**.

## Como o merge foi feito

O Codex tinha integrado o upstream copiando o conteúdo (`e2751a2`, "through `6b785c2`"),
sem um merge de verdade. Com o ancestral comum antigo, o git via 40 arquivos e ~200 trechos
em conflito. O merge usou `6b785c2` como base (`git merge-tree --merge-base`), o que deixou
30 arquivos e ~95 trechos.

| Área | Decisão |
|---|---|
| Renderer (`exec.c`) | versão do upstream inteira, com os nossos ganchos reaplicados: diagnóstico de desempenho (`diag.c`: espera da GPU, perfil por passe, saúde dos timestamps e a correção de época da .17), diagnóstico de driver em cada pipeline (.3), último marcador de device lost (M7, .24), linha `[mem]` (.23) |
| Modo cópia (arena, gcopy, write-back) | o do upstream. Os experimentos do Codex (.5–.16: caches de 4 vias, coalescência de write-back, contadores `[arena-diag]`/`[upload-diag]`/`[vertex-cache-diag]`) saem: o Odin não usa o modo cópia desde a .20 (heap na GPU) |
| Fila de compilação de shaders | a do upstream (do BOTW: builds urgentes, roubo de job, drenagem no reset). Sai a reconstrução da .18/.19/.22 |
| Geração de quadros | **a do upstream** (`runtime/nvn/vulkan/lsfg/`, de Eden/Swap2-Native). Sai a nossa (`lsfg-vk-android`, máscara do HUD, 60→120). O submódulo `lsfg-vk-android` e o problema do `dxbc` saem junto. O Lossless.dll é importado de novo pelo menu do upstream |
| Automático da Compatibilidade do driver | heap na GPU e LRZ: Adreno 8xx **e 740** (nosso); leituras no lugar: Adreno 7xx/8xx e Mali (regra do upstream, que já inclui a 740) |
| 1.4.2 / 1.0.0 | mantidos (`GAME_V_142` nas regras de 60 fps do upstream, `PATCHES_142`, loader, launcher) |
| NSP/XCI, telemetria M1–M7, `[mem]`, "Mensagens do driver no log", aviso do cache de pipelines | mantidos |
| Saíram (desligados por padrão) | constant buffers no lugar (sync lag desligado) e a resolução dinâmica nossa (o upstream tem a dele; no BOTW, a 1x dinâmica não aliviou a GPU no Odin) |
| SDL3 | recompilado com o patch do upstream (`sdl-aaudio-resume.patch`: áudio que volta depois do segundo plano) |

## O que o upstream traz (de 0.2.2 a 0.4.0+)

Render thread: builds urgentes de shader, vertex input dinâmico, cache `.smeta` da tradução,
caches que crescem, gravação em thread de replay (opt-in), decodificação de textura em paralelo.
Uso: watchdog de travamento, "Ilimitado (até 120 fps)" e 20/40/45 fps, sombras desligáveis,
relação de aspecto esticada, economia de RAM, variáveis do driver na tela de início, ícones
de ação, mapa em tela larga, piso de clock da GPU (desligado). Muita coisa para Mali (heap
em hardware buffers, BC na CPU) que não afeta o Odin.

## Importante na atualização

- O ID do código do jogo mudou (`5e8124d5…` → `60ffdc74…`): na primeira abertura a 1.4.2 é
  **recompilada no aparelho** (demora).
- A geração de quadros precisa do Lossless.dll importado de novo (o caminho mudou).

## Validação

- Gradle `assembleRelease` assinado (lint vital pulado: download do Maven).
- `tools/test_translate_ops.sh` 36/0, `test_native_timing_fixes.py`, `test_romfs_build.py`,
  `test_android_log_policy.py` (atualizado para a opção da .23) passaram.
- `test_totk142_patches.py`: pendente (precisa do `main` 1.4.2 do jogador).
- Aparelho: pendente.
