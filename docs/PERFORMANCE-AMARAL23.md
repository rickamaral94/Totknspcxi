# amaral.23 — ajustes da revisão e primeiros itens dos ports de PC

Base: amaral.22 (`c3cca75`) + roteiro `docs/PORTS-PC-PENDENCIAS.md` (`27fb999`).
Pacote `com.totkrecompandroid`, versionCode 26, versionName
`0.2.2-beta-amaral.23`. Alvo: TOTK 1.4.2 recompilado no Odin 2 Portal.

## O que entrou

| Commit | Mudança | Padrão |
|---|---|---|
| `bc3465a` | Histórico de testes (`docs/logs/`) e regras de agentes no branch de trabalho | — |
| `f1e7f69` | Cache de pipelines: aviso acima de 64 MB, tamanho carregado no log, "saved" só quando gravou. Opção **Mensagens do driver no log** na tela de início. `tools/apk_manifest.py` | opção desligada |
| `f2010d5` | Linha `[mem]` a cada 2 s: RSS (e pico), memória disponível (e mínimo), VMA em blocos e alocado, texturas, tamanho do cache de pipelines | sempre (só log) |
| `731d87e` | `LARECOMP_VK_NO_INVARIANT=1`: A/B do `invariant` da posição; `precise` e NoContraction ficam | desligado |

Nada muda no caminho de renderização com as opções no padrão. A diferença
para a .22 em gameplay é uma leitura de `/proc` a cada 2 s e uma consulta do
tamanho do cache a cada 20 s.

## Itens do roteiro

1. **Escalada em FPS alto: não aplicável no Odin agora.** O patch
   `0681133` do PC Recomp só altera o fator de velocidade quando o passo do
   jogo é menor que 1/60 s (acima de 60 fps). Entre 30 e 60 fps o fator do
   próprio jogo já é contínuo (`1,272 + 1,189·(1 − r)`, limitado a [1, 2]) e
   o patch o mantém bit a bit. Como o Odin não passa de 60 fps nativos, não há
   o que corrigir; fica só a medição de controle (mesma escada a 30 e 60 fps,
   velocidades iguais). Frame generation não muda o passo do jogo.
2. **Memória e viagem rápida:** instrumentado (`[mem]`). Teste abaixo.
3. **PGO:** não iniciado. Exige build instrumentado, sessão de treino no
   Odin e segundo build; fica para rodada própria.
4. **Recorder paralelo:** não iniciado. O roteiro pede medir antes; a medição
   usa o `LARECOMP_VK_PROF` existente.
5. **Invariant:** o tradutor já emite `invariant` + `precise gl_Position` em
   todo vertex shader e o NoContraction em toda aritmética de vértice.
   Entrou só o A/B opt-in, que remove apenas `invariant`.

## Testes no Odin

Configuração comum: 1.4.2, 60 fps, framegen desligado, Compatibilidade do
driver em Automático, mesmo save e trajeto, cache quente.

1. **Viagem rápida:** 5 viagens seguidas entre os mesmos dois pontos.
   Enviar o log. Critério: o `rss` e o `available` voltam ao mesmo patamar
   depois de cada carregamento; um aumento a cada viagem que nunca volta é
   retenção. Anotar crash, áudio e imagem.
2. **Invariant (só com ADB):** iniciar com
   `adb shell am start -n com.totkrecompandroid/.TOTKRecompAndroidActivity --es env LARECOMP_VK_NO_INVARIANT=1`,
   depois sem, depois com (A/B/A). A primeira vez recompila os shaders. Olhar
   folhagem e grama de perto (piscadas pretas = o `invariant` era necessário).
   Medir GPU por frame no `[diag] summary`.
3. **Cache:** depois de uma sessão longa, procurar `pipeline cache loaded` na
   abertura seguinte e o aviso de limite.

## Validação

- `tools/test_translate_ops.sh`: 31 casos, 0 falhas.
- `aarch64-linux-android35-clang -fsyntax-only -Wall`: sem erros; nenhum
  aviso novo nos arquivos alterados.
- Gradle `assembleRelease` (JDK 17, SDK 36, NDK 30.0.16248370, CMake 3.30.4,
  Build Tools 35.0.0) a partir de `10acf6e`, com as dependências de
  `android/deps` já compiladas e `thirdparty/dxbc` no commit fixado
  `78ab59a8aaeb43cd1b0a5e91ba86722433a10b78`. O `lintVitalAnalyzeRelease` foi
  pulado: ele baixa `kotlin-reflect` do Maven e o download falhou; o código
  nativo e o Java compilaram antes dele.
- `aapt2`: `com.totkrecompandroid`, versionCode 26, `0.2.2-beta-amaral.23`,
  ABI `arm64-v8a`. `zipalign -c -P 16 4` e `unzip -tq` sem erro. Recompilador
  presente (`assets/gen.id`, 2.898 arquivos em `assets/gen/`, Python 3.14 e Zig).
  As strings novas (`[mem]`, `LARECOMP_VK_NO_INVARIANT`, `driver_logcat`, aviso
  do cache) estão em `libmain.so`.

