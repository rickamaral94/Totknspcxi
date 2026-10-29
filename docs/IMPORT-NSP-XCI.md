# Importar a partir de NSP/XCI (`tools/nximport`)

O resto do projeto recebe o jogo **extraído**: `exefs/` + `romfs/`. Isso vale para o app, os scripts e o
`tools/botw_dump.py`. O `nximport` é uma etapa **anterior** a esse fluxo. Ele lê os pacotes do próprio jogador (NSP ou
XCI da base e, opcionalmente, o NSP do update) com as `prod.keys`/`title.keys` do console dele e entrega os mesmos
arquivos que a extração do hactool entregaria. O recompilador, o loader e a detecção de versão não mudam.

> Nenhuma chave, pacote ou arquivo do jogo entra no repositório (`.gitignore`: `*.keys`, `*.nsp`, `*.xci`, `*.nca`,
> `*.tik`, `*.nsz`, `*.xcz`). As chaves vêm do console do próprio jogador; o projeto não diz como obtê-las.

## Onde ele se encaixa (conferido no upstream `4acbf40`)

| Peça | Hoje | Com o nximport |
| --- | --- | --- |
| Versão do jogo | build id do NSO `main` em +0x40: `runtime/loader.c` `game_version_of_build` (app: `nativeGameVersion`) e `tools/botw_dump.py` | Sem mudança. O nximport não tem lista de versões própria: só confere o title ID pelo CNMT. |
| App, "Game files" | `importGame()` (Java), depois `nativeImportBegin` (`runtime/ui/launcher.mm`), que **apaga o setup anterior na hora**, copia `exefs/` e monta o `romfs.bin` com `romfs_build_entries` | Seção nova **Importar NSP/XCI**, com fluxo transacional próprio (`package_import.cpp`). A importação por pasta continua como era. |
| Builder do `romfs.bin` | `runtime/romfs_build.c`: `fdopen` + `fread` sequencial de `entries[i].size` bytes, depois `fgetc == EOF`. Sem seek, sem fstat. Três tentativas por arquivo. Uma thread. | **Sem alteração**, alimentado pela `nx::RomfsFeed` (socketpair). A prova de compatibilidade está em `tests/test_feed.cpp`. |
| Scripts | `android/build_gamelib.sh <exefs>`, `android/push_game.sh <pasta com exefs/ e romfs/>` | Também aceitam `--nsp/--xci … [--update …] --keys … [--title-keys …]`. Chamam `android/nximport_dump.sh`, que roda o CLI e depois o `tools/botw_dump.py`, e seguem o fluxo de sempre. |

Layout gerado pelo CLI: `Dump/01007EF00011E000/exefs/{main,main.npdm,rtld,sdk,subsdk*}` e
`Dump/01007EF00011E000/romfs/…`. Ele grava primeiro em `01007EF00011E000.partial` e só renomeia no fim.

## Uso (CLI de desktop)

```bash
cmake -S tools/nximport -B build/nximport && cmake --build build/nximport
build/nximport/nximport --keys prod.keys [--title-keys title.keys] --nsp base.nsp [--update update.nsp] -o Dump
build/nximport/nximport --keys prod.keys --xci jogo.xci -o Dump --verify-nca   # + SHA-256 de cada NCA
build/nximport/nximport --keys prod.keys --nsp base.nsp --list                 # só lista
```

As mensagens saem em português quando `LANG` começa com `pt`. `--programs` imprime as NCAs Program usadas, que o
`verify.sh` entrega ao hactool.

### Scripts de desenvolvedor

```bash
android/build_gamelib.sh --nsp base.nsp --update update.nsp --keys prod.keys   # importa e compila o game code
android/push_game.sh --nsp base.nsp --update update.nsp --keys prod.keys       # importa e envia ao aparelho
NXIMPORT_OUT=Dump/v160 android/push_game.sh --xci jogo.xci --update u160.nsp --keys prod.keys
```

