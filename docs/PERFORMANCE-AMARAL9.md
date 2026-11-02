# Amaral.9: reduzir trabalho por quadro preservando a base sem piscadas

Base: e9c1a997c98ead586be2d11893263d6e623a5781 (amaral.8). Odin 2 Portal, Adreno 740.
O usuário confirmou que a integração upstream eliminou as piscadas. Não remover suas barreiras ou a política de conflitos CPU/GPU para ganhar FPS.

## Evidência disponível

Último log de 60 segundos: 1789 quadros de jogo, framegen desligado, limite de 30 FPS, 1x, LRZ desligado.

| Métrica | Amaral.8 |
|---|---:|
| FPS reais | 30,02 |
| Frame time p50 / p95 / p99 | 33,38 / 35,48 / 37,60 ms |
| SoC | 43–47 °C |
| GPU, média por apresentação | 12,85 ms |
| Render thread, CPU por quadro | 13,85 ms |
| Render thread, espera de trabalho | 13,50 ms |
| Espera de GPU | 4,55 ms |
| Uploads lógicos por 2 segundos | 1,35–1,38 GB |
| Writeback comparado por 2 segundos | 2,46–2,51 GB |
| amostras de CPU de render em memcmp | 19,48% |

CPU e GPU trabalham parcialmente em paralelo; esses tempos não devem ser somados como um orçamento sequencial. A memória do SoC é compartilhada: os contadores acima representam bytes lógicos processados, não uma medição física de largura de banda DRAM. Uma comparação satisfeita pelo cache também conta. O limite de 30 FPS produz espera intencional; aumentar clocks ou workers não é por si só uma melhoria.

## Alteração isolada

A fila de devolução GPU→CPU pode receber o mesmo intervalo mais de uma vez antes de uma submissão terminar. Amaral.9 elimina uma entrada repetida somente quando todos os campos coincidem: origem, endereço de destino, tamanho, sequência/fence, shadow e tabela de sequências de CPU.

- Não elimina entradas transitórias sem shadow.
- Não atravessa submissões/fences.
- Não elimina A,B,A se B sobrepuser o destino de A: preserva a ordem de escrita.
- O índice de busca é apenas uma dica; a entrada real é sempre validada, inclusive depois de compactação da fila.
- Mantém a capacidade de 256 entradas e a política original de não sobrescrever alterações mais novas da CPU.
- Não muda waits, feedback copies, LRZ, Turnip, workers, afinidade, boost ou framegen.

Novo log: `writeback coalesced`, `avoided_scan` e `queue_full`. Bytes evitados indicam extensão lógica que deixou de ser comparada; não significam banda física economizada nem garantem ganho de FPS. O trabalho extra para procurar sobreposição pode custar CPU se houver poucas duplicatas: é necessário medir no aparelho.

O log da arena também distingue `merge_in_pass_rejects`. Antes, uma recusa correta de mesclar buffers dentro de um render pass podia aparecer junto de falhas de espaço. Não aumentar arena às cegas com base naquele contador agregado.

## Comparação com BOTW e upstream

Revisados `rickamaral94/Botwnspxci` (9b5efc7e9fb74f4524da463aa1a3deec30a5827d), seu exec.c e documentação Turnip, além das alterações recentes de mamcjr/BOTWRecompAndroid. A fila do nosso BOTW conserva o append simples e não oferece uma implementação pronta dessa otimização.

O commit BOTW 14ba8c77a72b252692d5f7b04f03e08fa9fcdef1 desativou boost ADPF de workers por padrão após medir 26,32 FPS com boost e 27,33 sem, apesar de clocks maiores. São testes de outro jogo, não uma comparação causal para TOTK. Servem para rejeitar a premissa de que mais uso de CPU necessariamente ajuda.

As experiências BOTW com sysmem/GMEM, UBWC e UBO também não justificam trocar os padrões de TOTK sem medição própria. Não há ajuste obrigatório de Turnip nesta entrega. A correção upstream já integrada na .8 continua sendo a base.

## Validação e próxima decisão

Compilação Android arm64 Release com NDK r30. Teste da fila com ASan/UBSan, incluindo identidade, fence, capacidade, transitórios, sobreposição e 10000 ciclos de compactação; LeakSanitizer desligado por incompatibilidade do ambiente com ptrace. A política de writeback passou 5000 casos contra a implementação anterior. Passaram o teste de captura de efeitos e 1000 fixtures de montagem de ranges com tabela cheia.

APK gerado por recompilação nativa e reempacotamento dos recursos/Java existentes; não é uma recompilação Gradle completa. VersionCode 12, versão 0.2.1-beta-amaral.9, assinatura fixa anterior, GAMELIB_ID 9588e7371082fde1 preservado.

No Odin: manter as configurações da .8, framegen desligado, mesma cena e percurso por pelo menos 60 segundos após aquecimento. Comparar p95/p99, CPU de render, scans, temperaturas, novos contadores e ausência de piscadas. Repetir numa área pesada. Ainda não há resultado de desempenho da .9 no aparelho.

Próximas prioridades dependem desse resultado: reduzir ranges de upload comprovadamente redundantes; medir custo de tradução/recording por draw; só depois avaliar batching de passes/submissões com provas de dependências. Não retirar sincronização que corrigiu efeitos, nem ativar memória diretamente mapeada na Adreno 740 sem verificar suporte e coerência.
