# TOTKDroid

[English](README.md) | **Português**

Um porte nativo para Android de *The Legend of Zelda: Tears of the Kingdom* **1.4.3**, com recompilação estática opcional.
O código ARM64 do jogo roda no celular e os gráficos (NVN) são traduzidos para Vulkan. Este fork também mantém
a **1.4.2** e a **1.0.0** experimental, que rodam apenas recompiladas.

> [!NOTE]
> A **1.0.0** (o jogo base sozinho, importado sem update) é um experimento nas mesmas condições da 1.4.2 abaixo, e nunca
> rodou neste app antes.
>
> A **1.4.2** fica ao lado da 1.4.3, com arquivos do jogo, saves e configurações próprios (Arquivos do jogo > Versão do
> jogo). Ela roda só com o código do jogo recompilado, a no máximo 30 fps, sem os ajustes da 1.4.3 (os endereços deles
> são da 1.4.3): abaixo de 30 fps o jogo fica mais lento que o tempo real, e a cena pode escurecer por um instante
> quando muda de tamanho. Os saves da 1.4.2 abrem na 1.4.3.

Feito sobre o motor do **[LAS Recomp](https://github.com/peiikai/LASRecomp)**, de peiikai (uma recompilação de
*Link's Awakening*), do seu porte para Android **[LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)** e do
**[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid)**, de onde este repositório saiu.

Baseado no upstream **TOTKDroid** (antes TOTKRecompAndroid), sincronizado até `6b785c2`.
Este fork mantém importação NSP/XCI, diagnósticos, geração de quadros e o recompilador no celular.
O id Android continua `com.totkrecompandroid`; atualizar uma instalação também exige a mesma chave de assinatura.

> [!WARNING]
> **Resultados da beta upstream; esta integração ainda precisa de teste no aparelho.** No celular (com o driver Turnip) um jogo novo ou um save vão do prólogo à caverna e à Grande Ilha Celestial;
> a maior parte do mapa ainda não foi jogada. Com Vídeo > Taxa de quadros 60, um POCO F7 Ultra frio segura 60 fps no
> prólogo e 54-57 no bosque da ilha; em sessões longas o calor do celular baixa isso para ~45. O plano e o estado de
> cada fase estão em [docs/TOTK-PORT.md](docs/TOTK-PORT.md). Versões: [Releases](https://github.com/mamcjr/TOTKDroid/releases).

## Plataforma

Somente Android (arm64, Vulkan). Windows, Linux ou macOS servem como máquina de desenvolvimento, com o SDK/NDK do
Android.

## Estado

- O app (tela inicial, configurações, controles de toque, controles físicos, mods, escolha do driver Vulkan) vem do
  BOTWRecompAndroid e compila.
- A 1.4.3 roda sobre o SDK 20.5.22 da Nintendo, a mesma linha do 20.5.6 do BOTW 1.8.2 (NVN 55): o que o motor aprendeu
  com o BOTW 1.8.2 vale. Os patches do jogo e muito do que se sabe dele vêm do
  [MacNativeGames](https://github.com/ly05010419/MacNativeGames), que roda exatamente esta versão nativa em Macs e
  iPhones.
- A 1.4.3 usa execução nativa por padrão; o modo recompilado continua selecionável. A 1.4.2 e a 1.0.0 usam
  recompilação, salvo override explícito de desenvolvimento. O compilador continua incluído por padrão
  (`-Ptotk.gameBuilder=false` gera um APK menor, apenas nativo).
- No celular (POCO F7 Ultra, driver Turnip): a tela título, jogo novo e saves; o prólogo, a caverna (as paredes e o
  fundo das cutscenes iluminados, como no Switch) e a Grande Ilha Celestial, com o HUD, as legendas e as vozes. A
  água, o mar de nuvens, a grama e a iluminação (sem os quadrados pretos) ficam como no Switch. O mundo anda pelo
  tempo real, então quadros acima de 16,7 ms não deixam o jogo em câmera lenta. As vozes, a música e o ambiente chegam
  à mixagem. Com o driver da própria Qualcomm também roda, mais devagar.
- Taxa de quadros: Vídeo > Taxa de quadros tem 30, Auto (prefere 60, cutscenes em 30) e 60. A resolução dinâmica do
  jogo fica desligada; a cena é 1600x896, escalada para a saída (FSR 1, GSR, SMAA / FXAA).
- Vídeo > Proporção > Desbloqueado: o mundo e os menus ocupam a tela larga (o HUD fica nos 16:9 do meio).
- Falta: a maior parte do mapa, 60 fps sustentados com o celular quente, o HUD nas bordas da tela no modo largo.

> [!IMPORTANT]
> Como o LAS Recomp, este projeto é feito quase inteiramente com IA (Claude, da Anthropic, pelo Claude Code), dirigido,
> testado e revisado por uma pessoa.

> [!NOTE]
> **Nenhum arquivo do jogo vem incluído.** É preciso o seu próprio *Tears of the Kingdom*, obtido legalmente, com a
> atualização 1.4.3 (ou 1.4.2): uma pasta extraída (`exefs` e `romfs`, com o update aplicado) ou o NSP/XCI base e o NSP desse update
> do seu próprio console, junto com as chaves desse console (veja
> [Importar a partir de NSP/XCI](#importar-a-partir-de-nspxci)). Não serão dadas instruções de como obter os arquivos
> ou as chaves.

## Importar a partir de NSP/XCI

O app (e a ferramenta `nximport` no PC) monta o jogo direto do NSP ou XCI do jogo base e do NSP do update
(`0100F2C0115B6800`: a 1.4.3 é a v720896, a 1.4.2 a v655360), usando as `prod.keys` (e, quando um pacote não tem ticket, as `title.keys`) do **seu
próprio console**. Nenhuma chave vem com este projeto nem é baixada. O jogo base sozinho é importado como 1.0.0 experimental; o update é opcional.

- **No app:** **Arquivos do jogo > Importar NSP/XCI > Pacote do jogo** e **Update**, depois **prod.keys** /
  **title.keys** e **Importar**. O setup anterior continua jogável até o novo estar completo e conferido. **Lembrar as
  chaves** guarda as chaves no armazenamento privado do app, fora de backups e da transferência entre aparelhos;
  **Apagar as chaves** apaga. **Verificar pacotes** confere cada NCA contra o SHA-256 dos metadados do pacote, útil
  quando uma importação acusa dados danificados.
- **No PC:** `tools/nximport` gera um CLI que grava `Dump/0100F2C0115B6000/{exefs,romfs}`;
  `android/build_gamelib.sh` e `android/push_game.sh` aceitam `--nsp/--xci … --update … --keys … [--title-keys …]`.
- Cada bloco lido é conferido contra os hashes do pacote (e o RomFS da atualização, já com o patch, contra os hashes
  da própria atualização), então uma importação concluída é exatamente o conteúdo do jogo. NSZ/XCZ não são suportados.

Detalhes, formatos e testes: [docs/IMPORT-NSP-XCI.md](docs/IMPORT-NSP-XCI.md). O importador é o do
BOTWRecompAndroid, com os title IDs e a versão do TOTK.

## Compilando (desenvolvedores)

Com o SDK / NDK do Android e o seu dump em `Dump/0100F2C0115B6000/` (`exefs/` e `romfs/`, ou a imagem do RomFS
`romfs.bin`; a pasta fica fora do git), com Python e os pacotes `lz4`, `capstone` e `unicorn` (o
`tools/nsp_extract.py` desempacota NSPs e as title keys deles para o hactoolnet):

```bash
cd tools && python totk_dump.py                              # confere o dump (módulos, versão, romfs)
cd android/totkrecompandroid && ./gradlew assembleDebug      # o app
ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000      # os arquivos do jogo para o celular
# opcional, só para desenvolvimento - o código recompilado (rode com LARECOMP_NATIVE=0):
# android/build_gamelib.sh Dump/0100F2C0115B6000/exefs       # ~20 min num PC, depois push_game.sh de novo
android/run_boot.sh 120                                      # inicia o jogo, log em android/out/boot.log
```

## Apoie o projeto

O TOTKDroid é um projeto gratuito e independente. Se você gosta dele e quer apoiar seu desenvolvimento, pode me
patrocinar pelo GitHub. Esse apoio me ajuda a dedicar tempo à correção de bugs, à melhoria do desempenho e da
compatibilidade e às próximas versões. O patrocínio é totalmente opcional: não libera recursos exclusivos nem garante
correções específicas. Relatos de bugs, testes em outros aparelhos e contribuições também ajudam muito.

[![Patrocine no GitHub](https://img.shields.io/badge/Patrocine-no%20GitHub-EA4AAA?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/mamcjr)

## Créditos

- [peiikai](https://github.com/peiikai) pelo [LAS Recomp](https://github.com/peiikai/LASRecomp), o recompilador e o
  runtime de onde este projeto parte (MIT, ver [LICENSE](LICENSE)). O README original está em [docs/las/](docs/las/).
- [MacNativeGames](https://github.com/ly05010419/MacNativeGames) (MIT), que roda o Tears of the Kingdom 1.4.3 nativo em
  Macs e iPhones: os sítios de patch da 1.4.3 verificados por ele (a resolução fixa, o passo de quadro em tempo real, o
  relógio de pano e cabelo, o tamanho de renderização) e o que ele descobriu (o flag de early-Z e o arredondamento da
  altura de bloco do NVN, as leituras de profundidade e a comparação de sombra D16, o contador de visibilidade).
- [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) (MIT), uma recompilação estática do Tears
  of the Kingdom 1.4.3 para Windows/Vulkan: o dimensionamento e o scissor de render passes sem anexos (luz ambiente do
  interior nas rochas da caverna), a ordem de canais dos formatos NVN empacotados de 16 bits (`RGBA4`/`RGB5`/`RGB5A1`/`RGB565`
  e `BGR565`/`BGR5`/`BGR5A1`/`A1BGR5`/`BGRX8`/`BGRA8` nos mapas de normais e mistura do terreno), a preservação de
  páginas já mapeadas em `MapPhysicalMemory` e os achados documentados da 1.4.3.
- [LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid), o seu porte para Android, e
  [BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid) (o README dele em [docs/botw/](docs/botw/)): o app,
  a compilação do código do jogo no celular, os controles de toque, os mods, a compatibilidade de drivers, os menus em
  português, o visual Sheikah e tudo o que o motor aprendeu com o SDK do BOTW 1.8.2.
- PKMSVRecompAndroid (um porte irmão, do *Pokémon Scarlet*, não publicado): as versões para um jogo de uma versão só, o
  program id lido do NPDM e os pares exclusivos de 16 bytes no recompilador.
- [Revivedx](https://github.com/Revivedx) pelo [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) (MIT):
  as correções de kernel do SDK 20, o setjmp, o NVN 55.
- [Nycolazs](https://github.com/Nycolazs) pelo LASRecomp-Android-Plus (MIT): compatibilidade de drivers Vulkan.
- A decompilação [zeldaret/botw](https://github.com/zeldaret/botw) e o [open-ead/sead](https://github.com/open-ead/sead),
  pelo que se sabe do motor de onde o TOTK veio.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (Apache 2.0, AES e SHA-256 da importação de NSP/XCI) e
  [hactool](https://github.com/SciresM/hactool), de SciresM (ISC), referência dos formatos de pacote do Switch (nenhum
  código copiado) e do `tools/nximport/verify.sh`.
- [lsfg-vk](https://lsfg-vk.dev) 1.0.0, de PancakeTAS (MIT), via [GameNative/lsfg-vk-android](https://github.com/GameNative/lsfg-vk-android)
  (MIT), com o dxbc do [DXVK](https://github.com/doitsujin/dxvk) (zlib) e o [pe-parse](https://github.com/trailofbits/pe-parse) (MIT):
  os shaders da geração de quadros, traduzidos no celular a partir do Lossless.dll do próprio usuário (nenhum incluído).
- Upscaling e anti-aliasing: [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (MIT),
  [Qualcomm Snapdragon GSR](https://github.com/SnapdragonStudios/snapdragon-gsr) (BSD-3-Clause),
  [SMAA](https://github.com/iryoku/smaa) (MIT).
- Bibliotecas: [SDL](https://libsdl.org) (zlib), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [libadrenotools](https://github.com/bylaws/libadrenotools) (BSD-2-Clause), [volk](https://github.com/zeux/volk) (MIT),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT),
  [shaderc](https://github.com/google/shaderc) (Apache 2.0), [FFmpeg](https://ffmpeg.org) (LGPL),
  [LZ4](https://github.com/lz4/lz4) (BSD-2-Clause), [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
  [Unicorn](https://www.unicorn-engine.org) (GPLv2, compilado do código-fonte por `android/deps/build_unicorn.sh`),
  [Python](https://www.python.org) (PSF) e [Zig](https://ziglang.org) (MIT) para a compilação no celular.
- O driver Turnip do [Mesa](https://www.mesa3d.org) (MIT), que os jogadores carregam por conta própria.
- Fontes: [Baloo 2](https://github.com/EkType/Baloo2) (OFL), DejaVu Sans (licença Bitstream Vera). O robô do Android
  é reproduzido ou modificado a partir de trabalho criado e compartilhado pelo Google (CC BY 3.0).
- Feito com o [Claude Code](https://claude.com/claude-code) (Anthropic).

*The Legend of Zelda* e *Tears of the Kingdom* são marcas registradas da Nintendo. Este projeto não é afiliado à
Nintendo nem endossado por ela.