`android/nximport_dump.sh`:
- compila o CLI em `build/nximport-host` na primeira vez;
- grava em `${NXIMPORT_OUT:-Dump}/01007EF00011E000`, e se recusa a sobrescrever um dump que já existe;
- roda o `tools/botw_dump.py` quando há um Python com lz4 (`tools/venv`). É ele quem diz a versão.

## Chaves

- **`prod.keys`**: uma por linha, `nome = hex` (`,` também é aceito como separador; `;`/`#` comentam). O importador
  usa:
  - `header_key`;
  - `key_area_key_application_XX` (ou `_ocean_`/`_system_`);
  - `titlekek_XX`.
  
  Se faltar alguma delas, ele **deriva** a partir de `master_key_XX` com `aes_kek_generation_source`,
  `aes_key_generation_source`, `key_area_key_*_source`, `titlekek_source`, `header_kek_source` e
  `header_key_source`, quando essas existirem no arquivo. O resto do arquivo é ignorado.
- **Revisão da chave de uma NCA**: `g = max(keygen_antigo [0x206], keygen [0x220])`, `rev = g ? g - 1 : 0`.
- **`title.keys`**: `rights_id (32 hex) = title key (32 hex)`. A title key fica **cifrada** com `titlekek_<rev>`,
  exatamente como no campo `title_key_block` de um ticket common.
- **Precedência da title key**:
  1. o ticket **common** (`.tik`) dos pacotes com o mesmo rights ID;
  2. a `title.keys`.

  Cada candidata é **testada** antes do uso: descriptografa o topo da árvore de hash da seção e compara com a master
  hash. Se não bater, passa para a próxima. Ticket personalizado (RSA) é ignorado: nesse caso a key precisa vir da
  `title.keys`.
