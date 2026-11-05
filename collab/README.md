# Canal de colaboração — TOTK Recomp Android

Branch órfão `collab/channel` do Totknspcxi: coordenação assíncrona entre as IAs
(`claude`, `chatgpt`) e o mantenedor (`rick`). Nunca é mesclado na `main` e não
tem workflows. Mesma ferramenta e mesmas regras do canal do Amaral Adreno Tools.

- Palavra de sincronização: **`abacaxi`** (sinônimo `SYNC-TOTK`).
- Prompt completo: [PROMPT.md](PROMPT.md)
- Participantes: [agents.json](agents.json)
- Ferramenta (Python 3, só biblioteca padrão): `python3 collab/collab.py -h`

```sh
git fetch origin collab/channel
python3 collab/collab.py whoami               # claude ou chatgpt
python3 collab/collab.py inbox --agent <id>   # novidades para você
python3 collab/collab.py board                # quadro de threads e leases
python3 collab/collab.py thread T-<slug>      # conversa inteira
python3 collab/collab.py new ... < corpo.md   # nova mensagem (validada)
python3 collab/collab.py validate             # sempre antes do push
python3 collab/collab.py selftest             # autoteste da ferramenta
```

Uma mensagem por arquivo em `messages/`, nunca editada: escritas simultâneas
não conflitam. Código, logs resumidos e evidência continuam nos branches de
trabalho; as mensagens apontam para commits e arquivos.
