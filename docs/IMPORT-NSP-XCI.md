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
| App, "Game files" | `importGame()` (Java), depois `nativeImportBegin` (`runtime/ui/launcher.mm`), que **apaga o setup anterior na hora**, copia `exefs/` e monta o `romfs.bin` com `romfs_build_entries` | Fluxo novo e transacional (Fase 3): staging, validação e promoção. |
| Builder do `romfs.bin` | `runtime/romfs_build.c`: `fdopen` + `fread` sequencial de `entries[i].size` bytes, depois `fgetc == EOF`. Sem seek, sem fstat. Três tentativas por arquivo. Uma thread. | Alimentado por socketpair (Fase 3; precisa da prova de compatibilidade antes). |
| Scripts | `android/build_gamelib.sh <exefs>`, `android/push_game.sh <pasta com exefs/ e romfs/>` | Aceitam `--nsp/--xci … --keys …` e chamam o CLI antes (Fase 2). |

Layout gerado pelo CLI: `Dump/01007EF00011E000/exefs/{main,main.npdm,rtld,sdk,subsdk*}` e
`Dump/01007EF00011E000/romfs/…`. Ele grava primeiro em `01007EF00011E000.partial` e só renomeia no fim.

## Uso (CLI de desktop)

```bash
cmake -S tools/nximport -B build/nximport && cmake --build build/nximport
build/nximport/nximport --keys prod.keys [--title-keys title.keys] --nsp base.nsp [--update update.nsp] -o Dump
build/nximport/nximport --keys prod.keys --xci jogo.xci -o Dump --verify-nca   # + SHA-256 de cada NCA
build/nximport/nximport --keys prod.keys --nsp base.nsp --list                 # só lista
```

As mensagens saem em português quando `LANG` começa com `pt`.

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

Para o update (Fase 2), a árvore IVFC da NCA patch cobre o RomFS **já com o patch**. Por isso a verificação dela é a
prova independente de que IndirectStorage + AesCtrEx reconstruíram o conteúdo da Nintendo. Ela não depende dos
nossos geradores de teste.

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
- **Corrupção**: 3.000 bit flips (headers densamente, depois em qualquer lugar) e truncamentos progressivos. Cada caso
  precisa falhar com erro ou extrair **exatamente** os arquivos originais.

Os testes sintéticos não bastam sozinhos para dar o BKTR por validado: o gerador e o leitor podem repetir o mesmo
erro. A validação final é o `verify.sh` com o hactool sobre os arquivos do próprio jogador (Fase 2).

`nximport_tests --write-sample <pasta> [MiB]` grava um `base.nsp` sintético e o `prod.keys` de teste (chaves
aleatórias, nada da Nintendo), para exercitar o CLI à mão.

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
| 2 | Update (BKTR: IndirectStorage + AesCtrEx), `verify.sh`, scripts | pendente |
| 3 | App Android (SAF, instalação transacional, serviço, chaves) | pendente |
| 4 | APK compilado e importação completa no aparelho | pendente |
