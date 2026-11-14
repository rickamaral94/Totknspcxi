# amaral.24 — evidência para o trabalho no driver (M1–M7) e chave nova

Base: amaral.23 (`1457c7b`). Pacote `com.totkrecompandroid`, versionCode 27,
`0.2.2-beta-amaral.24`. Pedido do agente do Turnip (Amaral Adreno Tools,
`docs/PORTS-EVIDENCIA.md`), em nome do Rick, em 14/11/2026.

## Cobertura M1–M7

| Item | Estado | Onde, no log |
|---|---|---|
| M1 driver carregado | **tem** | `[vk-cap] ... driver-id N name ... info ...` (nome, versão, API, driverID); `[vk] driver <pacote>: loaded through adrenotools` ou o motivo da queda para o driver do sistema; **novo:** `[evidence] driver object <caminho>: N bytes, sha256 ..., build-id ...` de cada objeto Vulkan carregado no processo |
| M2 GPU e sistema | **tem** | **novo:** `[evidence] system: Android X (API N), device, SoC, kernel, build` e `[evidence] gpu: kgsl model ..., chip_id 0x..., gpu_id, gmem` (ioctl `KGSL_PROP_DEVICE_INFO`) |
| M3 configuração efetiva | **tem** | `[diag] run:` (resolução, upscaler, AA, fps, sync lag, cbuf, framegen, LRZ), `[driver-diag] render settings`, `guest heap ...`, `sampled by it: ... (automatic/settings)`, `Turnip LRZ ... TU_DEBUG=...`, `[vk-cache] pipeline cache loaded: N KB` (cache quente/frio) |
| M4 linha do tempo | **tem** | `[vk] t=...` a cada 2 s; `[diag] summary` com FPS reais, p50/p95/p99, GPU por quadro, CPU por thread, clusters, temperatura; **novo:** `GPU clock by residence (devfreq trans_stat ...)`: média ponderada e % por frequência na janela |
| M5 contagens | **tem** | `[vk] t=...`: draws, passes, dispatches, flushes, binds de pipeline e sets, fbcopy, upload (totais da janela de 2 s, divida pelos quadros) |
| M6 variáveis sem rebuild + logcat do driver | **tem, sem zip** | variáveis por `adb shell am start ... --es env NOME=valor,...`; opção **Mensagens do driver no log** (.23) copia o logcat do processo (todas as tags: TU, MESA, vulkan, adrenotools) para o log com prefixo `[driver-logcat]`. Não há zip: o arquivo de log já reúne tudo |
| M7 crash / device lost | **tem** | `VK_CHECK`: chamada e `VkResult`; `vk_device_fault_report` (VK_EXT_device_fault); **novo:** `[vk] last marker: submission N (x ms), last recorded pass #, last dispatch cs ...` sempre, mesmo sem a extensão; backtrace termina com `module <arquivo> build-id <hex>` de cada módulo |

Limites: o SHA-256 do driver é calculado numa thread após criar o device (pode
aparecer alguns segundos depois); `trans_stat` e o ioctl do KGSL dependem de
permissão do Android (o log diz `n/a`/`unavailable` com o motivo); o último
passe/dispatch é o último **gravado**, não necessariamente o que a GPU executava.

## Chave de assinatura nova

Ver `docs/SIGNING-AMARAL.md`. Certificado
`0969fc0776fa8f3fe2f5361a8caa3c415bf7bfdfbc6a7cdbcd2e23c24a6079bf`. **Não
instala por cima** de APKs com a chave anterior: exportar saves, desinstalar,
instalar, importar saves e configurar os arquivos do jogo de novo (a
desinstalação apaga os dados do app, inclusive o código do jogo recompilado).

## Validação

- SHA-256 e leitor de `trans_stat` testados no host contra `sha256sum` e um
  arquivo de exemplo; NDK clang `-fsyntax-only -Wall` limpo nos arquivos.
- Gradle `assembleRelease` assinado (lint vital pulado: download do Maven).
- `apksigner verify`: v2, 1 assinante, certificado acima. `zipalign -c -P 16 4`,
  `unzip -tq`, `aapt2`: pacote, versionCode 27, `arm64-v8a`. Strings novas em
  `libmain.so`.
- APK: `Totknspcxi-0.2.2-beta-amaral.24-v142.apk`, 92.917.062 bytes, SHA-256
  `800fe9c769caa4acabc74f743d6378857b85864dde47c67821b70d456a8c28db`.
- Aparelho: pendente.

## Teste no Odin

Instalar (migração acima), abrir o jogo com "Salvar arquivo de log" ligado,
jogar 2 minutos com a medição de desempenho, fechar e enviar o log. Conferir
as linhas `[evidence]`, `GPU clock by residence` e o `[diag] summary`.
