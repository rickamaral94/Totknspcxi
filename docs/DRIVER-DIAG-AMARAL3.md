# APK de diagnóstico amaral.3

Mantém a correção da sobreposição de módulos da amaral.2, sem alterar os
endereços ou a identidade do código recompilado dessa versão.

## Registros adicionais

- `[driver-diag]`: configurações efetivas no início (32 bits, Sync lag,
  geração de quadros, buffers constantes e opções de compatibilidade).
- Antes/depois de vkCreateGraphicsPipelines e vkCreateComputePipelines:
  identificador correlacionado, TID, endereços dos shaders (compute: tamanho
  do SPIR-V no campo aux), compilações simultâneas, resultado VkResult.
- Antes de cada chamada: VmRSS, VmSize e MemAvailable em KiB. São leituras
  de /proc; zero quando indisponíveis. Não são hooks de cada malloc do driver.
- `[driver-logcat]`: snapshots limitados às últimas 128 mensagens de outros
  tags do próprio PID (libc, Mesa/Turnip, Qualcomm etc.), uma vez por segundo,
  sem reenviar mensagens capturadas para logcat. Há deduplicação com janela
  de 512 hashes. Disponibilidade depende das permissões do Android.
  O subprocesso logcat usa -d, sem manter um leitor externo permanente.

Esses registros entram no mesmo arquivo e na mesma pasta selecionada no app.
Mantenha **Salvar arquivo de log** ligado. Reproduza o crash e envie o arquivo
antes de várias reinicializações, que podem substituir a rodada anterior.
O coletor pode perder mensagens emitidas imediatamente antes de morrer;
não garante a recuperação de um tombstone ou do texto final do abortamento.

## Validação e empacotamento

Runtime ARM64 compilado com NDK r30 / CMake 3.30.4. Teste concorrente
`tools/test_driver_diag.c` e seleção nativa `tools/test_native_mode.py`.
O APK reaproveita o manifesto (campos de versão atualizados), DEX, recursos,
assets e bibliotecas dependentes da amaral.2, substituindo libmain recompilada.
Não é um assembleDebug completo. ZIP/alinhamento e assinatura são verificados.

VersionCode 6, versionName 0.2.0-beta-amaral.3, mesmo applicationId.
Chave de debug nova nesta sessão: instalação por cima da amaral.2 pode ser
recusada. Preserve os dados antes de qualquer desinstalação.

É uma versão para diagnosticar o fechamento, ainda não uma correção do crash.
As leituras de memória e de logcat têm custo; não comparar seus FPS diretamente
com uma versão sem instrumentação. Sem execução em aparelho neste ambiente.
