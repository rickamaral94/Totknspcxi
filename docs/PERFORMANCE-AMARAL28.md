# amaral.28 — correção: imagem lavada de branco (constant buffers lidos no lugar com o sync lag)

Base: amaral.27 (`4de737e`). versionCode 31, `0.4.0-beta-amaral.28`, mesma assinatura (certificado `0969fc07…79bf`).

## O defeito

Rick, 28/11, Odin 2 Portal, TOTK 1.4.2, amaral.27: o jogo roda a 60 fps, mas a imagem sai lavada de branco, com o
cenário e o Link como silhuetas escuras. É igual com o Turnip v5.9.19.1 (`40921809…`, o mesmo das versões boas) e com o
driver da Qualcomm, que além disso travou o aparelho (watchdog: render thread esperando um shader). O log não mostra
erro, perda do dispositivo nem falha de shader.

## A causa

`cbwin_bind` (exec.c), a janela por desenho dos constant buffers: o buffer é ligado direto da memória do jogo, que está
na GPU, e só as linhas atualizadas pela lista de comandos vão num buffer à parte.

- **Na .24 (nossa):** usada só com "Constant buffers lidos no lugar" ligado (`cbwin_now`), que nunca liga com o sync
  lag, e com o teste da janela compilado nos shaders (`cbwin_wanted`, padrão ligado com o heap na GPU).
- **No merge da .25 (a do upstream):** usada sempre que o heap está na GPU, **com** o sync lag e **sem** o teste nos
  shaders (o upstream só compila o teste com `LARECOMP_CB_WINDOW=1`). Com o sync lag, a CPU já escreve as constantes do
  quadro seguinte enquanto a GPU lê as deste, e as linhas atualizadas iam para um buffer que os shaders não liam. É o
  sintoma do BOTW em 27/10 (água branca, "cortinas" no terreno), aqui em toda a iluminação.
- **Por que o upstream não vê:** o automático dele põe o heap na GPU só nas Adreno 8xx; o nosso também na 740
  (Odin 2).
- **Na .26 eu mantive a do upstream.** Foi errado pela nossa regra: a nossa não era pior. A .28 volta a nossa.

## A correção

`cbwin_bind` de novo só com `cbwin_now()` (`LARECOMP_CB_WINDOW=1` ou constant buffers no lugar, que exige o sync lag
desligado); `cbwin_wanted` de novo ligado por padrão com o heap na GPU (o teste nos shaders, como na .24).
`LARECOMP_CB_WINDOW=0` desliga os dois.

## Atualização

- Instala por cima da .27 (versionCode 31).
- Os shaders são traduzidos de novo na primeira vez (a chave da tradução mudou): pequenos travamentos no começo, uma vez.
- O código do jogo **não** é recompilado (nada mudou no gerador).

## Validação

- Host: `test_translate_ops.sh` 36/0, `test_native_timing_fixes.py`, `test_romfs_build.py`,
  `test_android_log_policy.py`, `test_native_fg_policy.py`, `test_render_sigs.py`, `test_fg_pacing.c`.
- `assembleRelease` assinado, certificado conferido, versionCode 31.
- Aparelho: pendente (a imagem no mesmo lugar da foto de 28/11).
- Não vai para o agente do Turnip: o defeito aparece igual no driver da Qualcomm, então é nosso.
