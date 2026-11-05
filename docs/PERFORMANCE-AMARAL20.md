# amaral.20 — Automático dos caminhos rápidos do Turnip no Adreno 740

Base: `327d05b` (fonte reconstruída da amaral.19, ver `PERFORMANCE-AMARAL19-RECOVERY.md`).

## Mudança

Em *Compatibilidade do driver*, o **Automático** de "Memória do jogo na GPU" (`gpu_heap`) e "Leituras do alvo de
desenho no lugar" (`inplace_reads`) passa a ligar também no **Adreno 740**, igual ao LRZ (`vk_lrz_auto`). Antes ligava
só no Adreno 8xx. Os outros Adreno 7xx continuam desligados: um Adreno 750 desenhou textos e cores errados com esses
caminhos no LASRecomp. Ligado e Desligado continuam valendo do mesmo jeito.

Código: `vk_turnip_path_on` (`runtime/nvn/vulkan/device.c`) usa o mesmo conjunto de GPUs de `vk_lrz_auto`. Os textos
da tela de início e as chaves de tradução foram atualizados (pt-BR traduzido; nos outros idiomas a tradução antiga
ainda cita só "Adreno 8xx").

## Medição que motivou (Odin 2 Portal, Turnip Amaral 27.0.0-devel v5.8.17.1, 1.4.2, 60 fps, 1x, GSR 1, AA 2)

Mesmo trajeto, APK amaral.19, janela de diagnóstico de 60 s em gameplay:

| | Heap, in-place e LRZ desligados | Os três ligados (em Ligado) |
|---|---|---|
| FPS reais | 29,2 | **41,4** |
| Frame p50 / p95 | 31,0 / 56,8 ms | 21,0 / 44,0 ms |
| GPU por frame | 25,1 ms | **20,8 ms** |
| Render thread (CPU) | 21,9 ms | 16,0 ms |
| Upload por 2 s | 3,3–4,6 GB | 0,3–0,65 GB |
| Cópias de feedback | ~1000/s (~6,5 ms de GPU/frame) | 0 |
| SoC | 45–60 °C | 53–74 °C |

Nenhum erro gráfico visto pelo usuário. As travadas de 1–2 s no início da sessão com os três ligados foram
compilação de shaders (2,1–2,6 s de espera), esperadas na primeira vez após a troca do heap.

## Ainda falta para 60 fps

- GPU em 20,8 ms (precisa de ≤16,6): testar 0,8–0,9x de resolução interna ou a resolução dinâmica mirando 16,6 ms.
- Lock do kernel no core 0: 5–13 s de espera somada por janela de 30 s, ocupado 64% do tempo.
- Render thread: 24% das amostras em `libc+0xa5c40` (sem símbolo; simbolizar com `tools/diag_symbolize.py` e a
  libc do aparelho) e 7% em `clock_gettime`. A espera ociosa do render thread usa `pthread_cond_wait` (não é espera
  ativa).
- Temperatura mais alta com clocks maiores: em sessões longas, usar modo Desempenho com a ventoinha alta.

## Validação

Não compilado nem testado em aparelho neste commit. No Odin: limpar as três opções para Automático, abrir o jogo e
conferir no log `guest heap ... is GPU memory`, `sampled by it: in place (automatic)` e `Turnip LRZ on (automatic: ...)`.
