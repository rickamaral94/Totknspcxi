# TOTK Android — ajustes pendentes dos ports de PC

Roteiro de implementação e build para Claude. A referência reproduzível é a
**amaral.22**, commit `c3cca75e956eceb984adc5619d0e6663bde72961`,
versionCode 25. O alvo primário é o Odin 2 Portal (Adreno 740) com TOTK 1.4.2
recompilado. A [.22](PERFORMANCE-AMARAL22.md) já tem espera sinalizada de
pipelines, controle de um/dois workers, gravação atômica do cache e amostras
de clock KGSL. O relógio em tempo real, cooldown e bounce da 1.4.2 já estão
no [gerador](PERFORMANCE-AMARAL16.md).

## Ordem de trabalho

1. Sincronize `collab/channel` e leia as threads `T-upstream-022-recomps` e
   `T-perf-60fps-a740`. Compare as pontas do seu branch e de
   `codex/continue-upstream-20261101`, integre o trabalho recente e confirme
   que o commit da .22 é ancestral. Revise eventuais mudanças posteriores
   antes de editar as mesmas áreas.
2. Comece pela **escalada em FPS alto**. Se faltar o executável 1.4.2 ou
   uma reprodução inequívoca, entregue a instrumentação isolada de
   viagem rápida como primeira candidata quando houver mudança útil.
   Mantenha PGO, recorder e Invariant em experimentos independentes.
3. Faça um commit por hipótese. Reserve a próxima numeração `amaral.N`
   no canal antes de alterar versionName/versionCode. Gere uma candidata
   com um escopo que possa ser comparado com a .22. Publique o SHA do código,
   o resultado de build e o próximo teste na thread existente.

## Ajustes pendentes

