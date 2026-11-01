# kuraeow/ultratears - o que serve para nós (2026-10-30)

Port nativo do TOTK 1.4.3 para Windows (recompilação estática ARM64 -> C -> x86-64, NVN em Vulkan), versão 0.1.0 de
2026-10-25, 3 commits. O NVN vem do MacNativeGames (o mesmo `nvn_mtl.h`); a camada Vulkan (`src/gfx/vkb_*.c`,
`backend_vk.c`) foi escrita do zero. As texturas vêm do LASRecomp. Joga até a Grande Ilha Celestial a 30 fps numa
RTX 3060. O que eles acharam vem anotado no código com a data, e é isso que segue.

## Correção de imagem - os mais promissores para os nossos bugs abertos

1. **Depth em float para o teste EQUAL da iluminação** (`vkb_tex.c`). O TOTK classifica os tiles de luz assim: um
   passe escreve `gl_FragDepth` = id do material, e os passes de luz desenham quads nesse mesmo z com teste EQUAL. Num
   depth UNORM os dois valores chegam ao buffer por conversões diferentes, que numa GPU que não é Maxwell podem
   arredondar um degrau diferente. Lá os tiles falhavam e **o mundo ficava sem luz** (D16, 2026-10-23). Eles guardam
   D16 e D24 como D32F.
   - Nosso "paredes só com névoa" é exatamente isto na aparência: a iluminação diferida em 11 passes por material e o
     buffer de luz zerado. Nós usamos D16_UNORM e, no Turnip, D24_UNORM_S8.
   - **Pronto para testar:** `LARECOMP_VK_D16_AS_D32=1` (D16 como D32_SFLOAT, desligado por padrão) e
     `LARECOMP_VK_D24=0` (D24S8 como D32S8, já existia).
