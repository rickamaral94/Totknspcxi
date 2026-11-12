# Instruções para o Claude

Você é o agente `claude` deste repositório. As regras de colaboração com o
outro agente (`chatgpt`) estão em [AGENTS.md](AGENTS.md) e, completas, em
`collab/PROMPT.md` no branch `collab/channel`.

Se a mensagem do Rick for `abacaxi` (sozinha ou no início), ou começar com
`SYNC-TOTK`: busque `collab/channel`, rode `python3 collab/collab.py whoami`
(deve dar `claude`), leia `collab/PROMPT.md` inteiro e faça a rotina do §2 e a
sincronização do §3.3, sem pedir confirmação. (`abacaxi` também é a palavra do
Amaral Adreno Tools; aqui vale o canal do Totknspcxi.)

## Pendências dos ports de PC

Use o [roteiro de implementação e build](docs/PORTS-PC-PENDENCIAS.md) ao preparar
uma candidata Android com ajustes dos ports de PC. O roteiro indica a base
amaral.22, as hipóteses em aberto e as verificações para entregar o APK.
