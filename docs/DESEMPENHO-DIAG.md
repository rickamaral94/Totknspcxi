# Diagnóstico de desempenho (Fase M)

O diagnóstico responde onde vai o tempo de cada quadro: em que threads do jogo, na GPU, e em que clocks e
temperaturas. É o primeiro passo antes de qualquer otimização de CPU (Fase C). Código: `runtime/diag.c`.

## Como usar

1. Na tela inicial, em **Configurações > Arquivos**, deixe **Salvar um arquivo de log** ligado e toque em **Medir desempenho (60 s)**.
   A aba Arquivos só existe na tela inicial.
2. Inicie o jogo e jogue normalmente. A gravação começa depois de **5 s seguidos de jogo**, fora de menus, pausa,
   eventos e filmes, e dura 60 s. Uma linha no canto da tela mostra a espera e quanto falta.
3. No fim, o relatório vai para o log, em linhas `[diag]`, e para `perf-diag.txt` na pasta de dados do app. Mande o
   `botwrecomp-log.txt`.

Por adb, `LARECOMP_DIAG=<segundos>` mede a partir do início da sessão. `LARECOMP_DIAG_TID=<id>[,<id>]` acrescenta
threads do jogo à amostragem.

Desligado, o diagnóstico custa um teste de flag em cada svc e em cada espera instrumentada. Nada é gravado e nenhum
timer existe.

## O que o relatório traz

### Quadros

O intervalo entre dois `vkQueuePresentKHR` na thread de render:
- média, p50, p95, p99 e máximo, só dos quadros de gameplay;
- a mesma estatística separada entre os blocos com o amostrador ligado e desligado. Ele alterna 5 s ligado e 5 s
  desligado, e a diferença entre os dois blocos mostra o custo dele;
- o tempo de `vkAcquireNextImageKHR` e o de `vkQueuePresentKHR`.

### GPU

- **Tempo ocupado por intervalo entre presents:** timestamps no início e no fim de cada submissão, sem o tempo ocioso
  entre elas (`gpu_busy_total`, em exec.c).
- **Composição final:** dois timestamps em volta de "a imagem do jogo na janela + menus/overlay" (device.c), lidos 16
  quadros depois.

### Aparelho, a cada 2 s

- frequência de cada cluster de CPU (mínima, média e máxima);
- temperatura do SoC;
- status térmico do Android;
- headroom.

O que o aparelho não fornece aparece como "n/a".

### Threads: tempo de parede em categorias exclusivas

Cada intervalo de tempo cai em **uma única** categoria. A categoria interna pausa a externa: uma svc dentro de uma
espera do host volta para a espera quando termina.

| Categoria | O que é |
| --- | --- |
| `run` | trabalho da própria thread: código do jogo e HLE/NVN do runtime. As amostras dizem qual |
| `svc` | dentro de uma svc que não bloqueia |
| `svc-block` | dentro de uma svc que bloqueia, por número: WaitSynchronization, ArbitrateLock, WaitProcessWideKeyAtomic, WaitForAddress… A CPU do próprio handler entra junto |
| `yield` | `SleepThread(0/-1/-2)` |
| `core0-wait` | esperando a vez no núcleo 0 (`core0_acquire`) |
| `host-wait` | esperas longas do runtime numa thread do jogo (`kernel_blocking_begin/_end`, o renderer) |
| `gpu-wait` | `vk_wait_seq` |
| `render-idle` / `render-present` | a thread de render sem trabalho / apresentando |
| `acquire` / `present-call` | as duas chamadas do swapchain |
| *não atribuído* | o tempo antes da primeira transição da thread na janela (por exemplo, uma thread bloqueada desde antes) |

- **CPU própria de cada thread:** lida **por ela mesma** (`CLOCK_THREAD_CPUTIME_ID`), na primeira transição e a
  cada 100 ms. Parede menos CPU menos as esperas é o tempo em que a thread estava pronta mas não rodou (preempção,
  migração).
- **Quem é quem:** o relatório diz se a thread que apresenta é a thread principal (handle 0x101). Pelos logs de
  outubro, não é: a t82 apresenta e espera a t80.
- **Custo dos ganchos:** o relatório estima o custo total pelo número de transições.

### Amostras por endereço

Amostradas: a thread principal, a que apresenta, a de render e as listadas em `LARECOMP_DIAG_TID`.
- **Como é medido:**
  - cada thread cria **o próprio timer** sobre o **próprio relógio de CPU**, com sinal para ela mesma
    (`SIGEV_THREAD_ID`);
  - o handler confere que está na thread certa. Se não estiver, a amostra é descartada e contada;
  - o pedido é uma amostra por 1 ms de CPU, mas o kernel entrega na granularidade do tick (4 ms com HZ 250, o comum no
    Android).
- **O que cada amostra guarda:**
  - **pc e lr como dois endereços soltos**, não como pilha. O lr é só "quem chamou por último";
  - a categoria atual da thread.
- **Contadores:** amostras mantidas, perdidas (anel cheio) e descartadas (thread errada), e o tempo gasto no handler.
- **Nomes:**
  - código do jogo vira `f_<endereço do jogo>`, pela tabela do gamelib;
  - o resto vira `módulo:símbolo` quando o módulo exporta o símbolo, senão `módulo+offset` em blocos de 64 bytes,
    com o build id do módulo listado no fim.
  - `tools/diag_symbolize.py log --lib <libmain.so sem strip da mesma build>` resolve esses offsets e soma por
    função. O script avisa se o build id não bate.
- **Desligado no código nativo:** com "Código do jogo: Nativo", o amostrador não liga (handler de sinal entre
  instruções do jogo reescritas não foi testado). As categorias continuam.

## Testes

- **No host:** `cc -DDIAG_TESTING -I runtime runtime/diag.c tools/diag_tests/test_diag.c -lpthread -ldl -lrt`, com
  ASan/UBSan. Cobre:
  - percentis;
  - contabilidade exclusiva com relógio falso: soma igual à janela, recorte nas bordas, pop sem push ignorado;
  - amostrador real com o timer na própria thread, sem amostras perdidas e sem amostras na thread errada;
  - nomes por módulo;
  - o fluxo inteiro do relatório.
- **No aparelho:** o relatório precisa mostrar as categorias somando a janela, as amostras perdidas e o custo do
  amostrador (ligado vs desligado).
