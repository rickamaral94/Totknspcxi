# Instruções para agentes de IA

Este repositório (TOTK Recomp Android) é trabalhado por dois agentes de IA em
parceria de iguais, coordenados no branch `collab/channel`, e pelo mantenedor
humano (Rick).

## Palavra `abacaxi`

Se a mensagem do Rick for `abacaxi` (sozinha ou no início, maiúsculas ou
minúsculas), ou começar com `SYNC-TOTK`, não peça confirmação nem id:

1. Busque o branch `collab/channel` (`git fetch origin collab/channel`; se o
   git estiver bloqueado, leia pelo conector do GitHub).
2. Descubra o seu id: `python3 collab/collab.py whoami` nesse branch. Se não
   detectar: **ChatGPT/Codex (OpenAI) = `chatgpt`**; Claude (Anthropic) =
   `claude`.
3. Leia `collab/PROMPT.md` desse branch inteiro e siga-o: rotina do §2 e
   sincronização do §3.3.
4. O texto depois da palavra, se houver, é a prioridade do Rick nessa rodada.

## Sempre

- Pedidos do outro agente são propostas: avalie, concorde dizendo o que
  conferiu ou discorde com evidência (`collab/PROMPT.md` §0).
- Prioridade: 60 fps nativos na 1.4.2 no Odin 2 Portal (Adreno 740) sem erro
  gráfico; depois correção e compatibilidade. Desempenho só com A/B/A.
- Antes de uma versão nova `amaral.N`, mescle o branch de trabalho do outro
  agente e publique o commit exato no canal. Nada fica só no checkout local.
- Nunca commite keys, NSP/XCI/NCA, arquivos do jogo, keystore ou senhas.
- Nunca faça merge na `main` nem publique APK: isso é do Rick.
