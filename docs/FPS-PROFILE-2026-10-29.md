# Perfil e experimentos de FPS — 2026-10-29

## Ambiente e método

Celular `192.168.1.6:40089`, modelo `24122RKC7G`, Adreno 830 / Turnip. Mesma cena estática na saída da caverna da Grande Ilha Celestial, código nativo, resolução 50%, taxa Auto e sync lag ligado. Cada execução iniciou com uma cópia isolada do save; o progresso original não foi alterado.

O perfil `LARECOMP_VK_PROF=1` mede trabalho da thread de render. Experimentos alternaram o caminho antigo (0) e o candidato (1) a cada 240 quadros com `LARECOMP_AB`. Foram excluídos carregamento (primeiros 55 segundos), intervalos com menos de 100 mil draws e amostras atravessando uma troca de bloco. A média de FPS é das amostras do log, não um benchmark de toda a sessão. Clocks, temperatura e caches não foram fixados; ganhos pequenos não permitem afirmar aumento consistente de FPS. A instrumentação também tem custo.

## Gargalo observado

Em uma amostra estabilizada de referência: 158.847 draws em cerca de 2 segundos / 40 quadros; CPU de tradução 1.255 ms, preparação de descritores 323 ms, constantes 141 ms, vértices 205 ms. Isso corresponde a aproximadamente 31 ms de tradução por quadro, incluindo 8 ms em descritores e 5 ms em vértices. A escrita dos descritores no driver consumiu só 24 ms nesse intervalo (~0,6 ms/quadro): descriptor update templates não são a primeira prioridade deste perfil.

A heap principal já está colocada em memória Vulkan e os shaders a alcançam por ponteiro. Porém, cópias grandes foram identificadas em endereços como `0x254ade028` (2.001.840 bytes), `0x254cc6bd8` (2.669.120 bytes) e `0x2553ec088` (1.108.216 bytes). Estão na região de alias / memória virtual, abaixo de `HEAP_BASE=0x340000000`, e não são atendidas pelo caminho direto da heap. O cache com rastreamento de páginas está desativado porque o sistema não fornece esse rastreamento. O contador de cópias chegou a aproximadamente 2 GB a cada 2 segundos.

## Comparações A/B

Cada linha usa uma execução independente; compare apenas as duas condições da mesma linha.

| Experimento | Amostras antigo/novo | FPS antigo/novo | Tradução CPU ms/quadro antigo/novo | Upload MB/quadro antigo/novo |
| --- | --- | --- | --- | --- |
| Constantes do driver limitadas ao tamanho lido | 11 / 10 | 19,29 / 19,36 | 32,62 / 31,95 | 59,20 / 58,95 |
| Bloco de zeros compartilhado | 13 / 15 | 21,23 / 20,41 | 30,18 / 31,29 | 58,59 / 58,67 |
| Reutilização de prefixos nas cópias | 12 / 14 | 20,00 / 19,99 | 32,60 / 32,41 | 58,27 / 57,74 |
| Snapshots persistentes de vértices, comparados byte a byte | 23 / 23 | 20,95 / 20,93 | 30,25 / 31,32 | 59,06 / 49,69 |

Nenhum experimento demonstrou aumento consistente de FPS. O snapshot reduziu bytes copiados, mas aumentou o custo de CPU e não melhorou o resultado.

## Alteração mantida

O caminho de constantes do driver inicializa somente o tamanho que será vinculado ao shader e evita construir tabelas fora desse intervalo. Shaders gráficos cujo cb0 termina antes da tabela usam diretamente o bloco de zeros. O conteúdo entregue ao shader permanece idêntico.

`tools/test_driver_constants.py` extrai a implementação real e compara com a anterior: **24.600 comparações byte a byte passaram no celular**, incluindo tamanhos parciais, gráficos, compute e caminho antigo. O APK final compilou com sucesso. `LARECOMP_VK_DRIVER_CONSTANTS=0` permite desativar a mudança; `LARECOMP_AB=240:drvconst` reproduz a alternância.

Essa mudança teve uma pequena redução medida de CPU, ainda sem ganho relevante de FPS. Os outros três candidatos e seus testes experimentais foram removidos. O diagnóstico opcional `LARECOMP_LOG_GUEST_COPY=1` foi mantido: imprime até 32 cópias de pelo menos 64 KB.

## Próxima prioridade

Investigar um caminho de memória Vulkan para os buffers da região de `MapPhysicalMemory`, antes das primeiras escritas do jogo, evitando o upload repetido. Não remapear memória já usada sem preservar dados e coordenar todas as threads; preservar páginas já mapeadas, escritas GPU/CPU, unmap/reutilização de regiões e reinício do runtime. Limitar o compromisso de RAM e manter fallback em drivers sem suporte.

Depois disso, repetir medições sem profiler, com aquecimento e blocos alternados, validar movimento/câmera e conferir os artefatos visuais já existentes. Os testes desta rodada foram estáticos e não cobrem toda a jogabilidade.

Evidências locais em `build/fps-opt-20261029/`: `baseline.log`, `ab.log`, `zero-ab.log`, `prefix-ab.log`, `snapshot-ab.log`, `copy-diagnostic.log` e `summary.json`.
