# amaral.21 — upstream 0.2.2 e logging de baixo overhead

Base: amaral.20, commit `38fadc4`. Versão candidata:
`0.2.2-beta-amaral.21`, versionCode 24.

## Alterações

- Opcode Maxwell TXD com derivadas explícitas traduzido para Vulkan/GLSL
  `textureGrad`. O upstream encontrou um shader da superfície que perdia cerca de
  11 draws por quadro quando a tradução falhava.
- Configuração “usar RomFS no local” corrigida para o TOTK, que não possui
  `System/Version.txt`. A validação usa `System/RegionLangMask.txt` e percorre o
  diretório diretamente, sem listagem de centenas de milhares de arquivos pelo SAF.
- O snapshot secundário das mensagens Mesa/Adreno via `logcat -d` deixa de criar
  um processo a cada segundo por padrão. Para uma investigação de driver, usar
  `LARECOMP_DRIVER_LOGCAT=1`. Logcat do runtime, log opcional e crash reports foram
  preservados.

## Hipótese de desempenho

A remoção do polling do driver reduz wakeups, processos e leitura do buffer do
Android durante gameplay. A direção é sustentada por medições do projeto TotK
Native sobre duplicação de logs, mas ainda não foi medida neste fork ou no Odin 2
Portal. TXD é primeiro uma correção gráfica: os draws restaurados podem custar GPU,
portanto não são anunciados como aumento de FPS.

## Validação no host

- `tools/test_android_log_policy.py`: relay principal mantido e captura secundária
  somente por opt-in.
- `tools/test_translate_ops.sh` com GCC/G++: 31 casos, zero falhas.
- `tools/test_native_timing_fixes.py`: timer de equipamento e bounce em 15–240 Hz.
- `tools/test_romfs_build.py`: 6 testes de imagem e 9 verificações de conjunto.
- `tools/test_romfs_build_py.py`: imagem Python idêntica à implementação C.
- Gradle `assembleRelease`: concluído com Android SDK 36, Build Tools 35.0.0,
  NDK 30.0.16248370, CMake 3.30.4 e JDK 17; ABI `arm64-v8a`.
- `git diff --check`: sem erro.

O build Android compilou o tradutor e o runtime nativo. O teste
`test_totk142_patches.py` não foi executado porque exige um `main` 1.4.2 fornecido
pelo usuário; nenhuma imagem do jogo é armazenada no repositório ou artefato.

## APK candidato

- Arquivo: `Totknspcxi-0.2.2-beta-amaral.21-v142-upstream.apk`
- SHA-256: `1e3ad3f02abca00055e4a00a0ffaabe2f49cee5ed95a3d8299df7d549765fc7e`
- Tamanho: 93.156.709 bytes.
- Pacote: `com.totkrecompandroid`; versionCode 24; versionName
  `0.2.2-beta-amaral.21`.
- Assinatura APK v3 válida, um assinante. Certificado SHA-256
  `dfd0653e2a1df81b42a0381e39f4251ac82001a0f8bd8bfa7070157f4c36f1e3`,
  igual ao APK amaral.16 usado como referência de atualização.
- `unzip -t`: íntegro. Somente ABI `arm64-v8a`; `libmain.so`, FFmpeg, SDL3,
  shaderc, Python/Zig, Unicorn/Capstone/LZ4 e `liblsfg_framegen.so` presentes.

Este é um candidato tecnicamente validado no host. A promoção para build estável
continua condicionada à revisão cruzada e ao A/B/BA no Odin.

## Teste obrigatório no Odin

Comparar amaral.20/amaral.21/amaral.20 com cache quente, mesma resolução, driver,
save, trajeto e estado térmico. Conferir salto inicial, saída da primeira caverna e
superfície. Registrar FPS reais, P50/P95/P99, GPU por frame, render thread, compilações,
draws pulados e correção visual. Até lá, a candidata não é uma otimização validada.
