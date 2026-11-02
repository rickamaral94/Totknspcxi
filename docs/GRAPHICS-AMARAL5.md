# Amaral.5 — diagnóstico gráfico para Odin 2 Portal

Pacote com.totkrecompandroid, versionName 0.2.0-beta-amaral.5, versionCode 8.

## Mudanças

- Runtime Android recompilado em Release, otimização -O2. Java, recursos e assets preservados do APK amaral.3. Empacotamento tools/repack_amaral5.py verifica que somente libmain.so e a versão do manifesto mudaram. Não é um assembleRelease completo.
- Um worker de pipelines no Android, equivalente ao experimento amaral.4 que evitou a exaustão de regiões do Scudo; quantidade registrada no log.
- Agregados graphics-diag por intervalo: cópias de profundidade/cor, snapshots reutilizados, alterações de escala de saída/cena e imagens recriadas por mudança de escala. Sem habilitar a varredura quadrática de aliases.
- Rejeição de capturas gpuprof com timestamps decrescentes, em vez de subtração unsigned que gerava durações absurdas. Isso não resolve todas as limitações de medição do tempo GPU.
- Nova assinatura fixa, backup privado salvo e fingerprint registrado em SIGNING-AMARAL.md. Sem mudanças no driver Turnip.
- GAMELIB_ID confirmado pelo CMake: 9588e7371082fde1, igual às versões anteriores.

## Validação e limites

Compilação nativa arm64 Android API30 com NDK30.0.16248370/CMake3.30.4. Testes host de diag.c e driver_diag.c. Verificação de assinatura, manifesto, zip alignment e integridade das entradas do APK. Não testado em dispositivo físico. Não há correção validada para as fumaças nem promessa de 30 FPS.

## Rodada de teste

Preservar saves e arquivos antes da reinstalação exigida pela nova assinatura. Usar o mesmo Turnip e configurações atuais. Primeiro framegen desligado, limite30, resolução1x; percorrer a mesma área das fumaças por cerca de60–90s e exportar log. Depois repetir com framegen2x para comparar. Não alterar LRZ, sync lag ou outras opções entre essas duas rodadas.
