# Novembro de 2026 — histórico inicial no Odin 2 Portal

Aparelho de referência: AYN Odin 2 Portal, Snapdragon 8 Gen 2 e Adreno 740.
Esta retrospectiva usa relatórios versionados e observações recuperadas das
sessões de teste. Nas sessões recuperadas, os logs brutos e os parâmetros
completos precisam ser conferidos com Rick para reprodução.

## Piscadas e frame generation

- **Medição histórica:** com sync lag desligado, a média caiu de 29,77 para
  26,13 FPS na comparação registrada. O usuário ainda viu piscadas. Em uma
  captura com framegen 2×, foram anotados 24,29 FPS reais, cerca de 44–60 FPS
  apresentados e custo de aproximadamente 6 ms. A captura não é um A/B/A.
- **Relato do jogador:** na `.8`, houve relato inicial de que o jogo parou de
  piscar. O sintoma voltou após a primeira caverna em teste posterior, cuja
  build não está identificada aqui. A saída da caverna permanece um ponto
  obrigatório para verificar a correção.
- **Conclusão:** desligar sync lag não resolveu o sintoma na sessão observada.
  O retorno das piscadas mantém a causa em aberto. Para confirmar uma
  correção, filmar o mesmo trecho antes e depois da saída e anotar build,
  versão do jogo, driver, opções e marcadores do log.

## Amaral.14 a Amaral.19

- **Medição no Odin:** o diagnóstico da `.14` na 1.4.2 registrou 28,75 FPS
  reais e 21,69 ms de CPU de render por frame. A análise posterior da `.16`,
  também 1.4.2 e com framegen desligado, anotou 36,88 FPS reais, pico de 52 e
  17,28 ms de CPU de render por frame. Houve quedas a 15–21 FPS perto de
  6 mil draws. As coletas não estabelecem ganho controlado. A leitura de GPU
  de 0 ms na `.16` foi tratada como inválida.
- **Verificação da fonte e dos APKs:** a `.17` tratou a leitura inconsistente
  de timestamps, a `.18` priorizou pipelines que bloqueavam draws e a
  `.19` passou a dois workers. O código dessas etapas foi reconstruído a
  partir das evidências disponíveis.
- **Conclusão:** 60 FPS nativos sustentados, física e cutscenes em FPS alto
  seguem sem confirmação nestas coletas. O efeito dos dois workers ainda
  precisa de A/B/A com frametime, espera de compilação, RSS/Scudo e imagem.
  Fontes: [relatório da .16](../PERFORMANCE-AMARAL16.md) e
  [recuperação da .19](../PERFORMANCE-AMARAL19-RECOVERY.md).

## Amaral.20

- **Medição no Odin:** duas sessões de 60 s usaram o APK `.19`, TOTK 1.4.2,
  Turnip Amaral 27.0.0-devel v5.8.17.1, alvo de 60 FPS, escala 1×, GSR 1 e
  AA 2. Com heap, in-place e LRZ desligados, houve 29,2 FPS reais, p95 de
  56,8 ms e GPU de 25,1 ms/frame. Com os três ligados, houve 41,4 FPS reais,
  p95 de 44,0 ms e GPU de 20,8 ms/frame. As faixas de temperatura também
  diferiram. O jogador não viu erro gráfico nessas sessões.
- **Conclusão:** a diferença observada motivou habilitar os caminhos rápidos
  no Automático da Adreno 740. As sessões deixam em aberto a contribuição
  de cada opção, a estabilidade prolongada e a validação do próprio
  Automático na `.20`. Fonte: [relatório da .20](../PERFORMANCE-AMARAL20.md).

## Amaral.21

- **Verificação no host:** Gradle `assembleRelease`, 31 casos de tradução
  de shaders, testes de RomFS, testes de timing e política de captura do
  driver passaram. O teste de patches com o executável do jogo 1.4.2 ficou
  pendente.
- **Conclusão:** TXD e RomFS entraram como correções. A retirada do polling
  secundário do log do driver pode reduzir o custo em gameplay. O efeito
  em FPS, frametime e imagem no Odin 2 segue pendente da comparação com
  a `.20`. Fontes: [relatório da .21](https://github.com/rickamaral94/Totknspcxi/blob/af600a7c4090a0e178b843bfe32df89f7de243ae/docs/PERFORMANCE-AMARAL21.md)
  e [avaliação dos upstreams](https://github.com/rickamaral94/Totknspcxi/blob/af600a7c4090a0e178b843bfe32df89f7de243ae/docs/UPSTREAM-2026-11-10.md).