2. **Uniforms copiados no submit** (`vkb_ucap.c`). No console a GPU lê as constantes logo depois de
   `nvnQueueSubmitCommands`, e o TOTK reconstrói os pools de constantes por objeto e por quadro em outras threads logo
   em seguida, **sem esperar cerca**. Um tradutor que lê depois (thread de tradução) vê os do quadro seguinte ou zeros:
   - os parâmetros de luz (c13 dos shaders de iluminação) liam zero quadro sim, quadro não;
   - num corte de câmera o cenário ficava preto por 4-6 quadros;
   - malhas distantes eram desenhadas na frente da câmera.

   A solução deles: no submit (thread do jogo), copiar as faixas de uniform que a submissão liga, e a tradução lê a
   cópia. Uma escrita da GPU na faixa no meio da submissão derruba a cópia dali em diante.
   - A nossa thread de render (`async.c`) parte do contrato contrário ("o jogo só reescreve depois de esperar uma
     cerca"), e `guest_copy` lê os uniforms quando traduz, até um quadro depois. O sync lag deixa o jogo correr mais à
     frente.
   - Candidato para: paredes sem luz, cenário sumindo nas cutscenes, o flicker escuro. **Teste barato:**
     `LARECOMP_VK_ASYNC=0` (traduz no submit). Se a imagem consertar, portar a cópia no submit.
3. **Clears condicionais** (`backend_vk.c:789`). Repetir todo clear condicionado a contador limpava os alvos de luz e
   de histórico em quadros que o jogo não pediu: a luz do menu e do mundo ficava preta por alguns quadros depois de
   cortes de câmera. Eles olham primeiro o resultado do contador.
4. **Imagem nova no mesmo endereço com outro tamanho** (`vkb_tex.c:255`). Ela herda o conteúdo da última escrita no
   mesmo endereço e formato, escalado, como fazem os caches de textura dos emuladores. Sem isso a luz do menu lia
   alvos de outro plano e a cena escurecia 4-6 quadros. Candidato para o nosso flicker escuro na troca de resolução
   dinâmica.
5. **O depth 192x192 D16 de cada corte de câmera** (sondas de ambiente). Vazio, a sonda e a cena ficavam escuras.

## Desempenho

- **Descriptor buffers** (`VK_EXT_descriptor_buffer`, `vkb_desc.c`). Com ~6000 draws por quadro, alocar e escrever um
  descriptor set por draw era ~45% da thread de tradução no driver. Nós temos cache de sets (311 mil acertos, 42 mil
  falhas por janela), e o Turnip ficou com 19% da nossa thread de render. O Turnip tem a extensão. É grande, mas é o
  maior corte de CPU do driver que eles mediram.
- **Shader objects** (`VK_EXT_shader_object`): nenhum pipeline para montar no draw, todo estado dinâmico, e shaders
  criados adiantados a partir de uma lista dos usados (`used.txt`, ~17% da gravação entrando no mundo). Atacaria os
  nossos "draw skipped (shader still building)". Falta ver se o Turnip V37 expõe a extensão.
- **Gravação em várias threads** (`vkb_mt.c`). Uma passada em ordem pelo estado, depois partes da submissão gravadas
  em paralelo (6000 draws = ~25 ms numa thread). Não medido aqui.
- **Medidas que eles descartaram:**
  - dividir a submissão (`TOTK_VK_SPLIT`): sem ganho;
  - repor todo o estado a cada draw: o driver ficava com ~60% da gravação, então pular o estado inalterado vale (nós
    já pulamos: `ss_need`).
- O hash de shader com memo de um só caiu de ~20% da gravação quando lembraram mais de um: o TOTK troca de programa
  centenas de vezes por quadro. Conferir o nosso.

## O que não serve aqui

- ASTC -> BC7 no build (desktop sem ASTC; o Adreno tem ASTC).
- Espelhos de memória para atomics através do PCIe.
- Write-watch do Windows: no Android já temos o rastreio por `mprotect` (`plat_written_pages`), ligado só sem heap
  na GPU.
- Os pools virtuais (MapVirtual) por tabela de páginas.

## Testes no celular (2026-10-30): nenhum muda as paredes

Testado na caverna do prólogo: um save do usuário de frente para as paredes, uma cópia feita pelo app, sem entrada,
~75 s por lançamento (`android/out/walls-*`, `walls-compare.png`). As paredes continuam só com névoa nos quatro, com a
mesma imagem:

- **base:** 30,0 fps;
- **D16 em D32F:** 29,9 fps;
- **D24 em D32F:** 30,0 fps. O jogo nem pede D24S8 ali (nenhuma linha `depth24 stencil8`);
- **`ASYNC=0`:** 19,2 fps. Tradução no submit, sem a corrida dos uniforms.

Ficam descartados como causa: a profundidade UNORM e a leitura atrasada dos uniforms. Restam desta lista os clears
condicionais e a herança de conteúdo entre imagens no mesmo endereço.

### O que o dump da caverna mostra (2026-10-30)

Dump do quadro (`fogdump`, `android/out/fogdump.log` e `fogdump-dbg/`, com o GLSL dos shaders) confirma o mecanismo
que o ultratears descreve:
- o classificador `13fe41b7645b246c` (draw 1533) escreve `gl_FragDepth` = material · (1 − k) / 255 + k num depth D16
  (`ds 010108`: escrita, ALWAYS, sem cor);
- os 11 passes de luz (1534-1544) desenham quads com z = 2·r11 − 1 (a mesma fórmula, cb4[147].z) e testam EQUAL
  (`ds 010003`), somando em `@35b9d8200`.

Testado nas paredes, sem mudança nenhuma:
- `LARECOMP_VK_EQUAL_TOL=1`, que troca EQUAL por LEQUAL com viés, com e sem D16 em D32F. Só a luz do Link muda (fica
  manchada), as paredes seguem névoa. **O teste de profundidade não é o que apaga as paredes:** os passes rodam nesses
  pixels e somam zero.
- O mesmo alvo nos slots 2 e 3 dos draws de 8 alvos que escrevem as máscaras de 800x448 (`@35dd67e00`). Testado
  manter o slot conforme o que o shader escreve, e manter o último: nada muda. Desfeito.

O próximo passo é seguir um pixel de parede pelas entradas de um passe de luz (o rastreador de pixels do
`GLITCH-PITCH`). As máscaras de 800x448 escritas pelos draws de 8 alvos são os primeiros suspeitos: fs hashes
`41e26c95606e5dcb`, `532c7c6d5353bb90`, `fe5271974c9c07d2`, `7f6c09fa5e64a19b`, `730a9d705e47b56f`, `e3726562d6267f37`,
`fe2af74db736e61b`, `c5954fa604f5e656`, `4aaf92234cacc933`. Depois a sonda de ambiente e as sombras.

Os comandos testados:

```
LARECOMP_VK_D16_AS_D32=1
LARECOMP_VK_D24=0
LARECOMP_VK_D16_AS_D32=1,LARECOMP_VK_D24=0
LARECOMP_VK_ASYNC=0
```
