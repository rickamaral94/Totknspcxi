# Geração de quadros no BOTWRecompAndroid: avaliação (23/10/2026)

Pergunta: o framegen do LASRecompAndroid serve para o BOTW? Resumo: **serve como opção "30 → 60" para GPUs que não
seguram 60 (Adreno 7xx, Mali)**. Não é o caminho para o Snapdragon 8 Elite, onde a meta continua sendo 60 de verdade.
Nada foi portado ainda. Esta nota diz o que o porte exige.

## O que o framegen do LAS faz

`LASRecompAndroid/runtime/nvn/vulkan/framegen.c` (~950 linhas, MIT, do LASRecomp-Android-Plus de Nycolazs, que veio do
`reproj.cpp` do port de Wind Waker HD):

- O jogo roda a 30. Depois de cada quadro real, a GPU faz um quadro intermediário, apresentado uma atualização depois
  (FIFO).
- O intermediário é o quadro real **reprojetado com a profundidade da cena**: a câmera é avançada meio passo, com a
  view-projection do quadro atual e a do anterior.
- **HUD:** a imagem é copiada no momento em que a cena é composta na imagem final, antes do HUD. Os pixels em que a
  imagem final difere dessa cópia são HUD e ficam parados.
- **Link** fica onde o quadro real o tem (raio em volta da posição dele na tela). Os outros personagens se movem só a
  30 por segundo. A exceção é `LARECOMP_FRAMEGEN_FLOW=1`, que estima movimento por blocos e é experimental.
- **Overlays da janela** (controles de toque, contador de FPS) são desenhados de novo por cima do intermediário.

## O que o BOTW já tem e o que falta

| Entrada do framegen | No LAS | No BOTW hoje |
| --- | --- | --- |
| Momento da composição da cena (antes do HUD) | `scene_composite` em exec.c | **Existe**: `scene_upscale` já acha esse draw (FSR 1 / SGSR 1 da cena). |
| Profundidade da cena | `fg_note_draw` no passe principal | **Existe**: o passe do G-buffer (6 alvos + profundidade, 1600x900 ou a resolução dinâmica). Falta só anotar qual é. |
| View-projection da câmera, por quadro | `widescreen.c` lê o UBO da cena (320 bytes, matriz em +32) | **Falta**: `widescreen_scene_ubo` é um stub no BOTW. É preciso achar o buffer de constantes da câmera do AGL. Os parâmetros já foram vistos no heap (near 1, far 25000, fovy 0,8727, aspect 1,7778). |
| Posição do Link | `doorjump_link_pos` (Link's Awakening) | **Falta**: ler a posição do ator do jogador na 1.8.2 (`ksys::act::PlayerInfo`, há nomes no decomp zeldaret). |
| Ritmo 30 + 30 | o LAS roda a 30 | **Existe**: `framepace.c` fixa 30 e o relógio do jogo (VFR) acompanha. |

## Qualidade esperada no estilo do BOTW

- **Bom:** giro e deslocamento de câmera em campo aberto, o caso mais comum. A reprojeção com profundidade lida bem
  com isso, e a grama com alpha-test está no buffer de profundidade.
- **Fraco:**
  - Partículas, água, fumaça, neblina e vidro não estão na profundidade e "arrastam" junto com o que estiver atrás.
  - Bordas de objetos que se movem abrem buracos (disocclusion), preenchidos com o vizinho.
  - Inimigos e NPCs andando ficam a 30 (judder), como no LAS sem o modo flow.
  - Latência: cerca de +1 quadro (16,7 ms).
- O HUD do BOTW tem muitas animações (fôlego, coração, minimapa). A máscara por diferença já cobre isso no LAS.

## Custo

- O LAS mede um passe de warp na resolução de saída, mais um preparo e a máscara por quadro real. No BOTW a saída é
  2560x1440 no POCO.
- Estimativa: 1 a 2 ms de GPU por quadro real. Numa GPU que já faz 30 com folga (Adreno 7xx a 1x), cabe.
- No 8 Elite a 60 não ajuda: lá o problema é segurar 16,6 ms, e o framegen só entra quando o jogo roda a 30.

## Recomendação e passos

Vale portar como opção **Vídeo > Geração de quadros (30 → 60)**, desligada por padrão. Fica para depois do trabalho de
60 fps nativo no 8 Elite.

1. Achar o UBO da câmera:
   - Num quadro de debug, despejar os UBOs dos draws do G-buffer.
   - Procurar a matriz cujo produto bate com a projeção conhecida (aspect 1,7778, near 1, far 25000).
   - Ligar `widescreen_scene_ubo` a ela. Isso também ajuda o widescreen, que procura a mesma câmera.
2. Achar a posição do Link na 1.8.2. É uma leitura de memória por quadro.
3. Portar `framegen.c` e os ganchos em exec.c e device.c:
   - `fg_note_draw` no passe do G-buffer;
   - `fg_scene_composited` em `scene_upscale`;
   - em device.c, os quadros extras na apresentação.
4. Testar em campo aberto, em santuário (interior) e com chuva e partículas. Medir o custo com `LARECOMP_GPUPROF_AT`.

Esforço estimado: um a dois dias. A maior parte vai nos itens 1 e 2 e no teste visual.
