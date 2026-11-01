# Teste de write-back no celular — 2026-10-29

Dispositivo ADB: `192.168.1.6:40089`, modelo Android `24122RKC7G`, GPU Adreno 830 com Turnip. APK debug compilado e instalado com sucesso. Configuração existente: código nativo, resolução 50%, taxa Auto, sync lag ligado.

## Procedimento e isolamento

Três execuções carregaram o save mais recente na saída da caverna / Grande Ilha Celestial, sem movimentação. Cada execução usou uma pasta nova criada pelo app (`wbtest-20261029`, `wbtest-off-20261029`, `wbtest-diag-20261029`) e `LARECOMP_SAVE_SEED` apontando para o save original. O teste não substituiu o save do jogador nem editou as configurações persistidas.

- Regra nova ligada, cerca de 77 segundos.
- Regra nova desligada com `LARECOMP_WB_CPU_SMALL=0`, cerca de 78 segundos.
- Diagnóstico com a regra ligada e `LARECOMP_LOG_WB_FRAME=1` para identificar as flags do alvo.

Ao terminar, as execuções de teste foram encerradas e o app foi reaberto normalmente, sem extras de teste. O APK instalado mantém a regra nova ligada por padrão.

## Resultados

- Os 14 casos de `test_writeback_policy` passaram no próprio aparelho.
- O jogo chegou à cena nas duas rodadas de comparação. Não foram encontradas assinaturas `FATAL` / `Fatal signal` nos logs dessas rodadas.
- Ambas registraram `write-back 128x128 f0x25 at 359dda000`.
- O diagnóstico confirmou `flags 0x40`: esse alvo é **linear**, portanto já era atendido pelo caminho antigo de write-back para alvos lineares de até 512 KB. Sua presença no log não comprova a necessidade da ampliação para pools CPU-visíveis não lineares.
- Artefatos verticais alongados no cenário aparecem com a regra ligada e desligada. O teste não resolveu nem determinou a origem desse defeito.
- Entre 50 e 75 segundos de execução, a média das amostras de FPS do log foi 22,55 com a regra ligada (13 amostras) e 19,49 desligada (12 amostras). São rodadas únicas, sem controle de clocks/temperatura/cache; **não demonstram ganho de desempenho**.

## Pendências

Ainda é necessário observar a aplicação dos bytes GPU→CPU e testar corte/pisoteio da grama e reação de insetos. Este teste foi estático na saída da caverna. Também não valida a iluminação no interior da caverna nem a importação pela interface.

Evidências locais, ignoradas pelo Git: `build/writeback-device-20261029/enabled.log`, `disabled.log`, `diagnostic.log`, `enabled.png` e `disabled.png`.
