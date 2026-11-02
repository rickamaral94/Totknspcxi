# Amaral.13 — grade de apresentação do framegen

Base: 6dfb0484e05d9491d1bffe126d0beaee5fc26288 (.12). Objetivo: investigar a cadência dos 60 FPS exibidos mantendo 30 FPS reais.

## Evidência

Captura .12 de 600 segundos: 35593 apresentações, 35324 em gameplay; 29,97 FPS reais; intervalos efetivos da tela p50 16,71 ms, p95 25,06 ms, p99 25,09 ms, máximo 133,74 ms. SoC 40–71 °C, estado térmico Android máximo 0; checkpoints finais próximos de 55 °C e 30 FPS reais. O custo do FG permaneceu próximo de 5,85 ms por quadro real, sem crescimento sustentado demonstrado. Há 5 descartes informados nos relatórios FG, insuficientes para atribuir todos os intervalos longos a essa causa.

São indícios para investigar apresentação; não provam que o agendador seja a única causa. Conteúdo, compositor, trabalho GPU e sincronização também podem atrasar imagens. Não comparar diretamente tempos de CPU por apresentação com tempos por quadro real: com FG, o denominador inclui imagens geradas.

## Mudança isolada no caminho com presentation times

O código anterior calculava o primeiro horário desejado como `now + 2 ms`, sendo `now` lido antes da preparação e submissão do FG. Ao perder esse horário, a grade era ancorada de novo no instante da chegada do próximo quadro, podendo mudar a fase. Havia também um incremento do contador "dropped" ao reiniciar uma grade muito adiantada, mesmo sem descartar imagem.

Agora o relógio é lido no ponto em que os horários são definidos, depois de submeter a preparação/FG. Mantém-se a antecedência mínima existente de 2 ms; isso NÃO estima quando a GPU terminará. Um prazo perdido avança por um número inteiro de períodos de saída, preservando a fase da grade. Um próximo slot ainda válido, menos de dois períodos adiante do mínimo, é conservado. Pausas maiores que 250 ms ou uma grade excessivamente adiantada reiniciam a fase com o tempo atual. A ordem interpolados→real, número de imagens, FIFO e barreiras permanecem.

Esse ajuste pode adiar uma apresentação até o próximo slot em vez de pedir um horário intermediário imediatamente. Portanto há possível custo de latência, que deve ser avaliado junto da fluidez. Não é promessa de menor p95 nem de redução de GPU. O caminho sem presentation times permanece inalterado, assim como o algoritmo de interpolação, o driver, os caches e o diagnóstico de 10 minutos.

## Novos registros durante o diagnóstico

- `[fg-pacing] grid_skipped_slots`: posições temporais puladas ao recuperar um prazo; NÃO são imagens descartadas. `grid_resets`: início/reinício da grade.
- `[present-diag] actual_minus_requested_mean/max`: diferença entre o horário desejado não nulo e o efetivo retornados pelo histórico de apresentação, com contagem de imagens. Inclui o alinhamento normal à atualização da tela; um valor positivo não prova falha de prazo nem custo de shader. Os registros não separam imagens reais e geradas e cobrem as entradas retornadas pelo histórico, não necessariamente toda apresentação enviada.

Os relatórios são limitados a aproximadamente uma linha por dois segundos de dados. O resumo `screen` de 10 minutos continua sendo a referência de cadência. O contador FG `dropped` deixa de incluir reinício de grade sem descarte de imagem, logo não comparar esse contador isoladamente com versões anteriores.

## Validação

Teste da função real de escolha de slot para 60 e 120 Hz: horário pontual, igualdade da margem, atraso de um ou vários slots, pausa, fila adiantada e 600 segundos de chegadas com jitter. Confere fase, prazo mínimo e limite de fila. Compilação Android arm64 Release NDK r30 e verificação do APK/manifesto/assinatura. A simulação não modela Android compositor ou GPU e não comprova ganho no Odin.

APK 0.2.1-beta-amaral.13, versionCode 16, assinatura fixa e GAMELIB_ID 9588e7371082fde1 preservados. Compilação nativa e reempacotamento, sem build Gradle completo.

Teste no Odin com FG2x e configurações iguais à .12, diagnóstico de 10 minutos, cena/percurso e aquecimento semelhantes. Comparar p95/p99 na tela, FPS reais, quedas, novos registros e sensação de resposta. Caso a resposta piore ou o p95 não melhore, manter os diagnósticos e rever a política; não remover sincronização para esconder o atraso.
