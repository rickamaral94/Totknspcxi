# Amaral.11 — cache de vértices e invalidação

Base: 751cfa7e1be4c9f3535c758af0a55edff614b6c9 (.10).

## Evidência do Odin

No último log da .10: 30,00 FPS reais, p95 35,91 ms, p99 37,70 ms, render CPU própria 14,34 ms/quadro, SoC 42–46 °C, thermal status máximo 0. A .9 anterior registrou 30,00 FPS, p95 36,12 ms, render CPU 11,84 ms e 42–46 °C. As configurações e o driver coincidem, mas a carga difere: médias dos últimos 20 relatórios de aproximadamente 2 segundos são 80507 draws na .10 contra 62135 na .9. Não atribuir a diferença de CPU apenas ao cache.

Os últimos 20 intervalos da .10 mostram médias de 1553,40 MB alocados no ring; 1189,66 MB de vértices copiados; 77,22 MB de uniformes; 29,85 MB de constantes; 814,85 MB lógicos reutilizados por guest_copy. Vértices respondem por cerca de 77% do ring nesse trecho. São contagens lógicas de código, não medidas físicas de DRAM.

O cache de arena alterado na .10 movimentou só 1,57 MB por intervalo, sem hits nem evictions nesse trecho. Houve algumas reutilizações durante a entrada na cena, mas não há ganho sustentado demonstrado para esse ajuste. Não ampliar esse cache. A otimização de writeback da .9 continua ativa: aproximadamente 2057 MB lógicos evitados por intervalo nesse trecho; queue_full zero.

## Alteração

guest_copy atende vértices, uniformes e constantes. Antes havia uma entrada por posição de uma tabela de 4096 entradas. Agora são 1024 grupos de quatro alternativas, ainda 4096 entradas. O payload continua no ring; nenhuma segunda cópia dos vértices é mantida na RAM. Uma entrada com mesmo endereço, tamanho suficiente, submissão e geração de invalidação pode ser reutilizada conforme a política existente. Tamanho insuficiente cria outra cópia, preservando o snapshot já referenciado por draws. Nenhuma reutilização atravessa submissões.

As quatro alternativas são verificações de metadados, sem novo memcmp de payload. Um grupo cheio substitui sua primeira posição; não é LRU nem promete evitar toda colisão. A redistribuição dos grupos também pode piorar certos padrões, portanto o ganho líquido exige medição no Odin. Crescimento dos ranges e mudanças de submissão continuam provocando novas cópias.

Corrigido também o registro de índices ativos: a invalidação marca uma entrada como inválida, e o próximo preenchimento antes registrava novamente seu índice. Após repetição suficiente, a lista de tamanho fixo podia ficar tomada por índices duplicados; outros índices deixariam de ser registrados para invalidação. Agora há um marcador `tracked` por entrada, por época de submissão/geração. Cada índice entra no máximo uma vez; a invalidação ainda percorre as entradas ativas e testa sobreposição. Não há lista dinâmica nem aumento da capacidade do cache. São 32 KiB adicionais de metadados para os marcadores.

## Telemetria

`[vertex-cache-diag] guest_misses` cobre todos os tipos de guest_copy, apesar do nome da linha:

- `cold`: grupo com posição inválida disponível, sem a chave válida procurada. Inclui primeiro uso, troca de submissão/geração, invalidação e chaves anteriormente expulsas. Não permite separar essas causas.
- `grow`: mesma chave encontrada, mas o tamanho/política não permite servir a solicitação; com gcreuse habilitado significa crescimento.
- `collision`: grupo cheio de outras chaves válidas.
- `invalidated`: número de entradas efetivamente invalidadas por escrita sobreposta; não o número de chamadas de escrita.

Cada motivo mostra contagem e bytes copiados. Não são contadores separados apenas para vértices; usar junto com guest_vertices/uniforms/constants. Não há contador anterior comparável para provar que colisões dominavam. O objetivo da entrega é reduzir uma causa possível e medir as demais.

Preservados os caminhos de cópia GPU pendente, invalidações de comandos, writeback, barreiras, arena upstream, driver, LRZ e workers. Não ativar memória diretamente mapeada nem alterar Turnip nesta entrega.

## Validação e entrega

Teste extrai as funções reais guest_copy, guest_copy_invalidate e guest_copy_flush, com memória e fences simulados. Verifica quatro chaves que colidem no hash antigo, 4000 hits sem novas alocações, crescimento, política AB, escritas sobrepostas e nas bordas, preservação de snapshots, pressão de substituição, troca de submissão/geração e 5000 ciclos de invalidação. Confere que um índice adicionado depois dessa pressão continua sendo invalidado. Executado com ASan/UBSan; LeakSanitizer desativado por limitação de ptrace no ambiente.

Também passaram o teste da arena com 10000 mutações e as 1000 fixtures de ranges upstream. Compilação arm64 Release NDK r30. APK recompilado nativamente e reempacotado, versão 0.2.1-beta-amaral.11, versionCode 14; assinatura fixa e GAMELIB_ID 9588e7371082fde1 preservados. Não é build Gradle completo. Verificações de manifesto, ZIP, assinatura e demais membros da base preservados.

Ainda não há medição da .11 no aparelho. Comparar no mesmo save, posição/câmera e percurso da .10, pelo menos 60 segundos com framegen off e aquecimento semelhante. Julgar bytes de vértices por quadro/draw, custo de CPU, p95/p99 e ausência de novos erros gráficos; média de 30 FPS sozinha não distingue margem de desempenho.
