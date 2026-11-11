# Logs de testes do TOTK no Android

Histórico compartilhado por Rick, Claude e Codex. Cada registro guarda o que
foi testado, o resultado, a conclusão sustentada pela evidência e o próximo
passo. Os relatórios detalhados de cada build continuam em `docs/`.

## Histórico

| Registro | Escopo | Resultado e situação |
| --- | --- | --- |
| [Histórico inicial do Odin 2](2026-11-historico-odin2.md#piscadas-e-frame-generation) | Piscadas, sync lag e frame generation | As piscadas voltaram após a saída da primeira caverna. A causa permanece em aberto. |
| [Histórico inicial do Odin 2](2026-11-historico-odin2.md#amaral14-a-amaral19) | `.14` a `.19`, TOTK 1.4.2 | FPS e CPU observados na `.16`; prioridade de shaders e dois workers ainda sem comparação controlada registrada. |
| [Histórico inicial do Odin 2](2026-11-historico-odin2.md#amaral20) | Sessões na `.19` que motivaram a `.20` | 29,2 e 41,4 FPS reais em duas sessões com três opções alteradas juntas. |
| [Histórico inicial do Odin 2](2026-11-historico-odin2.md#amaral21) | `.21`, upstream e captura do log do driver | Build e testes no host passaram. Falta validação no Odin 2. |

| [Relatório da .22](https://github.com/rickamaral94/Totknspcxi/blob/c3cca75e956eceb984adc5619d0e6663bde72961/docs/PERFORMANCE-AMARAL22.md) | Fila Vulkan, cache e clock GPU | APK ARM64 assinado com a chave fixa e validado no host. A/B/A no Odin pendente. |

## Pesquisa de outros ports e Turnip

- [Radar de ports — 11/11/2026](2026-11-11-radar-ports.md) reúne commits
  avaliados, o que já temos no fork e os próximos testes por prioridade.
- [Turnip no TOTK](TURNIP.md) concentra hipóteses, medições e critérios de
  encaminhamento ao Amaral Adreno Tools. Consulte este tópico quando o driver
  receber uma candidata ou surgir uma regressão reproduzível.

## Próxima rodada no Odin 2 Portal

1. Priorizar `.21 → .22 → .21` para medir a fila e o cache. Seguir o
   [roteiro da .22](https://github.com/rickamaral94/Totknspcxi/blob/c3cca75e956eceb984adc5619d0e6663bde72961/docs/PERFORMANCE-AMARAL22.md) e separar cache frio de cache quente.
2. Comparar `.20 → .21 → .20` com o mesmo save, trajeto, versão do jogo, driver,
   resolução, cache aquecido e estado térmico comparável. Manter framegen
   desligado na linha de base. Registrar o salto inicial, a saída da primeira
   caverna e a superfície, com vídeo se houver piscadas.
3. Para atribuir efeito a heap, leituras in-place ou LRZ, repetir A/B/A dentro
   do mesmo APK, variando uma opção por vez. Anotar configuração e estado
   efetivo no log quando a opção estiver em Automático.

4. Registrar clocks, temperatura e cache em cada braço. Com a linha de base
   da `.21` estabelecida, comparar o Turnip atual com a próxima candidata
   mantendo o APK fixo. Ver [matriz de testes do driver](TURNIP.md#matriz-de-testes).

## Como registrar e revisar

- Crie `AAAA-MM-DD-build-cenario.md` a partir do [modelo](MODELO.md) e acrescente
  uma linha ao índice. Use a mesma entrada para rodadas do mesmo experimento e
  acrescente uma nota datada se a conclusão mudar.
- Identifique **medição no aparelho**, **relato do jogador**, **verificação no
  host** e **hipótese**. Indique se a coleta foi controlada, o que ficou sem
  medir e que conclusão os dados permitem. Use o `[diag] summary` quando
  disponível e registre FPS reais e frames gerados em campos separados.
- Mantenha os logs brutos com Rick. No Git, guarde o nome da captura, janela ou
  marcador relevante e métricas resumidas, sem dados pessoais, credenciais ou
  arquivos do jogo.
- Claude e Codex consultam este índice antes de repetir um teste. Quando
  revisarem uma conclusão, deixem o parecer e a evidência no registro e
  apontem o caminho e o commit na thread existente do
  [canal de colaboração](https://github.com/rickamaral94/Totknspcxi/tree/collab/channel/collab).