## APK

**Sem assinatura.** A chave fixa (`SIGNING-AMARAL.md`, certificado
`dfd0653e…f1e3`) só existe no ambiente do `chatgpt`; o Gradle não usou chave
nenhuma (não caiu na de debug). Antes de instalar por cima da .22, assinar com
a chave fixa (`apksigner sign` após o `zipalign` já feito) e conferir o
certificado com `apksigner verify --print-certs`. O SHA-256 abaixo é do APK
**sem assinatura**; o assinado terá outro.

APK `Totknspcxi-0.2.2-beta-amaral.23-v142-unsigned.apk`: SHA-256 `ffdc25d3d2c6bb2035f60e40906831faf23c2dece4ccd3bae1b5d345981d4264`, 92.908.398 bytes

| Arquivo | SHA-256 | Bytes |
|---|---|---|
| `assets/gen.id` | `12389b4e9c5cfca780367cab2459f406f924935d6b6fcf33a7902dab2aafab04` | 64 |
| `lib/arm64-v8a/libSDL3.so` | `1a13cada7881c434fa48307edf0ddfbb90eb1e83b406e2c7366f682321aacd79` | 2148080 |
| `lib/arm64-v8a/libavcodec.so` | `150db475650bcd2b3d15252be2ed97e8b1ba94c7e0c78c37ad4f1b86dfd980c8` | 2876008 |
| `lib/arm64-v8a/libavformat.so` | `95d65600bdc764dc8d0dbd3095b956be83273f709bd70da82e5b46ad3efbde40` | 471304 |
| `lib/arm64-v8a/libavutil.so` | `d0185a17558dd5798184359b59d01b56c607373daad0e2e51f48e4d9b3866950` | 688088 |
| `lib/arm64-v8a/libcapstone.so` | `bb231104585b35bb36bd3cf60969a1d8b8e736080e655d94eeb8053d3d83145c` | 1077872 |
| `lib/arm64-v8a/libfile_redirect_hook.so` | `f9ad0eea3a409e5dcae90de0e7a765be9a5dc9f6bbc6607466df1b14bd52ed82` | 4032 |
| `lib/arm64-v8a/libgsl_alloc_hook.so` | `92f03928ce12b01c6b4665aae91dc51bf9584bda1aac7ebb2cf31626d890cf80` | 4344 |
| `lib/arm64-v8a/libhook_impl.so` | `89f0ccca59656c19d83da0c1d582b3a2866d36d889b739da3052c50a0a2b169d` | 105352 |
| `lib/arm64-v8a/liblsfg_framegen.so` | `f1b921502617f608e6c7188495df7f6b9c215d81dd1f2eec76a86cd9e17b679b` | 792192 |
| `lib/arm64-v8a/liblz4.so` | `7d058c1af928989c5ffd1ee2e3e7784dc7fede26403da05718de135b2b08bc93` | 159272 |
| `lib/arm64-v8a/libmain.so` | `4bd16fb6510d6420d278eab6393bdce2626a4747299a6df594f83c79a2734a23` | 3852840 |
| `lib/arm64-v8a/libmain_hook.so` | `5dad41b913436458d35945eb94471453210ea0205743fa223d5ec26b09ef83ec` | 4248 |
| `lib/arm64-v8a/libpymain.so` | `5d8ecb4366112114c26cd513ffa25819d312b92ad757a98f1260f278e99a6fa4` | 4736 |
| `lib/arm64-v8a/libpython3.14.so` | `655816679dcd5e36e48187515c9435ad330688c1b2f2ffb40fc4f1a3f3e17bf4` | 5835936 |
| `lib/arm64-v8a/libshaderc_shared.so` | `3e5edd864bd9166d876109fd5888ac665e4dac6de201fd6a639b6be4dae16f20` | 8890208 |
| `lib/arm64-v8a/libswresample.so` | `d5d3a1f0f4e8aac02e0ae04ca937eb3c2d9b8750d774ec812e4a03aa1672ca6e` | 90864 |
| `lib/arm64-v8a/libswscale.so` | `0ad93054bd674175ff1ff7c7f550bde96de817511901a7d7f3ed3b0d53263eab` | 1192656 |
| `lib/arm64-v8a/libunicorn.so` | `6c0a1ae2e1e2ce8e35c3398ee73bcb3f484124a016d0ffb03eaae3824112933b` | 2266624 |
| `lib/arm64-v8a/libzig.so` | `c296564bc9a54c6c8ac40628c167aed9fb547ac982b19f0ba5a3995582dbcf4a` | 171691896 |

Para ver o que difere da .22: `python3 tools/apk_manifest.py <amaral.23> --diff <amaral.22>`.