- **Erros**: citam só o **nome** da chave ou do arquivo e a linha (ex.: "Falta key_area_key_application_0b: suas
  prod.keys são de um firmware mais antigo que este conteúdo"). Nunca citam o valor. O rights ID aparece só pela
  metade (o title ID).
- Na memória, as chaves ficam em `Secret<N>`, apagadas com `mbedtls_platform_zeroize`. O texto lido do arquivo também
  é apagado logo depois do parse.

## Formatos

- **NSP** = PFS0. **XCI** = header do cartucho (`HEAD` em 0x100, ou em 0x1100 em dumps com a key area na frente),
  HFS0 raiz e partição `secure`. Não precisa de chave do cartucho.
- **NCA**:
  - Header de 0xC00 em AES-128-XTS (`header_key`, setores de 0x200, tweak = número do setor em **big-endian**).
    Na NCA3, os quatro FS headers são os setores 2-5; na NCA2, cada um é um setor 0.
  - Seções em AES-128-CTR. O contador é FS header 0x140..0x147 (invertido) mais `offset_na_NCA >> 4` em big-endian.
  - A chave vem da key area (índice 2, cifrada com `key_area_key_*_<rev>`) ou da title key.
  - O hash de cada FS header é conferido contra o header da NCA.
- **Conteúdo pelo CNMT**:
  - A base precisa de um CNMT `Application` de `01007EF00011E000`. O update, de um `Patch` de `01007EF00011E800`
    cujo `application_id` é o da base.
  - A NCA Program é a do **content ID** listado no CNMT, conferida pelo tamanho; com `--verify-nca`, também pelo
    SHA-256.
  - Mais de uma base ou update no mesmo pacote é recusado, por ser ambíguo.
- **Recusas explícitas**: NSZ/XCZ/NCZ, seções esparsas ou comprimidas, criptografia de seção XTS ou desconhecida,
  ticket personalizado sem `title.keys`.

## Integridade (o que prova que a saída está certa)

| Onde | Conferência |
| --- | --- |
| NCA header | SHA-256 de cada FS header contra a tabela do header |
| exefs (PFS0) | HierarchicalSha256 completo: master hash → tabela → **todos** os blocos |
| romfs | IVFC completo: master hash → níveis 1-5 (em memória, ~32 MiB por 16 GiB) → **cada bloco do nível 6** lido. O bloco parcial no fim é aceito com hash sobre os bytes ou com padding de zeros. |
| NCA inteira (opcional) | SHA-256 contra o registro do CNMT e o content ID (`--verify-nca`) |

Para o update, a árvore IVFC da NCA patch cobre o RomFS **já com o patch**. Por isso a verificação dela é a
prova independente de que IndirectStorage + AesCtrEx reconstruíram o conteúdo da Nintendo. Ela não depende dos
nossos geradores de teste.

### Update (BKTR)

A seção RomFS da NCA Program do update (criptografia AesCtrEx, tipo 4) traz no fim duas árvores de buckets. Os
offsets e tamanhos delas estão no patch info do FS header (0x100 e 0x120):

- **IndirectStorage**: para cada faixa da seção virtual (já com o patch), diz se os bytes vêm da seção RomFS **da
  base** (storage 0, lida decifrada mas sem conferir os hashes da base) ou dos dados **do update** (storage 1), e em
  qual offset. Entrada de 0x14 bytes: virtual, físico, storage.
- **AesCtrEx**: para cada faixa dos dados do update, a *generation* que o contador AES-CTR usou. Ela substitui os
  bytes 4-7 do contador; os bytes 0-3 continuam sendo o secure value do FS header. Entrada de 0x10 bytes: offset,
  cifrado (0) ou não (1), generation. As próprias tabelas usam o contador do FS header.
- **Layout da árvore** (um nível):
  - um nó de 0x4000 bytes: `{índice, nº de conjuntos, offset final, início de cada conjunto}`;
  - em seguida, os conjuntos de 0x4000 bytes: `{índice, nº de entradas, offset final, entradas}`.
- **Validação estrutural**:
  - magic `BKTR` v1, e o nº de entradas igual ao do FS header;
  - início e fim de cada conjunto coerentes com o nó;
  - offsets começando em 0 e estritamente crescentes até o fim;
  - storage só 0 ou 1; faixas AesCtrEx alinhadas a 16 e terminando antes das tabelas;
  - cada faixa indireta dentro da base ou dos dados do update.
  
  Árvores de dois níveis são recusadas com mensagem clara (não foram vistas em patches de jogos).
- **Prova**: o RomFS reconstruído é lido pela árvore IVFC **do update**, cujos hashes cobrem o resultado já com o
  patch. Uma base de outra versão ou um patch mal aplicado falha com "falha na verificação de integridade … um update
  que não é deste jogo base".

### Parsers defensivos

- Offsets e tamanhos em 64 bits, com soma e multiplicação checadas.
- Toda região é conferida contra o tamanho do pai, e todo arquivo truncado é recusado.
- Limites de quantidade de entradas, tabela de nomes e memória.
- Nomes: recusa `/`, `\`, `.`, `..`, NUL e duplicados.
- RomFS: ciclos detectados por conjunto de offsets visitados, mais profundidade máxima.

## Testes

```bash
cmake -S tools/nximport -B build/nximport-asan -DNXIMPORT_SANITIZE=ON && cmake --build build/nximport-asan
ctest --test-dir build/nximport-asan --output-on-failure
```

- **KAT**: FIPS-197 (AES), SP 800-38A F.5.1 (CTR), IEEE 1619 vetores 1-2 (XTS), FIPS 180-2 (SHA-256), mais o tweak
  big-endian da Nintendo contra o XTS cru.
- **Chaves**: parse, derivação, fórmula da revisão, mensagens (sem valores).
- **Contêineres**: PFS0/HFS0/XCI (com e sem key area), nomes e limites inválidos, NSZ recusado.
- **RomFS**: listagem, ciclos, nomes e dados fora da imagem.
- **Importação ponta a ponta** com pacotes sintéticos (chaves aleatórias do teste):
  - NSP, XCI, NCA2 e keygen 3;
  - title key via ticket, via `title.keys`, com fallback e com as duas erradas;
  - ticket personalizado, outro jogo, NCA ausente, `header_key` errada, `key_area_key` ausente, cancelamento.
- **Update**: base + update NSP e base XCI + update com title key. Os pacotes sintéticos usam entradas indiretas que
  apontam para a base em outro offset, faixas AesCtrEx com várias generations (uma em cada três sem criptografia) e
  mais de 818 entradas (vários conjuntos). Também cobrem a base errada, os pacotes trocados, árvores malformadas
  (ordem, início, storage, contagem, conjunto, truncagem, magic) e 1.200 bit flips no update.
- **Corrupção**: 3.000 bit flips (headers densamente, depois em qualquer lugar) e truncamentos progressivos. Cada caso
  precisa falhar com erro ou extrair **exatamente** os arquivos originais.

Os testes sintéticos não bastam sozinhos para dar o BKTR por validado: o gerador e o leitor podem repetir o mesmo
erro. A validação final é o `verify.sh` com o hactool sobre os arquivos do próprio jogador:

```bash
tools/nximport/verify.sh --keys prod.keys --nsp base.nsp [--update update.nsp] [--hactool /caminho/hactool]
```

- O script extrai com o nximport e com o hactool, e depois compara a **lista de caminhos, os tamanhos e o SHA-256**
  de cada arquivo. Pastas vazias diferentes viram só um aviso.
- Saída 0: idêntico. Saída 1: diferente (mostra as primeiras diferenças e guarda a pasta de trabalho). Saída 2:
  **"COMPARAÇÃO DE REFERÊNCIA NÃO EXECUTADA"**, quando não há hactool. Isso **não** é aprovação.
- O hactool lê as title keys de `~/.switch/title.keys`, então conteúdo com rights ID precisa delas lá.
- A pasta de trabalho guarda duas extrações (~2 × 16 GB) e é apagada no fim (`--keep` mantém).
- A lógica de comparação foi testada com um substituto do hactool (idêntico → 0; um byte a mais → 1). A comparação
  com o hactool de verdade ainda não foi feita.

`nximport_tests --write-sample <pasta> [MiB]` grava um `base.nsp` sintético, um `update.nsp` com patch BKTR sobre ele
e o `prod.keys` de teste (chaves aleatórias, nada da Nintendo), para exercitar o CLI e o `verify.sh` à mão.

## App Android

### Na tela: Game Files, seção "Importar NSP/XCI"

- **Campos**:
  - Pacote do jogo (`.nsp`/`.xci`);
  - Update (`.nsp`, opcional);
  - `prod.keys`, que aparece como "Lembradas" quando o app guardou as chaves;
  - `title.keys` (opcional).
  
  Cada um abre o seletor do Android (SAF, `ACTION_OPEN_DOCUMENT`). Cancelar o seletor de um campo opcional o limpa.
- **Lembrar as chaves** (ligado por padrão): guarda as chaves em `getNoBackupFilesDir()/keys`. Esse é o
  armazenamento interno privado (só o app lê), fora do Auto Backup. `res/xml/data_extraction_rules.xml` e
  `backup_rules.xml` também excluem esse diretório do backup na nuvem e da transferência entre aparelhos.
  `allowBackup="false"` sozinho não cobre a transferência entre aparelhos no Android 12+, e o resto do app continua
  como era.
  - Desligado: as chaves são lidas só nesta importação e nada vai para o disco.
  - **Apagar as chaves** remove a cópia.
  - O Java nunca transforma uma chave em `String`: copia bytes de um descritor para um arquivo (escrito como `.tmp`
    e renomeado) ou passa o próprio descritor.
- **Importar**: os pacotes são lidos **onde estão**, por descritor (`openFileDescriptor(...).detachFd()`).
  - Se o provedor não permite leitura em posições aleatórias (nuvem, streaming), o app diz qual arquivo é e oferece
    copiá-lo antes para o cache externo. A cópia precisa da confirmação do jogador, mostra o tamanho e o espaço
    livre, e é apagada no fim. Se o app for morto, ela é apagada no próximo início.
- **Progresso**: aparece no overlay de sempre, com o botão **Cancelar** (toque, B ou Esc), e na notificação do
  `BuildService` ("Importando os arquivos do jogo").
  - O serviço sabe em que fase está, então o fim da importação não derruba a compilação do game code que começa
    logo depois.
  - `onTimeout` (Android 15+, limite de 6 h/24 h para `dataSync`) cancela a importação de forma limpa e tira o
    serviço do foreground.

### Instalação transacional (`android/totkrecompandroid/app/src/main/cpp/package_import.cpp`)

1. Abre os pacotes. Um descritor sem acesso aleatório é recusado antes de qualquer outra coisa.
2. **Versão**: `game_version_of_build` sobre o `main` importado (a decisão é do loader).
3. `romfs_check_set` da versão, igual ao da pasta.
4. **Espaço**: `romfs.bin` com limite superior pelo layout do builder, mais os filmes soltos, o `exefs/` e uma
   margem de max(512 MiB, 2%), contra o `statvfs` da raiz da versão. A mensagem traz o valor exato.
5. Grava em `<raiz da versão>/extracted.importing/`, no mesmo sistema de arquivos: `exefs/`, depois o `romfs.bin`
   via `RomfsFeed` (com os filmes soltos em `romfs/`).
6. **Confere**: `romfs_image_ok` e a versão do `main` gravado. Aí escreve `COMPLETE` com fsync.
7. **Promove**: remove os arquivos do setup anterior (`rt_remove_game_files_for_setup`; os objetos compilados
   ficam) e renomeia `exefs/`, `romfs/` e `romfs.bin` para `extracted/`.
8. O fluxo segue como o de uma pasta escolhida: o game code (ou o modo nativo), depois a checagem final.

Cancelamento, falha ou o app morto antes do `COMPLETE` deixam o setup anterior **intacto**. No próximo início,
`package_import_recover` apaga um staging incompleto ou termina uma promoção interrompida (cada passo pode ser refeito).

Testes de host (`tests/test_package_import.cpp`, com o `romfs_build.c` de verdade e substitutos pequenos de
`paths.c`/loader):
- import sobre um setup antigo, com o mesmo `romfs.bin` que a pasta gera;
- cancelamento e falha de integridade preservando o setup antigo;
- descritor sem seek;
- versão desconhecida;
- chaves erradas;
- a checagem real de arquivos recusando um RomFS incompleto;
- recuperação de staging incompleto, completo e com promoção interrompida.

## Dependência

- **mbedTLS 3.6.7** (LTS), submodule em `third_party/mbedtls`, commit `068ff080b3`. Licença Apache-2.0, a opção que
  adotamos da dupla Apache-2.0/GPL-2.0-or-later.
- Compilamos só `aes.c`, `aesce.c`, `aesni.c`, `sha256.c` e `platform_util.c`, com `tools/nximport/mbedtls_config.h`.
- AES e SHA-256 usam as extensões ARMv8 no aparelho e AES-NI no host x86-64, escolhidas em tempo de execução.
- O hactool (SciresM, ISC) foi só referência de formato. Nenhum código de yuzu, Ryujinx ou LibHac foi usado.

## Estado

| Fase | Conteúdo | Estado |
| --- | --- | --- |
| 1 | CLI com base (NSP/XCI, chaves, NCA, integridade, romfs, testes) | **feita** |
| 2 | Update (BKTR: IndirectStorage + AesCtrEx), `verify.sh`, scripts | **feita**: BKTR implementado; **falta validar** com arquivos reais e o hactool |
| 3 | App Android (SAF, instalação transacional, serviço, chaves) | **feita**: compila (NDK e javac); **falta** a build do APK e o teste no aparelho |
| 4 | APK compilado e importação completa no aparelho | pendente |
