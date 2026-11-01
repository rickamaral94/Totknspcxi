# Paredes e FPS: o que Eden, Yuzu e Sudachi relatam, e onde estamos (2026-10-30)

## Os repositórios

- **Eden** (`git.eden-emu.dev/eden-emu/eden`): o histórico inclui o do Yuzu inteiro (29 mil commits). Lidos os
  commits e as issues que citam o TOTK.
- **Yuzu**: o espelho no GitHub está fechado (403). O histórico dele está dentro do Eden.
- **Sudachi**: `github.com/sudachi-emu/sudachi` não existe mais. É um fork do Yuzu.
- **"Nyushu"**: emulador de Android fechado, sem código nem tracker público. Nada a ler.

## Paredes só com névoa

Nenhum dos trackers relata paredes de caverna só com névoa. O que chega perto:

| Commit / issue | O que é | Aqui |
|---|---|---|
| Eden `a95efaf18` (2026-10-26) "Fix TOTK dark squares" | O Turnip e o driver Qualcomm arredondam as coordenadas do `textureGather` de outro jeito que a Maxwell: quadrados pretos. Liga para eles o deslocamento de subpixel que o Yuzu já usava na AMD (`45fb154f0`). | Já temos (`c02a43f`, gathers arredondados a 1/256 de texel). |
| Yuzu `2e478a66c` (2023-05) "Fix TOTK flickering clouds and depths" | O TMML devolve as duas componentes como `F2U` << 8, e o `VertexId` sem tirar o `base_vertex`. | Nosso TMML usa `int()`, igual exceto para LOD negativo. O VertexId é `gl_VertexIndex`, como o fix. |
| Eden #4371 "EDS3 Depth Clamp" (aberta) | O céu do TOTK quebra com o depth clamp ligado (RADV, NVIDIA). | Seguimos o `nvnCommandBufferSetDepthClamp` do jogo. |
| Yuzu `5d1961ad6` "Masked depthstencil clears", `dd2d450e3` "depth test on stencil clear path" | Clears de depth/stencil com máscara. | A conferir: os passes de luz da caverna usam stencil. |
| Eden `ba608d2b5` / `a27d35463` | Pisca-pisca dentro dos santuários, por sincronização CPU/GPU. | Outro sintoma. |

O dump da caverna (ver `ULTRATEARS-2026-10-30.md`) já descartou o formato da profundidade, o teste EQUAL, a tradução no
submit e os slots duplicados: os passes de luz rodam nos pixels da parede e somam zero.

## FPS: o que os emuladores fazem no Android

- Os relatos são de ~30 fps estáveis, não mais, e **com mods de desempenho** (TOTK Optimizer / UltraCam), por exemplo
  num Snapdragon 8 Gen 1. Os mods reduzem a sombra (1024), a distância de render (o UltraCam fala em até 20% de FPS
  com distância 1000, com mais névoa) e a resolução, e desligam a resolução dinâmica e o FSR. O UltraCam suporta até a
  1.4.2, não a 1.4.3.
- O Eden mediu no TOTK: a fila de comandos sem contenção (até +10 fps em alguns jogos), "Fast UBO" e o *throttling*
  de flushes em voo (só para corrigir imagem).
- O Yuzu de Android de 2023: menos despachos de draw (`445a1f1b1`), apresentação assíncrona, mais quadros em voo.

## Onde estamos (POCO F7 Ultra, caverna do prólogo, 2026-10-30)

O trace de quadros (`LARECOMP_FRAME_TIMELINE=2000`, `android/out/tl-cave.txt`) mostra, por quadro de ~33 ms:
- **jogo:** 1,2 ms de trabalho na thread que apresenta. O resto é espera: os workers dele e o vsync emulado. Ele pede
  30 fps (intervalo 2) na caverna.
- **thread de render:** traduz a lista em 13-17 ms e fica ~17 ms parada.
- **GPU:** 18-22 ms por quadro. Termina ~22 ms depois de o jogo mandar a lista, então render e GPU se sobrepõem só em
  parte.

Com o alvo de 60 (`frame_rate = 60`), o jogo fica nos mesmos ~27 fps. A média abaixo de 30 vem dos quadros que passam
de 33 ms e esperam o vsync seguinte (50 ms): picos de compilação de shader, upload de textura e as falhas de ritmo.

**Modo portátil** (`LARECOMP_HANDHELD=1`, a cena em 1280x720 em vez de 1600x896): a GPU cai de 22,5 para 19,6 ms
por quadro, mas o FPS fica igual (27 contra 27), porque a thread de render e a GPU estão em série.

## O que daria mais FPS (em ordem)

1. **Estabilizar os 30:** cortar os picos que levam um quadro a 50 ms. Shaders compilados antes (o cache de pipeline
   já existe; falta pré-compilar como o ultratears faz com o `used.txt`) e os uploads de textura fora da thread de
   render. Isso é o "mais estável" que o emulador mostra.
2. **CPU da thread de render (13-17 ms → menos de 8):** descriptor buffers (o ultratears mediu o driver em ~45% da
   gravação), o hash de textura completo (~10%) e as cópias de vértice do alias (o `vamm`, ainda sem A/B).
3. **GPU (18-22 ms → menos de 16):** o modo portátil (−13%), a resolução da sombra e a distância de render, como os mods
   (patches no código do jogo, que precisam ser achados na 1.4.3), e o upscaler.
4. **Sobrepor render e GPU:** com os dois abaixo de 16 ms e em paralelo, dá para os 60.

Fontes:
- [Eden: commits](https://git.eden-emu.dev/eden-emu/eden), [issue #4371](https://git.eden-emu.dev/eden-emu/eden/issues/4371)
- [Notas das versões do Eden (v0.0.4-rc3)](https://newreleases.io/project/github/eden-emulator/Releases/release/v0.0.4-rc3)
- [TOTK-mods / UltraCam](https://github.com/MaxLastBreath/TOTK-mods), [TOTK Optimizer](https://github.com/HolographicWings/TOTK-Mods-collection/releases/latest/)
- [Relato de TOTK a 30 fps no Android com mods](https://reddit.birdcat.cafe/r/EmulationOnAndroid/comments/1kuw2w2/zelda_tears_of_the_kingdom_perfectly_playable_on)
- [Sudachi (repositório removido)](https://github.com/sudachi-emu/sudachi/)
