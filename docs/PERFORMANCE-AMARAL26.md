# amaral.26 — o nosso de volta onde não era pior

Base: amaral.25 (`a5b6635`, upstream TOTKDroid 0.4.0+ no fork). versionCode 29,
`0.4.0-beta-amaral.26`, mesma assinatura da .24/.25 (certificado `0969fc07…79bf`).

Aplica a regra do Rick (27/11, `CLAUDE.md`): **só troca o nosso se o nosso for pior**. A .25
tinha trocado três coisas nossas pelas do upstream sem que as nossas fossem piores; a .26
devolve as três.

| Item | Na .25 | Na .26 | Motivo |
|---|---|---|---|
| Geração de quadros | a do upstream (`lsfg/`, de Eden/Swap2-Native) | **a nossa** (`framegen.c` + `lsfg-vk-android`, do BOTW) | a nossa tem a máscara do HUD (corações, mapa e textos nítidos nos quadros gerados); a do upstream interpola o HUD. Volta também o pacing com tempo de apresentação, 3x/4x e o 60→120 experimental |
| Constant buffers no lugar (opcional) | a opção existia mas não fazia nada | **funciona de novo** (`ubo_direct`, só sem sync lag) | não existe no upstream |
| Resolução dinâmica | a nossa já tinha sobrevivido ao merge; só perdeu os ganchos da geração de quadros | **ganchos de volta** (alvo na taxa base, GPU da geração descontada) | — |

Ficam do upstream, por serem melhores ou por não haver equivalente nosso: a fila de
compilação de shaders (builds urgentes, medida no BOTW), o modo cópia, o watchdog, o
"Ilimitado" e o resto da 0.4.0. A janela por desenho dos constant buffers (`cbwin_bind`) é
a do upstream: ligada sempre que o heap está na GPU (a nossa só ligava com a opção).

## Detalhes

- O pedido de taxa da tela para a geração de quadros (`vk_set_frame_rate`) passa pelo
  `android_frame_rate` do upstream, que agora decide a taxa da janela a cada quadro: os
  dois não brigam mais.
- O overlay de desempenho do upstream lê o estado da nossa geração por `vk_fg_state()`.
- `NOTICE.md`: sem a pasta GPL do upstream, o APK volta a ser MIT.
- O `dxbc` (dependência da tradução do Lossless.dll) tem a origem fora do ar: cópia
  verificada em `third_party/vendor/dxbc-78ab59a` (árvore `f19b5a7a…`, a mesma do BOTW),
  usada pelo workflow de release.

## Atualização

- Instala por cima da .24 ou da .25.
- Importe o Lossless.dll pelo menu (Vídeo > Geração de quadros > Lossless.dll). Quem vem
  da .24 já tem os shaders traduzidos.
- O código do jogo 1.4.2 é recompilado na primeira abertura se vier da .24 (o gerador mudou
  na .25).

## Validação

- `assembleRelease` assinado: certificado conferido, versionCode 29, `liblsfg_framegen.so`
  presente; strings da máscara do HUD, dos constant buffers e da telemetria em `libmain.so`.
- Host: `test_translate_ops.sh` 36/0, `test_native_timing_fixes.py`,
  `test_android_log_policy.py`, `test_native_fg_policy.py`, `test_fg_pacing.c`.
- Aparelho: pendente.
