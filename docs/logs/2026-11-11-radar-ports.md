# 11/11/2026 — Radar de outros ports do TOTK

Alvo desta rodada: TOTK 1.4.2 no Odin 2 Portal (Adreno 740), com a amaral.21
como candidata. Foram consultados commits, diffs e notas de versão.
As propostas abaixo estão pendentes de teste no aparelho.

| Projeto | O que evoluiu | Decisão para o Totknspcxi |
| --- | --- | --- |
| **TOTKDroid** | A 0.2.2-beta corrigiu TXD e RomFS no local e trabalhou em heap para Mali, orçamento de memória/pipelines e limites de FPS. Os commits posteriores inspecionados tratam de documentação. | **Testar a .21 primeiro.** TXD e RomFS já entraram. Conferir água, superfície, draws pulados e retomada do RomFS no local. Orçamento de pipelines só entra na fila do Odin se houver evidência de evicção, pressão de memória ou recompilação repetida. [Release](https://github.com/mamcjr/TOTKDroid/releases/tag/v0.2.2-beta) · [Integração na .21](https://github.com/rickamaral94/Totknspcxi/blob/af600a7c4090a0e178b843bfe32df89f7de243ae/docs/UPSTREAM-2026-11-10.md) |
| **TotK Native Android** | O benchmark passou a invalidar o veredito quando a diferença de clock médio da GPU ponderado pela residência supera 3%. O frame lag protegido a 60 FPS melhorou throughput no Odin deles, com piora no p99 de uma panorâmica. Há também um experimento para fixar a hora do jogo. | **Aproveitar a metodologia.** Usar o mesmo save e marcar rodadas com clocks discrepantes. Comparar o `sync_lag` que já temos somente depois da linha de base, conferindo p99, piscadas e resposta aos controles. Manter a hora igual via save quando possível. O patch de relógio deles é 1.4.3 e altera a progressão do tempo. [Clock GPU](https://github.com/AurelioB/totk-android/commit/e532668f7e5ab814e59ee1d8c534a11e41855175) · [Frame lag](https://github.com/AurelioB/totk-android/commit/8e4ddf013c9aede99619ef6de251a58e243f72e6) · [Hora do jogo](https://github.com/AurelioB/totk-android/commit/2acbe97725906f6438078dbe56bb5899fa792f89) |
| **TotK PC Recomp 0.4.0** | Recorder paralelo, perfil PGO e tratamento de espera por pipelines. Também corrigiu a velocidade de escalada acima de 60 FPS nas versões 1.4.3 e 1.2.1. As notas registram falhas de inicialização ou device lost com o recorder em algumas GPUs AMD. | **Medir as esperas da nossa fila** com cache frio e quente antes de mudar os workers. Testar subida na parede e em escada a 30/60 FPS reais para validar a 1.4.2. Se houver divergência, localizar o caminho correspondente nesta versão antes de adaptar o patch. PGO fica condicionado a um perfil de Android coletado no Odin. [Release](https://github.com/valentderah/totk-pc-recomp/releases/tag/v0.4.0) · [Escalada](https://github.com/valentderah/totk-pc-recomp/commit/06811330e738e96f298cd290ca3868f817265fc2) · [PGO](https://github.com/valentderah/totk-pc-recomp/blob/7a37fc3c346cd6f2b3c80637effb27b880ce817b/docs/pgo.md) |
| **UltraTears** | Adicionou um teste opt-in para retirar `Invariant` dos shaders de vértice, com cache identificado separadamente. Também compactou módulos SPIR-V com zstd, de aproximadamente 3,4 para 0,9 GiB naquele pack. | **Investigar `Invariant` apenas em passe identificado.** Nosso tradutor usa posições invariantes e precisas para o pré-passe de profundidade e o teste `EQUAL`. Priorizar captura e inspeção de custo de shader. Uma variante experimental exige cache próprio e comparação de imagem por passe. Compactação fica para quando medirmos I/O, tamanho do cache e custo de descompressão no Android. [Invariant](https://github.com/kuraeow/ultratears/commit/6465cb396b0106ac91fd0c59da840422f7877809) · [Pack](https://github.com/kuraeow/ultratears/commit/363de1f0522cdc4e2f3140fbe644049b66262974) · [Nosso tradutor](https://github.com/rickamaral94/Totknspcxi/blob/af600a7c4090a0e178b843bfe32df89f7de243ae/runtime/nvn/maxwell/translate.c) |
| **nxnative** | Separou os saves usados pela automação da sincronização em nuvem. O renderer do projeto é NVN→Metal. | **Aproveitar o procedimento de teste.** Restaurar uma cópia do save para cada braço e manter o save pessoal separado. O renderer Metal serve de referência para comportamento do jogo. [Automação e saves](https://github.com/seedds/nxnative/commit/4b616bb54d1412f0c8009e923e0ed4dc5188a4d8) |

## Ordem de execução

1. **Correção visual e linha de base.** Rodar `.20 → .21 → .20`, framegen
   desligado, mesmo driver, cache aquecido e save restaurado. Registrar salto,
   saída da caverna e superfície. Anotar resolução interna efetiva, draws
   pulados, compilação, GPU/CPU por frame e p95/p99. Comparar uso de RomFS no
   local numa coleta própria de configuração e carregamento.
2. **Medir de onde vêm os quadros lentos.** Na mesma `.21`, repetir com cache
   frio e quente em séries separadas. Correlacionar stutter com espera de
   pipeline, draws pulados e uso de memória. Se os stutters persistirem com
   cache quente, correlacionar com custo GPU, espera de fila e thread de render.
3. **Isolar driver e opções gráficas.** Usar o
   [registro de Turnip](TURNIP.md) e testar um fator por vez. Usar escala fixa
   para a comparação. Medir resolução dinâmica separadamente, registrando
   transições de escala e estabilidade de imagem na saída da caverna.
4. **Timing do jogo.** Medir o tempo de subida, cooldown, bounce e cutscenes
   a 30/60 FPS reais, com o mesmo trecho de jogo. Anotar o FPS de fato
   sustentado durante cada execução. O teste acima de 60 FPS só é comparável
   onde essa faixa for sustentada.

## Como atualizar o radar

Registre a data, o SHA ou tag inspecionado, a mudança, o trecho equivalente
no fork e um teste com critério de sucesso. Ao concluir um teste, aponte o
registro no índice e indique se a decisão foi integrar, investigar ou adiar.
