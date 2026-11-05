# TOTK 1.4.2 — recuperação da fonte amaral.19

## Proveniência

O APK `Totknspcxi-0.2.1-beta-amaral.19-v142-two-workers.apk` foi preservado fora do Git ([download](https://chatgpt.com/library?file_id=libfile_907d37369218819187b26d21ff273859&account_id=personal)). Seu SHA-256 é `7b53e0d108d9bd9c11bbb47afe8d31a0b1088476032060f167bb6df4227922ba`; o certificado da assinatura JAR é `DF:D0:65:3E:2A:1D:F8:1B:42:A0:38:1E:39:F4:25:1A:C8:20:01:A0:F8:BD:8B:FA:70:70:15:7F:4C:36:F1:E3`, igual ao do APK .18. Verificar também os esquemas de assinatura Android e o certificado do APK instalado antes de atualizar. Este commit **reconstrói o comportamento descrito para .17, .18 e .19** a partir da fonte .16, do histórico da conversa e dos símbolos/disassembly dos APKs .18 e .19. O commit original .17 (`29c027a`) não está no Git disponível; as fontes originais exatas .18 e .19 também não foram encontradas. Não declarar reprodutibilidade byte a byte do APK anterior.

| Etapa | Evidência | Código recuperado |
| --- | --- | --- |
| .17 | Logs .16: 22.355 submissões tidas como cobertas, GPU 0 ms; histórico informa relógio anterior mais de 1 s à frente. O binário .19 contém a mensagem `[gpu-ts] recovered timestamp span`. | Em `cq_resolve`, descartar `last_end` incompatível (>1 s à frente de `s0`) e registrar a recuperação; preservar a união para spans normalmente sobrepostos. |
| .18 | APK .18 contém `urgent_build`, `stat_urgent_promoted`, `stat_urgent_queued` e mensagem `[shader-priority]`; `run_job` tem três argumentos e aloca um job de 32 bytes. Seu diagnóstico dizia um worker. | Dar prioridade ao shader/pipeline cujo estado bloqueia um draw persistente, promovendo um job enfileirado ou inserindo na frente caso o tradutor ainda esteja preparando o job. |
| .19 | No APK .19, a inicialização de `run_job` contém duas chamadas a `pthread_create` e passa 2 ao log `pipeline workers`; no APK .18 há uma chamada e valor 1. A fila prioritária permanece em ambos. O manifesto .19 tem `versionCode` 22 e versionName `0.2.1-beta-amaral.19`. | Dois workers de compilação no Android (tradutor segue separado), mantendo o limite de 2; mesma fila prioritária. |

Os dois APKs têm o mesmo tamanho de `98.882.936` bytes. Entre .18 e .19 diferem `AndroidManifest.xml`, `libmain.so` e metadados de assinatura. A biblioteca .19 tem build ID `1d193fff999deb0ec7ed1b7a367969b5bbbaa0e6`. Esta reconstrução **não foi recompilada nem comparada binariamente** com essa biblioteca; o APK antigo permanece artefato autoritativo daquela entrega.

## Validação e limites

- `tools/test_vertex_growth.py` e `tools/test_native_fg_policy.py` passaram na fonte recuperada.
- O teste de patches 1.4.2 encontrou o `main` local mas não pôde ser executado neste ambiente: falta Clang no checkout; GCC não fornece `__builtin_bitreverse64`. Não tratar como teste aprovado.
- Não há dispositivo conectado para medir o efeito dos dois workers, o uso de memória Scudo, stutters, correção visual ou 60 FPS nativos.
- A escolha de dois workers é experimental. Quatro compilações paralelas já haviam causado pressão de memória/Scudo; voltar a um se houver regressão. `.18` teve pausa de 594 ms, 578 ms de espera por compilação. Uma fila mais rápida não resolve quadros lentos sem compilação.
- Antes de distribuir um novo APK como atualização, conferir o certificado da instalação anterior e assinar com a chave original. O código fonte não contém chave privada.

## Próximo teste

No Odin 2 Portal Pro, mesmo save, trajeto, driver, resolução, cache e temperatura: A (.18, um worker), B (.19, dois), A de novo. Registrar FPS nativo, p95/p99, 1% low, espera de compilação, builds pendentes, RSS/Scudo, crashes e imagem. Repetir com cache quente/frio e separar os quadros com compilação. Se o A/B/BA não sustentar ganho sem regressão, manter um worker.
