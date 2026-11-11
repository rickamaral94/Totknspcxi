# Instruções para o Claude

Você é o agente `claude` deste repositório. As regras de colaboração com o
outro agente (`chatgpt`) estão em [AGENTS.md](AGENTS.md) e, completas, em
`collab/PROMPT.md` no branch `collab/channel`.

Se a mensagem do Rick for `abacaxi` (sozinha ou no início), ou começar com
`SYNC-TOTK`: busque `collab/channel`, rode `python3 collab/collab.py whoami`
(deve dar `claude`), leia `collab/PROMPT.md` inteiro e faça a rotina do §2 e a
sincronização do §3.3, sem pedir confirmação. (`abacaxi` também é a palavra do
Amaral Adreno Tools; aqui vale o canal do Totknspcxi.)

## Histórico de testes

Ao entrar para avaliar o TOTK, abra [docs/logs/README.md](docs/logs/README.md)
e leia os registros ligados ao sintoma ou à versão em revisão. Confira o APK,
a versão do jogo e as condições da coleta antes de comparar números.

Se uma conclusão estiver em aberto, proponha um teste que diferencie as
hipóteses. Registre a revisão ou o novo resultado no branch de trabalho e
referencie o caminho e o commit na thread correspondente de `collab/channel`.
Preserve as observações anteriores quando uma nova evidência mudar a conclusão.
