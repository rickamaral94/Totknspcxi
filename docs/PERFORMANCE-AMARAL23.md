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
- Build e verificação do APK: ver a seção seguinte.
