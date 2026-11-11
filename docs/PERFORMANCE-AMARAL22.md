# Amaral.22 — espera de pipelines, cache e clock da GPU

Base: amaral.21 (`af600a7`). Android arm64, pacote
`com.totkrecompandroid`, versionCode 25. O teste principal é TOTK 1.4.2
no Odin 2 Portal (Adreno 740).

## O que entrou

- **Fila Vulkan.** A espera por shaders e pipelines de passes persistentes
  agora usa uma variável de condição. A compilação sinaliza o resultado
  (sucesso ou falha), eliminando o polling a cada 200 µs. A prioridade da
  fila e a exigência de desenhar os passes persistentes continuam valendo.
  No Android, dois workers seguem como padrão. Para o controle de um worker,
  iniciar um novo processo com `LARECOMP_SHADER_WORKERS=1`. A fila registra
  quantos workers foram efetivamente criados.
- **Cache de pipelines.** No Android, o cache anterior só é substituído
  depois da gravação e fechamento bem-sucedidos do temporário. A substituição
  é feita em um rename. Isso reduz o risco de perder o cache e recompilar em
  outro lançamento após uma gravação interrompida.
- **Clock da GPU.** A medição de desempenho registra mínimo, média, máximo e
  número de amostras em gameplay quando o Android permite a leitura do KGSL.
  As amostras são instantâneas, a cada dois segundos. A média serve à
  triagem entre sessões. A comparação ponderada por residência requer
  contadores de residência separados.

## Revisão dos outros ports

| Fonte | Decisão |
| --- | --- |
| [TotK PC Recomp 0.4.0](https://github.com/valentderah/totk-pc-recomp/releases/tag/v0.4.0) | Atacar a espera da fila já existente no nosso port e permitir comparar um e dois workers. O recorder paralelo de desktop tem regressões de device lost documentadas e depende do desenho da fila daquele renderer. PGO precisa de perfil do próprio Odin. |
| [UltraTears, variante Invariant](https://github.com/kuraeow/ultratears/commit/6465cb396b0106ac91fd0c59da840422f7877809) | Preservar a posição invariante e precisa no nosso pré-passe de profundidade. Uma variante exige identificar o passe e comparar a imagem antes de testar o custo de GPU. |
| [Weeping of the Empire, fast travel](https://github.com/daniel-madrid-07/weeping-of-the-empire/commit/df0586cdc5702ca19bbebfa59697e0b6980f6237) | Usar viagem rápida em teste de pressão de memória. A mudança de orçamento do backend Windows depende da memória de host e exige reavaliar o caminho Android. |
| [TotK Native Android, clocks](https://github.com/AurelioB/totk-android/commit/e532668f7e5ab814e59ee1d8c534a11e41855175) | Adaptar a observabilidade do clock, com a distinção entre amostras instantâneas e residência. |
| [nxnative, substituição de saída](https://github.com/seedds/nxnative/commit/5f6f162257f55c549782e4624c74d764b2455b93) | Aplicar a ideia de preservar a última saída íntegra ao cache Vulkan do Android. |

## Teste no Odin

1. Rodar a linha de base `.21 → .22 → .21` no mesmo driver e checkpoint,
   com framegen desligado. Repetir o percurso na saída da caverna, superfície
   e primeira viagem rápida. Separar séries com cache frio e quente.
2. No mesmo APK `.22`, comparar 2 → 1 → 2 workers, reiniciando o app entre
   braços. Para um worker, passar `--es env LARECOMP_SHADER_WORKERS=1` ao
   iniciar `com.totkrecompandroid/.TOTKRecompAndroidActivity` via ADB.
   Confirmar a opção efetiva em `[driver-diag] pipeline workers=`.
3. Anotar FPS reais, p95/p99, `compile ... (waited ...)`, draws pulados,
   clocks, temperatura, RSS e imagem. Observar se o cache volta aquecido
   depois de sair e reabrir. Marcar dados indisponíveis como tal.
4. Para comparar builds do Turnip, repetir com o APK fixo e registrar os
   hashes do driver e o SHA Mesa no [tópico de evidências do Turnip](https://github.com/rickamaral94/Totknspcxi/blob/ceecc98c08bb13b9107170a908828c51ae084325/docs/logs/TURNIP.md).

As decisões de desempenho e correção gráfica dependem dos resultados no Odin.

## Validação do candidato

- Gradle `assembleRelease` com JDK 17, NDK r30, SDK 36 e CMake 3.30.4.
  Bibliotecas de terceiros e dependências do recompilador reaproveitadas
  do APK .21 verificado quando disponíveis. Runtime e hooks recompilados.
- Testes de host: fila com sucesso/falha e 1/2 workers ou síncrono (2.000
  operações por modo), oito verificações de timestamps GPU, 31 casos do
  tradutor, diagnóstico, timing nativo, RomFS e política de logs Android.
- Pacote ARM64 íntegro e alinhado, assinatura APK v3 validada com o certificado
  fixo Amaral. O identificador `assets/gen.id` é igual ao da .21.
- Arquivo `Totknspcxi-0.2.2-beta-amaral.22-v142-pipeline.apk` (93.156.709 bytes).
  SHA-256 `69b6082c2644a31d88b15e8c98a13a3666485982746bdcf3297303850f9e5142`.
- O aparelho e a validação Vulkan em hardware estão pendentes.
