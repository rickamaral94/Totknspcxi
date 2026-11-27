# amaral.27 — opções gráficas na 1.4.2 por assinatura, ThinLTO e µs/draw

Base: amaral.26 (`20a1f7a`). versionCode 30, `0.4.0-beta-amaral.27`, mesma assinatura
(certificado `0969fc07…79bf`). Vem da avaliação do SpartanPort (God of War III Remastered de
PS4 no Android): as três práticas dele que ainda faltavam aqui.

## 1. Opções gráficas na 1.4.2, por assinatura

Antes, **Distância de desenho**, **Cascatas de sombra do sol** e **Sombras de luzes pontuais**
eram ganchos do modo nativo da 1.4.3 (endereços fixos em `runtime/native.c`). Na 1.4.2
(recompilada) apareciam no menu sem efeito.

Agora o gerador do jogo, que roda no aparelho com o `main` do próprio jogador, procura cada
ponto pela assinatura (`tools/patches.py` `render_sites`): a instrução do ponto e as
vizinhas que a 1.4.3 tem (as guardas do modo nativo). Só aplica com **exatamente um**
encontro no código. Zero ou mais de um: a opção fica desligada nessa versão e o log da
compilação do jogo diz `[patches] <nome>: N matches of its signature - left off`. Na 1.4.3
recompilada a assinatura tem de cair no mesmo endereço do modo nativo (autoverificação).

| Opção | Assinatura | Patch (C no código recompilado) |
|---|---|---|
| Cascatas de sombra do sol | `mov w6,#0xbb0` + 7 palavras vizinhas (rejunção da atualização das cascatas) | w15/w16 limitados a N antes de serem publicados |
| Sombras de luzes pontuais | `ldr w1,[x0]` entre dois `bl` e três `mov` | orçamento do pool = mín(jogo, N) |
| Distância de desenho | `ldr s0,[x19,#0x138]` + `ldr s0,[x19,#0x140]` 0x24–0x64 adiante | plano distante = mín(jogo, limite), gravado de volta |

Ficam só na 1.4.3 (nativo): o **tamanho do mapa de sombras** (o patch pula código, o que a
função recompilada não faz) e o **AA do próprio jogo** (uma palavra solta: assinatura fraca
demais, risco de acertar outro código). O menu avisa isso quando a versão não é a 1.4.3.
"Sombras" (liga/desliga) já funcionava em qualquer versão (é no renderer).

As cascatas e as sombras pontuais são passes de sombra gravados pela thread de render: menos
passes, menos draws — o gargalo medido no Odin. **Desligadas por padrão**: ligue em Vídeo e
meça.

## 2. ThinLTO no `libmain`

`-flto=thin` no runtime inteiro (`TOTK_LTO`, Gradle `-PtotkLto=false` desliga). No
SpartanPort, LTO + PGO baixaram a CPU por draw da thread de comandos em 1,8% / 4,8%. As
tabelas montadas por seção (`hle_impls`, `services`, `resets`) ficaram idênticas às da .26;
as funções que o jogo recompilado chama continuam exportadas. `libmain.so` 4,33 → 4,50 MB.

## 3. µs de CPU por draw na thread de render

Nova linha no `[diag]`, abaixo da thread `render`:
`per draw: X us of its own CPU (N draws, M a frame)`. Ao contrário do FPS, ela se mantém
entre cenas com mais ou menos draws: é a métrica para comparar builds (LTO ligado/desligado,
PGO, qualquer mudança no renderer).

## Atualização

- Instala por cima da .24/.25/.26.
- O código do jogo é **recompilado** na primeira abertura (o `patches.py` mudou). No log da
  compilação, procure as linhas `[patches]` para ver quais opções foram encontradas na sua 1.4.2.

## Teste no Odin

1. Abrir, deixar recompilar, conferir as linhas `[patches]`.
2. "Medir desempenho" 60 s num trecho de floresta, padrão. Depois o mesmo trecho com
   Cascatas = 2 e Sombras de luzes pontuais = 2; depois com Distância de desenho = 8 km.
   Comparar FPS reais, GPU por quadro, draws por quadro e a linha `per draw`.

## Validação

- `tools/test_render_sigs.py` (novo): encontra a assinatura única, rejeita iscas, duplicadas
  e quebradas, confere o endereço da 1.4.3 e compila o C gerado contra `runtime/cpu.h`.
- `test_translate_ops.sh` 36/0, `test_diag.c`, `test_native_fg_policy.py`,
  `test_android_log_policy.py`.
- Build assinado, versionCode 30. Aparelho: pendente. A busca nunca rodou num `main` real da
  1.4.2 (não tenho o arquivo): o log da compilação no seu Odin é a primeira prova.
