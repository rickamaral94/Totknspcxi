# Amaral.10 — cache de cópias e atribuição dos uploads

Base: 0f5d895d47aef91b189daa0eea649a2271a6316f (amaral.9).

## Retorno do Odin

Logs recebidos em 02/11/2026: amaral.8 e .9, mesmo Turnip Amaral v5.8.17.1, LRZ off, framegen off, limite de 30 FPS, resolução 1x. As cenas não são comprovadamente idênticas.

| Métrica de 60 segundos | .8 | .9 |
|---|---:|---:|
| FPS reais | 30,02 | 30,00 |
| CPU própria do render por quadro | 13,85 ms | 11,84 ms |
| Present p95 | 35,48 ms | 36,12 ms |
| Present p99 | 37,60 ms | 37,63 ms |
| SoC | 43–47 °C | 42–46 °C |
| amostras em memcmp | 19,48% | 10,08% |

A .9 eliminou 212789 entradas e 26103,75 MB lógicos de comparações nos 43 intervalos registrados; queue_full foi zero em todos. Isso demonstra atividade da otimização, mas não isola sua contribuição ao ganho de CPU de 14,5%: uploads, draws e feedback copies também diferem. O p95 não melhorou. Não há erro fatal ou perda Vulkan nesses arquivos; a saída da sessão anterior foi solicitada pelo usuário.

## Uma mudança de comportamento

`ga_load_copy`, usado para ranges de arena somente de leitura, mantinha 64 posições com uma entrada por posição. Chaves diferentes que caíam na mesma posição se expulsavam mesmo quando ambas ainda eram válidas. A .10 usa 16 grupos com quatro alternativas cada, preservando as 64 entradas de metadados. Não mantém uma segunda cópia do payload na RAM.

Reutilização exige endereço e tamanho exatos, mesma submissão, mesma geração de invalidação e comparação integral dos bytes com a cópia imutável no ring. Uma mudança de conteúdo cria outra cópia; nenhuma cópia já referenciada por draw é alterada. Entradas obsoletas têm preferência para substituição. Falha de alocação preserva as entradas existentes. O custo da busca é limitado a quatro verificações de metadados; só uma chave coincidente provoca comparação de conteúdo.

Isso pode reduzir cópias por colisão. Não há contadores anteriores que provem sua frequência real no Odin; agrupamento diferente também pode causar colisões em outros padrões. O ganho líquido desta versão depende de medição no aparelho.

Permanecem intactos: writeback da .9, proteção de conflitos CPU/GPU, barreiras, política de leitura/escrita da arena, Turnip, LRZ, workers, boost, framegen e assets de recompilação do jogo.

## Nova telemetria por intervalo

`[upload-diag]` informa:

- `ring_alloc`: bytes alocados no ring temporário; inclui usos que não representam cópias CPU→GPU e não mede banda física.
- `guest_constants`, `guest_uniforms`, `guest_vertices`: bytes copiados por guest_copy, classificados pelo chamador.
- `guest_reused`: bytes lógicos servidos por cópia existente nesse caminho.
- `arena_load_copied`, `arena_load_reused`: cópias/reutilização dos ranges somente de leitura da arena.
- `arena_hits`, `arena_misses`: reutilizações e novas cópias bem-sucedidas.
- `arena_evictions`: substituição de entrada ainda válida com endereço diferente.

Esses grupos não formam uma partição completa do contador ring_alloc: há texturas, tabelas e outros caminhos, e a arena possui alocador próprio. Não somar reutilização como tráfego transferido. Contadores indicam bytes lógicos, não leitura de controladores de DRAM. Campos zeram após o relatório.

## Validação

Compilação Android arm64 Release com NDK r30. Teste extrai a função real de produção e usa alocação/fences simulados: quatro chaves que colidiam no cache anterior reutilizam as quatro cópias após 4000 acessos, sem novas alocações; mudança de conteúdo, submissão, geração e falha de alocação respeitam seus limites. Também passaram 10000 mutações com pressão de substituição sob ASan/UBSan e as 1000 fixtures de ranges upstream. Esses testes não substituem execução na GPU.

APK: 0.2.1-beta-amaral.10, versionCode 13, assinatura fixa anterior. Compilação nativa e reempacotamento dos recursos/Java existentes; AndroidManifest.xml tem seu string pool reconstruído para acomodar o nome maior. GAMELIB_ID continua 9588e7371082fde1. Validação inclui leitura do manifesto pelo aapt, integridade ZIP, assinatura e comparação dos demais membros do APK com a base.

No Odin, repetir mesma área/configuração e duração de pelo menos 60 segundos após aquecimento, framegen off. Enviar os dois logs e relatar se efeitos continuam corretos. Decidir o próximo ajuste pelos bytes de cada caminho, custo de CPU, p95/p99 e temperatura; o limite de 30 FPS esconde margem adicional na média de FPS.