| Prioridade e origem | Implementação proposta | Critério de validação |
| --- | --- | --- |
| **1. Escalada em FPS alto** · [PC Recomp](https://github.com/valentderah/totk-pc-recomp/commit/06811330e738e96f298cd290ca3868f817265fc2) | Medir a mesma parede e escada a 30 e 60 FPS reais. O upstream corrige a curva acima de 60 FPS na 1.4.3 e 1.2.1. Se a divergência for reproduzida, localizar a rotina e as instruções originais no `main` 1.4.2 fornecido pelo Rick e adaptar a `tools/patches.py`, reutilizando o timing atual. Preservar o comportamento existente a 30/60 FPS. Tratar o modo nativo 1.4.3 separadamente em `runtime/native.c`, se entrar no escopo. | Validar build ID, instruções esperadas, recusa em caso de palavra divergente e resultado do patch em host. Medir distância por tempo e animação nas duas cenas. Só testar a curva acima de 60 onde o jogo sustentar FPS reais nessa faixa. Framegen fica desligado. |
| **2. Memória e viagem rápida** · [Weeping of the Empire](https://github.com/daniel-madrid-07/weeping-of-the-empire/commit/df0586cdc5702ca19bbebfa59697e0b6980f6237) | Começar com viagens repetidas entre os mesmos dois checkpoints. Correlacionar RSS, memória disponível, alocações Vulkan/VMA quando observáveis, tamanho do cache e motivo de saída. Se houver crescimento ou falha, localizar o recurso retido antes de alterar orçamento ou descarte. O orçamento do desktop depende de outra arquitetura de memória. | Registrar pico e recuperação de memória, duração da transição, áudio/imagem, crashes e device lost. Uma correção precisa reduzir a retenção ou resolver a falha reproduzida mantendo as transições corretas. |
| **3. PGO do runtime Android** · [PC Recomp](https://github.com/valentderah/totk-pc-recomp/blob/7a37fc3c346cd6f2b3c80637effb27b880ce817b/docs/pgo.md) | Instrumentar o target `main` de `android/totkrecompandroid/app/src/main/cpp/CMakeLists.txt` em variante opt-in. Gravar perfis em diretório privado com saída verificável. Treinar no próprio Odin com superfície, compilação, carregamento e viagem rápida. Mesclar com `llvm-profdata` do mesmo NDK e recompilar o mesmo commit em modo profile-use. | Comparar release sem PGO e release com PGO em trajeto que ficou fora do treino, mesmo Turnip e cache. Verificar se o compilador consumiu o perfil e medir CPU de render, p95/p99, tempo de carregamento e correção. Registrar origem do perfil. |
| **4. Recorder paralelo** · [PC Recomp 0.4.0](https://github.com/valentderah/totk-pc-recomp/releases/tag/v0.4.0) | Em `runtime/nvn/vulkan/exec.c`, medir com cache quente gravação de comandos, submit, espera de fences e compilação em separado. Só prototipar se a gravação se mostrar gargalo. Começar com um passe independente, pools por thread e submissão ordenada. Auditar o tempo de vida de descriptors, imagens e buffers e as dependências entre passes. Manter a variante experimental opt-in. | Validar sincronização e imagem em rodada separada da medição. Conferir ausência de device lost, corrupção e piora de p99. A redução de CPU precisa aparecer no passe medido. |
| **5. Invariant de vértice** · [UltraTears](https://github.com/kuraeow/ultratears/commit/6465cb396b0106ac91fd0c59da840422f7877809) | Inspecionar `runtime/nvn/maxwell/translate.c` e identificar o passe e o par de shaders. Preservar `Invariant` e `precise` no pré-passe e nos draws que dependem de `EQUAL`. Se a captura indicar custo e o passe for independente, criar uma variante restrita com chave própria de cache. | Comparar cor e profundidade no mesmo checkpoint, incluindo movimento de câmera e saída da caverna. Só avaliar ganho de GPU por passe depois de validar a imagem. |

Quando o ajuste tocar o gerador 1.4.2, conferir `GAMELIB_ID` e
`assets/gen.id`, rodar `python3 tools/test_totk142_patches.py /caminho/privado/exefs/main --cc clang`
com o dump correto e confirmar a reconstrução da biblioteca no aparelho.
O executável do jogo, os saves e os logs brutos ficam fora do Git.
Para timing, rodar também `python3 tools/test_native_timing_fixes.py`.
Para mudanças Vulkan, rodar `bash tools/test_translate_ops.sh` e os testes
do caminho afetado, além de capturar a validação Vulkan onde disponível.
Registre testes que dependem de hardware ou dump como pendentes quando faltar
o insumo.

## Build ARM64 e assinatura

Use JDK 17 com `javac`, SDK 36, NDK `30.0.16248370`, CMake `3.30.4`,
Build Tools `35.0.0` e o wrapper do projeto. Com o SDK configurado:

```bash
git submodule update --init --recursive
export ANDROID_NDK="$ANDROID_HOME/ndk/30.0.16248370"
bash android/deps/build_deps.sh
ANDROID_NDK_LINUX="$ANDROID_NDK" bash android/deps/build_ffmpeg.sh
bash android/deps/build_gen_deps.sh
(cd android/totkrecompandroid && bash ./gradlew --no-daemon assembleRelease)
```

O Gradle entrega o APK em `android/totkrecompandroid/app/build/outputs/apk/release/`.
Mantenha o recompilador embarcado para a 1.4.2. O APK pode ser compilado sem
o dump do jogo. Caso o submódulo DXBC falhe, restaure de um checkout
confiável o commit fixado pelo submódulo e confira o SHA. Na .22 era
`78ab59a8aaeb43cd1b0a5e91ba86722433a10b78`.

Use a [assinatura fixa](SIGNING-AMARAL.md). Caso o Gradle produza um APK
unsigned, alinhe e assine com a mesma chave fora do Git. Para verificar o
candidato assinado, defina `APK` para o caminho final:

```bash
BT="$ANDROID_HOME/build-tools/35.0.0"
"$BT/apksigner" verify --verbose --print-certs "$APK"
"$BT/zipalign" -c -P 16 4 "$APK"
"$BT/aapt2" dump badging "$APK" | grep -E '^package:|^native-code:'
unzip -tq "$APK"
sha256sum "$APK"
```

Conferir o certificado da política de assinatura, o pacote
`com.totkrecompandroid`, o versionCode reservado, a ABI `arm64-v8a` e
a presença do recompilador (`assets/gen.id`, `assets/gen/`, Python e Zig).
Entregar APK, SHA-256, commit, lista de testes e limites da validação.
A release pública fica sob decisão do Rick.

## Coleta no Odin e encaminhamento ao Turnip

Use o mesmo save de teste, trajeto, driver, resolução e modo de energia.
Separe cache frio e quente, desligue framegen na linha de base e registre
FPS reais, frametime p95/p99, clocks, temperatura, compilação, draws pulados,
imagem e estabilidade. Para repetir o braço A após atualizar o APK, prefira
um seletor experimental na mesma build quando a mudança permitir. Se forem
necessárias duas builds, planeje a alternância e a preservação dos saves
antes do ensaio, pois o Android pode bloquear o downgrade.

No [índice de logs](https://github.com/rickamaral94/Totknspcxi/blob/codex/totk-test-logs/docs/logs/README.md),
registre o resultado por hipótese. Se houver diferença reproduzível associada
à troca do ELF do driver com o mesmo APK, registre o caso no
[tópico Turnip](https://github.com/rickamaral94/Totknspcxi/blob/codex/totk-test-logs/docs/logs/TURNIP.md)
com hashes do ZIP e do ELF, SHA Mesa, patchset e evidência da cena. A amostragem
de clock da .22 é instantânea. Para comparar residência, guarde os contadores
de residência quando disponíveis.
