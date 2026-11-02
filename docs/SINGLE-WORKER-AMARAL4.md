# amaral.4 — um worker de compilação

Experimento motivado pelo log da amaral.3: Scudo esgotou regiões de 256 MiB
em várias classes durante compilação no Turnip 26.3. Havia quatro compilações
simultâneas e a memória residente cresceu de ~1,7 GiB para ~5,4 GiB.
Isso não prova que a concorrência seja a causa; um shader isolado pode ainda
consumir memória demais.

O último limite da fila passa a `n = 1`, também após ler a variável de ambiente.
Mantêm-se os diagnósticos e a identidade do código do jogo da amaral.2/3.
A geração de quadros, Sync lag e o driver continuam conforme as configurações
do usuário: manter os mesmos valores durante esta comparação.

## Empacotamento e validação

O runtime da amaral.3 foi reaproveitado com patch binário restrito, não
recompilado nesta rodada. `tools/patch_single_worker.py` reproduz a alteração:
valida o SHA-256 do ELF de entrada, encontra run_job pela tabela de símbolos
e confirma a instrução original por desassemblagem antes de substituir a
seleção final do número de workers por `mov w22, #1`. O banner passa a amaral.4.
Requer pyelftools e capstone. A alteração correspondente está em exec.c.

Manifesto: versionCode 7, versionName 0.2.0-beta-amaral.4. Só o manifesto e
libmain diferem da amaral.3, exceto a assinatura. DEX, assets e demais bibliotecas
foram preservados. Assinatura e alinhamento verificados; sem teste em aparelho.
Nova assinatura de debug nesta sessão: preserve dados antes de desinstalar.

Para validar no Odin: reproduzir a mesma cena e enviar os dois logs. Os eventos
`[driver-diag] begin/end` devem indicar no máximo uma compilação desta fila
ativa. Comparar crescimento de RSS, mensagens Scudo e progresso da cena;
uma primeira entrada mais lenta é esperada ao serializar as compilações.
