# Plano: TOTKRecompAndroid

Recompilação estática de *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, **1.4.3** desde 2026-10-25;
até então a 1.4.2 - as seções abaixo sobre o dump e as fases 0-5 foram escritas para ela) para Android, a partir do motor do [LAS Recomp](https://github.com/peiikai/LASRecomp), do seu
porte para Android ([LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)) e do
[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid), de onde este repositório saiu (com o histórico dos
dois). Como neles, não é emulação: o código ARM64 do jogo vira C, compilado para o celular, e o NVN (a API gráfica do
Switch) é traduzido para Vulkan.

O plano e as medições do BOTW estão em [docs/botw/BOTW-PORT.md](botw/BOTW-PORT.md); os do LAS para Android em
[docs/las/ANDROID-PORT.md](las/ANDROID-PORT.md). Muito do que o motor sabe hoje veio de lá (SDK 20, NVN 55, afinidade
de núcleos, ciclo de vida das texturas, render targets lineares, samplers, oclusão): está anotado onde foi feito.

## A descoberta que muda tudo: o SDK é o do BOTW 1.8.2

O `sdk` (nnSdk 20.5.6, com o NVN e o vi dentro) e o `subsdk0` (`multimedia.nss`) do TOTK 1.4.2 são **os mesmos
arquivos, byte a byte,** que os do BOTW 1.8.2 (builds `5cf080e8...` e `3308c1d2...`). O `main.npdm` pede os mesmos 49
serviços. Tudo o que o BOTWRecompAndroid já faz rodar no 1.8.2 - kernel do SDK 20 (variáveis de condição, setjmp,
`UserExceptionContextAddress`), os serviços, o NVN 55.15 (`nvnDeviceGetInteger`), o `nn::movie` com FFmpeg - vale
para o TOTK sem mudança. O que é novo é só o `main`: o jogo.

## O dump (medido em 2026-10-24 com `tools/totk_dump.py`)

O jogo `0100F2C0115B6000` 1.0.0 (NSP de 17 GB em `LASRecompAndroid/ROM/`) com a atualização `0100F2C0115B6800`
**v655360 = 1.4.2** (`Updates_Switch/`). O hactoolnet de 2019 não abre NSP: os NSPs (PFS0) foram desempacotados por
um script de 40 linhas, as title keys tiradas dos tickets (`.tik`, 0x180 / 0x2A0), e depois:

```
hactoolnet --titlekeys title.keys -t nca <NCA do programa da atualização> --basenca <NCA do programa do jogo> \
    --exefsdir Dump/0100F2C0115B6000/exefs --romfsdir Dump/0100F2C0115B6000/romfs
```

(o "Master Hash (FAIL)" da seção BKTR do patch não impede nada, como no Scarlet). `Dump/` fica fora do git.

| Módulo | Imagem | Símbolos | Relocações | O que é | Tratamento |
|---|---|---|---|---|---|
| `main` | 58,8 MiB (texto 43,6) | 706 | 501.144 | o jogo (`EX-King.nss`, build `Product_CSPGO_Optimize`): o motor da EPD depois do BOTW (sead, agl, gsys), Havok 2022.1 (AI, Animation, Cloth), NintendoWare (G3d2 e Vfx2 20.5.6, Ui2d e Font 15.3.1, Meshoptimizer) | recompilado |
| `sdk` | 12,3 MiB | 23.039 | 34.034 | nnSdk **20.5.6** com NVN e vi - o do BOTW 1.8.2 | recompilado + serviços HLE; NVN pelo nome |
| `subsdk0` | 6,4 MiB | 10.110 | 19.072 | `multimedia.nss` (nn::movie) - o do BOTW 1.8.2 | HLE (FFmpeg) |
| `rtld` | — | — | — | carregador dinâmico | não usado |

- Build id do `main` 1.4.2: `5cb42b1cf25469fb0635fd046453d843c18bc8ab` (o próprio romfs o repete em
  `System/RegionLangMask.txt`).
- `main.npdm`: programa `0100F2C0115B6000`, espaço de endereçamento de 39 bits (tipo 3), thread principal prioridade
  44 no núcleo 0, pilha 1 MiB, system resource 24 MiB.
- O `main` (58,8 MiB) cabe nos 64 MiB entre `MAIN_BASE` e `SDK_BASE` (`0x7004000000`) com 5 MiB de folga: o mapa de
  memória do runtime fica como está.
- `romfs`: **304.243 arquivos**, 15,9 GiB (o do BOTW 1.0.0 tinha 39.678). Quase tudo em Zstandard (`.zs`, com os
  dicionários em `Pack/ZsDic.pack.zs`), que o próprio jogo descomprime; as mensagens de cada idioma em `Mals/` - são 15
  regiões-idioma, **português do Brasil incluído** (`USpt`), como no BOTW 1.8.2; shaders em `Shader/*.bfsha.zs`; áudio
  em `Sound/Resource/*.bars.zs`; 28 vídeos WebM (1,5 GB) em `Event/Movie/` - os únicos arquivos que o runtime abre pelo
  caminho (`hle/movie.c`; o `keep_loose` do launcher já os separa pelo `.webm`).

## 1.4.3 e o MacNativeGames (2026-10-25)

O [MacNativeGames](https://github.com/ly05010419/MacNativeGames) roda o TOTK 1.0.0, 1.2.1 e **1.4.3** nativo no Mac,
iPhone e iPad (o código ARM64 do jogo direto no chip da Apple, NVN em Metal pelo tradutor do Ryujinx) e documenta o
que achou (`docs/how-it-works.md`, `docs/v1.4.3-port.md`, `games/totk/runtime/game_patches_v143.c`). Por isso o porte
passa para a **1.4.3**: os endereços e as instruções originais dos patches dele valem aqui.

- **O dump:** atualização `0100F2C0115B6800` **v720896** (`Updates_Switch/`). `tools/nsp_extract.py` desempacota os
  NSPs e tira as title keys dos tickets (só o NCA de programa do jogo base, 17 GB, com `--only-largest`); depois o mesmo
  `hactoolnet ... --basenca ... --exefsdir Dump/v143/exefs --romfs Dump/v143/romfs.bin`. Os SHA-256 do ExeFS e do
  RomFS (17.052.625.232 bytes, `797e66f2...`) são **os mesmos** que o MacNativeGames publica
  (`games/totk/assets/expected-v1.4.3.json`).
- **Tudo mudou de 1.4.2 para 1.4.3:** `main` (build `277178b7dba1b6d4`, +0x13A10 de texto, compilador novo), e também
  `sdk`, `subsdk0`, `rtld` e o npdm: **SDK 20.5.22** (era o 20.5.6 do BOTW 1.8.2), +27 símbolos no `sdk`. Os 24 sítios
  de patch do MacNativeGames para 1.4.3 batem um a um com o nosso `main`; no 1.4.2 nenhum estava no mesmo endereço
  (o compilador mudou: alguns grupos aparecem deslocados, outros não).
- **Código:** `gen.py` sem mudança (`sdk`: 40.516 funções, 1 instrução sem suporte - dados lidos como código, como
  antes; `main`: 228.238 funções, 0 sem suporte, 2 min). `GAMELIB_GEN=$PWD/generated-android-dyn-143
  GAMELIB_OUT=$PWD/android/out/v143 bash android/build_gamelib.sh <caminho absoluto>/Dump/0100F2C0115B6000/exefs`
  (o `gen.py` lê o exefs de dentro de `tools/`: caminho absoluto). A compilação inteira levou **3 h 45** (271 MB;
  desde 2026-10-26, **21 min**: abaixo, "Compilar o código do jogo");
  uma regeração com patches novos só reescreve os chunks que mudam (`write_if_changed`): 4 arquivos, **1 min 14 s**.
  As pastas ficaram com o sufixo `-143` / `v143` porque renomeá-las muda os caminhos dos objetos no CMake (uma
  compilação inteira de novo).
- **1.4.2 removida (2026-10-25):** o dump, o código gerado e o build dela no PC, os arquivos dela no celular e o
  `GAME_V_142` do código. A 1.4.3 é a versão principal (`GAME_V_MAIN`): no PC em `Dump/0100F2C0115B6000`, no celular em
  `files/extracted` (as pastas criadas pelo `adb` vieram `drwxrws---`: o app não lia o `exefs/main` - `chmod 775` nas
  pastas, `666` nos arquivos, como os da 1.4.2 estavam). O save da 1.4.2 abre na 1.4.3.
- **No celular (2026-10-25, Turnip):** a 1.4.3 inicia, passa da tela título e carrega o save na caverna (Link e Zelda
  inteiros, HUD em português) a 20 fps. A cena fica em **1600x896 do começo ao fim** (uma linha `[scene]`, a do início)
  e um vídeo de 8 s não tem quadro escuro (7 em 20 s antes). O passo em tempo real e os patches da segunda leva
  (pano / cabelo, tamanho de renderização) não foram medidos no jogo ainda.
- **Patches** (`tools/patches.py`, cada palavra original conferida antes):
  - `main+0x5848a8` (`mov w9, w23`): a escala do controlador de resolução dinâmica (`main+0x584300`) fica em 100 %
    (`rt_dynres_scale`; `LARECOMP_DYNRES=1` devolve a do jogo). Cada troca de tamanho do G-buffer (1600x896,
    1432x808, 1280x720) dava um quadro sem o mundo (abaixo, "piscadas").
  - `main+0x2daa54..78` e `0x3e5658`: o passo do mundo (o FrameRateMgr o tira dos 30 fps nominais) passa a ser o tempo
    medido entre os presents (a média dos últimos 5, entre 1/120 e 1/15 s: `rt_frame_ratio30`,
    `rt_frame_interval60`). Um celular a 22 fps não roda mais em câmera lenta. `LARECOMP_REALTIME=0`: o valor nominal,
    bit a bit.
  - `main+0x246eac` / `0x345904` e `0x249448` / `0x28bc94`: o segundo relógio do update de ator (pano e cabelo), que
    não passa pelo FrameRateMgr: o mesmo passo medido (`rt_cloth_step`, `rt_cloth_ratio`; `LARECOMP_CLOTH_REALTIME=0`
    desliga). Nos dois `bl 0x2a0e40` o patch fica na instrução antes do `bl` (o `mov w6, ...`), não no `bl`, que o
    lifter transforma em chamada.
  - `main+0x585e48` / `0x585e54` (`scvtf s0, w11` / `w10`): o tamanho da cena (`LARECOMP_RENDER_RES=<l>x<a>`, senão o
    do jogo; acima de uma saída de 1920 de largura o jogo multiplica por saída / 1920, o que é desfeito). O padrão do
    MacNativeGames no iPhone é 960x540 (GPU de 24,3 para 17,3 ms lá): a medir aqui.
  - Ainda não trazidos: 60 fps (7 sítios), o pulo em câmera lenta a fps altos, widescreen / ultrawide (4 + Vert+), a
    resolução de saída (a tela do celular é 20:9).
- **Do que eles acharam, independente de versão (já aqui):** o early-Z do NVN (byte `+0x718` da seção de controle de
  0x880 bytes: `layout(early_fragment_tests)`), a altura de bloco do NVN (trunca `altura x 1,5 / 8`; o deko3d
  arredonda para cima), o contador de visibilidade (um vertex shader com ballot: aqui cada vértice que passa conta a si
  mesmo, como a redução deles) e as máscaras de lane do `S2R`.
- **Ainda a ver deles:** os clears de D16 nas classificações das habilidades (Ultrahand / Recall: quadrados
  coloridos), decalques que leem o G-buffer em que se misturam (num tiler), `MatchSwitchDepthSampling`, a memória do
  pool de aplicação (3.285 MiB) e a lista de shaders compartilhada.

## Travamentos, piscadas, o driver da Qualcomm e o build (2026-10-26)

O usuário, jogando: "o jogo está travando muito e piscando a tela" (a tela inteira preta) e "o jogo trava e só volta
se alternar a janela". O log daquela sessão já tinha saído do logcat; o que se achou no código e num teste:

- **Qual driver:** a pasta `drivers` externa do app está vazia, então abrir o jogo pelo ícone ou pelas recentes usa o
  **driver da Qualcomm** - o Turnip só vem dos scripts de teste (`LARECOMP_VK_DRIVER`). Com ele a 1.4.3 perde a GPU no
  quadro ~150 (8 s, na abertura), como a 1.4.2.
- **Do BOTWRecompAndroid** (o que ele ganhou depois de 2026-10-24 e não dependia do jogo; o resto já estava aqui ou é
  recurso, não correção):
  - `67234dc`: no driver da Qualcomm, nada de desenhar em fatias de imagens 3D (lá isso perdia a GPU na tela título
    em segundos). `[vk] 3D target ...` no log diz se o TOTK desenha assim.
  - `e237938`: até 16 imagens na swapchain (um Mali fez 12). `7d3e980`: o quadro do vídeo copiado no buffer do próprio
    jogo (o BOTW mostrava ruído e travou num Adreno 840). `994d6c0`: as threads de áudio param antes do reset do
    renderer (Sair > Voltar ao início caía). `23ebe74` / `7cfb427`: ferramentas de teste.
  - Ficou de fora o `625f8c3` (congelar os contadores de GPU do jogo): o TOTK usa esses contadores na sua resolução
    dinâmica, que aqui já está presa em 100 % pelo patch; congelá-los mexeria no que mais os lê sem teste.
- **O travamento que volta ao trocar de janela:** duas esperas sem prazo que só a ida ao segundo plano desfazia.
  - `vkAcquireNextImageKHR` esperava para sempre (`UINT64_MAX`): um compositor que segura os buffers prendia ali a
    thread de render, e com ela o jogo (um quadro à frente), até a superfície sumir. Agora espera 1 s; sem imagem,
    refaz a swapchain (o que a troca de janela fazia) e tenta de novo; sem imagem ainda, pula o quadro.
    `[vk] no window image within 1 s` no log.
  - O áudio: o `audren` e o `audout` esperavam o dispositivo esvaziar a fila; um stream do AAudio que parou de
    consumir (erro, rota que sumiu) parava os eventos de áudio do jogo para sempre, até o SDL reabrir o dispositivo
    na volta. Agora, 250 ms sem consumir nada: a fila é descartada e as amostras seguem pelo relógio até o
    dispositivo voltar a consumir. `[audren] the audio device took nothing for 250 ms` no log.
- **As piscadas (tela inteira preta):** um draw cujo shader ou pipeline ainda compila é pulado (para não travar), e um
  alvo desenhado todo quadro "refazia" o draw no quadro seguinte. Nos passes de pós-processamento, na composição final
  e no HUD (sem alvo de profundidade, poucos passes de tela cheia), um draw pulado deixa o quadro inteiro preto - o
  sintoma em terreno novo, com shaders novos. Agora todo draw sem profundidade espera o seu pipeline (uma espera na
  primeira vez de cada shader; os caches guardam). `LARECOMP_WAIT_NO_DEPTH=0` volta ao de antes;
  `[vk] frame N: a draw into WxH ... skipped` mostra os 40 primeiros draws grandes ainda pulados (os com profundidade).
- **O "travando muito": quadros de 300 ms na GPU - resolvido.** Andando pela caverna com o Turnip (o save do usuário,
  `play_test.sh`: o direcional de toque e a câmera arrastada), o jogo caía de 30 para **3-6 fps** por vários segundos,
  quadros de 200-700 ms, a thread de render esperando a GPU quase o tempo todo. `LARECOMP_GPUPROF_SLOW=<ms>` (novo:
  perfila o quadro depois de um mais lento que isso, até 12) achou sempre o mesmo passe (1600x896, `de65b7d7...`) e,
  com `LARECOMP_GPUPROF_DRAWS=1`, **um draw** de 2.832 índices: **95-102 ms** de GPU (o shader `b9e2681d3cbc73d8`, de
  material; outro, `18c1caaa...`, 6-20 ms). O GLSL (`LARECOMP_DUMP_FS`): o shader guarda um registrador na memória
  local (um `STL [0x0]` e um `LDL [0x0]`, spill do compilador da Nintendo), e o tradutor declarava `uint lmem[256]` e
  zerava as 256 palavras **a cada pixel** - 1 KB por thread não cabe em registradores, vai para a memória privada, e
  o laço escrevia 256 vezes por pixel. Agora o vetor tem o tamanho que os acessos pedem: os de endereço constante, as
  palavras que alcançam (aqui 1); com endereço em registrador, o tamanho do SPH (`ShaderLocalMemoryLowSize` +
  `HighSize`), ou as 256 sem SPH; potência de 2 (o índice é mascarado: `lmask_`). Mesmo trajeto depois: **nenhum
  quadro acima de 200 ms** (antes, 11 em 4 min), 20-30 fps com o quadro máximo em ~52 ms. Fuzz do tradutor (4.000
  programas): só as 14 falhas antigas de `textureGather`; `tools/test_translate_ops.sh`: 31 casos, 0 falhas.
- **30 fps estáveis em vez de 30 <-> 20:** sem os quadros de 100 ms o jogo ainda trocava o intervalo de
  apresentação entre 2 e 3 a cada um ou dois segundos (`[pace] the game sets its present interval to 3`, lr
  `main+0x2db2ec`: o controlador que, pelas notas do MacNativeGames, passa a 3 quando o trabalho medido da thread
  principal passa de um quadro de 30 fps - aqui esse trabalho inclui as esperas pela thread de render e pela GPU).
  Com o passo do mundo em tempo real, o intervalo só ritma os presents: na 1.4.3 ele fica em no máximo 2
  (`LARECOMP_MAX_INTERVAL=<n>`, 0 = o do jogo). A mesma caminhada de 3 min (`LARECOMP_LOG_PACE=1`): **23,7 -> 29,5
  fps** de média, quadros acima de 40 ms de 53 % para 4,4 %, acima de 60 ms 0,1 %; render 16,7 ms e GPU 18,9 ms por
  quadro.
- **Quadros escuros, medidos:** 30 s de vídeo andando e girando a câmera (`screenrecord`, a luma média de cada quadro
  pelo `signalstats` do ffmpeg; escuro = abaixo de 35 % da mediana dos 10 vizinhos): Turnip, 852 quadros, **0
  escuros**; driver da Qualcomm, 313 quadros, **0 escuros**. (O título da área, "Subsolo do Castelo", se desfaz em
  partículas: é do jogo.)
- **No driver da Qualcomm, depois de tudo isso:** 16,0 fps de média andando (render 37 ms, GPU 37 ms por quadro; eram
  ~9-10 fps). Lá as leituras de um alvo que o draw escreve viram cópias (~40 por quadro de alvos 1600x896); lê-las no
  lugar (`LARECOMP_FEEDBACK_INPLACE=1`, indefinido nesse driver) deu 16,7 fps e a imagem certa nas capturas - pouco
  para ligar sem garantia. O gargalo restante é o modo de cópia da memória do jogo (~550 MB de upload a cada 2 s).
- **Próximo candidato a desempenho: a cópia de buffer de cada quadro.** A thread de render espera a GPU numa cópia
  buffer -> buffer (`waits: buffer -> buffer copy` no `LARECOMP_VK_PROF`): ~5,6 ms por quadro no Turnip, ~27 ms no
  driver da Qualcomm. `LARECOMP_LOG_BUFCOPY=1`: é uma só por quadro, **304 bytes** de `3b64a3040` para dois destinos
  alternados (`3b612d840` / `3b612dc40`, um buffer duplo), e a GPU escreve a origem (o conteúdo mudou durante a
  espera em 29 de 30). O `CMD_COPY_BUFFER_TO_BUFFER` é um `memmove` na CPU depois de esperar toda a GPU: certo, mas
  serializa a CPU e a GPU. Na GPU (um `vkCmdCopyBuffer` dentro do heap colocado, Turnip) a espera some, desde que quem
  lê o destino depois (as cópias de constantes que o renderer tira ao gravar um draw, ou o jogo na CPU) espere o
  fence. Quem o lê ficou sem resposta: o vigia de leitura (`LARECOMP_WATCH_READ_ADDR`) derrubou o jogo com o heap
  colocado (sinal 11 logo depois de armar).
- **Ainda pulados:** draws com profundidade cujo shader ainda compila (rajadas de centenas quando entram shaders
  novos, como depois de mudar o tradutor): um objeto que aparece um quadro depois, não a tela preta.
- **O driver da Qualcomm, de novo (o `67234dc`):** o TOTK também desenha nas fatias de um volume 32x32x32 (`[vk] 3D
  target 32x32x32 level 1`, no quadro ~150 - exatamente onde a GPU se perdia). Sem esses draws, a 1.4.3 passa da
  abertura e carrega o save: **5 min jogando, sem *device lost***, a imagem certa (caverna iluminada, Link escalando),
  mas a ~9-10 fps (render ~70 ms por quadro no modo de cópia desse driver, GPU 43 % do tempo esperada). O Turnip segue
  muito melhor: 20-30 fps.
- **Compilar o código do jogo - de horas para minutos.** Um arquivo, `main_568.c`, levava **199 min** (a compilação
  inteira, 3 h 45, esperava por ele). Não era o tamanho: a função `f_70006fe6ac` (9.746 instruções) tem uma tabela de
  salto, e toda função com tabela ganhava um switch de "rede de segurança" com **um case por instrução** (9.745), caso
  a emulação da tabela tivesse perdido algum alvo. Uma aresta para cada instrução: cada uma vira um bloco com dois
  predecessores, e o otimizador do clang se perde nisso (e não junta nada entre duas instruções). Medido nessa função
  sozinha, a -O1: o original passou de 11 min sem terminar; sem a rede, **3 s**; com a rede só nos **inícios de
  bloco** (os alvos de desvio, os das tabelas e cada instrução depois de um salto, retorno ou trap: 685 cases),
  **10 s**. É o que o `gen.py` faz agora; uma função com tabela **não resolvida** (142 delas) mantém a rede em toda
  instrução, como antes. Das 842 min de CPU da compilação, 666 eram de arquivos com uma função dessas de mais de 1.024
  instruções. **A compilação inteira no PC: 21 min 34 s** (era 3 h 45), 191 min de CPU (eram 842), o arquivo mais
  lento 12 min (uma das funções com tabela não resolvida), e o `totkgame.so` de 271 para 224 MB. O jogo roda com ele
  (os testes de desempenho acima). Com menos rótulos, só 2 arquivos passam de 15.000 e vão a -O0 (eram 13). E um bug do `gen.py` que
  apareceu aqui: com o código menor sai em menos arquivos (1.464, eram 1.690), e a limpeza dos que sobram de uma
  geração anterior só reconhecia nomes de 3 dígitos - `main_1464.c` em diante ficariam e entrariam na compilação.

## Áudio estourado, a resolução dinâmica e o cenário das cutscenes (2026-10-26, manhã)

O usuário, jogando com o Turnip (agora instalado no app: `Turnip_Gen8_V37`): "ainda está piscando a tela preta,
talvez seja a resolução dinâmica [...] e tem um audio estourado reproduzindo"; depois, numa cutscene, "o cenário
sumiu", e "é algo somente nas cutscenes".

- **O áudio estourado - resolvido.** Uma voz chegava à mixagem final com ganho **7×10^27**: o volume de um destino
  de splitter lido 12 bytes fora do lugar. Na REV15 cada entrada `SNDI` termina com 12 bytes que o jogo não limpa, e
  uma vez eles guardavam um cabeçalho `SNDD` velho e o texto "Main"; o leitor parava no primeiro `SNDD` que via, media
  o passo entre destinos como 0xB4 (12 + 0xA8) e lia cada destino deslocado - o volume vinha do texto (0x6e69614d =
  1,8×10^28). Agora um `SNDI`/`SNDD` 12 bytes adiante vence, o passo só pode ser um dos tamanhos conhecidos (0x70,
  0x90, 0xA8) e um destino com volume absurdo (≥ 64 ou NaN) é ignorado. Medido com o limitador desligado
  (`LARECOMP_NO_LIMITER=1`, uma cópia do save do usuário, 3 min andando e lutando): antes 100.128 amostras no volume
  máximo e picos de 10^27; agora pico **0,48**, nenhuma.
- **O limitador do jogo (efeito 7) - implementado.** O TOTK põe um na mixagem final (6 canais, limiar 0,708,
  ataque instantâneo, soltura de 2 ms, look-ahead de 4 amostras) e manda volumes acima de 1 para as submixagens
  (splitters com até 1,8). Porte do `LimiterCommandVersion1` do Ryujinx; os parâmetros conferem com o layout
  (`[audren] limiter:` no log). A mixagem 5.1 -> estéreo ainda soma até 1,89x depois dele (picos de 1,1 algumas vezes
  por minuto): o último estágio deixou de ser um corte seco e tem um joelho acima de 0,9. `[audren] N samples clipped`
  no log conta o que passa do máximo.
- **A resolução dinâmica - presa de verdade.** O patch antigo só fixava a escala que o controlador devolve; o
  controlador seguia mudando de faixa (40 trocas em 3 min andando), e o sinal "mesma faixa da última vez" que ele
  devolve no bit 33 mudava junto. Agora as três gravações da faixa (`main+0x584a70`, `0x584b70`, `0x584b94`, `str ...,
  [x19, #0x134]`) passam por `rt_dynres_tier`, que só aceita uma faixa de escala maior: a faixa fica em -1 (100 %) a
  sessão inteira (`[totk] dynamic resolution tier ... kept`). A lista de faixas é a do jogo (nós a partir de
  `[ctl + 0x158]`, a escala em nó - 0x14, o id em nó - 8). Congelar os contadores de GPU do jogo, como o BOTW faz,
  só reduziu as trocas (40 -> 3) e saiu.
- **O cenário nas cutscenes - aberto.** Não é a resolução dinâmica: com a faixa presa, as paredes da caverna somem
  igual em vários planos de cutscene (só névoa atrás dos personagens), e voltam no gameplay. Reproduz sozinho num jogo
  novo numa pasta de save própria (`files/gentest-ng-<hora>`): a abertura, a Zelda andando com a tocha, ~45 s depois
  do início. Seis configurações não mudam nada: `LARECOMP_SAMPLES_PASSED=0`, `LARECOMP_VK_COUNTER=cpu`,
  `LARECOMP_REALTIME=0`, `LARECOMP_DYNRES=1`, `LARECOMP_MAX_INTERVAL=0` e o padrão. Pelos dumps de quadro: as paredes
  **estão no G-buffer** (a reduzida das normais, 800x448, mostra a caverna inteira) e o depth do G-buffer é o mesmo
  objeto que a iluminação amostra para fazer a profundidade em meia resolução - mas no quadro iluminado a parede sai
  preta e só o personagem tem luz. Comparando com um quadro de gameplay (logs dos dumps, offline):
  - As paredes usam **os mesmos shaders** nos dois (o da rocha, `fs 245e6a300`: 41 draws na cutscene, 27 no gameplay).
  - As luzes da cutscene são **os mesmos shaders** das do gameplay (mesmos hashes, outros endereços); só uma é só de
    cutscene (`7ab0674cbc2b0e61`, uma luz com textura projetada, 32 draws). A profundidade reduzida que elas leem é
    escrita uma vez, da profundidade do G-buffer, antes delas, nos dois.
  - A cadeia de 6 shaders de tela cheia que só a cutscene tem (`241d60100` ... `241d65500`) é a profundidade de campo,
    e vem **depois**: a cena iluminada antes dela (1600x896 `f0x3f @35b9d8200`) já tem a parede preta.
  - O alvo 0 do G-buffer (`@35cb5ee00`) guarda bandeiras de material, não a geometria: no gameplay também é zero na
    maior parte das paredes.
  - Formatos conferidos: `0x11` (o tamanho do desfoque) é RG16F, `0x4B` é BC5.
  Próximos testes (no celular, a abertura de um jogo novo): `TU_DEBUG=nolrz` (o LRZ do Turnip está ligado),
  `LARECOMP_FEEDBACK_COPY=1` (leituras do próprio alvo por cópia), `LARECOMP_NO_EARLY_Z=1`; o GLSL das luzes
  (`LARECOMP_DUMP_FS=` os hashes acima) e os valores reais (`LARECOMP_DEBUG_RAW_PASSES=1`) do buffer de luz e da
  profundidade reduzida num plano com e num sem parede.

## As melhorias do BOTWRecompAndroid de 25 a 27/10 (2026-10-27)

48 commits trazidos por cherry-pick (`git remote botwl` = `../BOTWRecomp`, histórico comum até `9d9057e`; o `-x` de
cada um aponta o original), `088b04f..6030de0`. O que entrou:
- **Correções:** coordenadas de textura dos vertex shaders arredondadas a 1/256 de texel, como o Switch pesa (as
  rachaduras pontilhadas do terreno do BOTW, `9c3a0f3`); write-backs que esperam um slot em vez de se perder, e 128
  slots pequenos ao lado dos 32 grandes (a CPU não espera mais a GPU a cada dois quadros); chinês como zh-Hans /
  zh-Hant (zh-CN / zh-TW davam texto japonês); MapMemory confere os endereços; a janela esperada quando o jogo abre com
  a tela travada; relatórios de crash com os registradores do jogo.
- **Desempenho:** a resolução de saída Auto (no máximo 1080 linhas em telas mais altas; o present escala); GLSL mais
  barato para o driver (do SavageWindRecomp); a thread de render calcula os alvos escritos de um draw uma vez só.
- **Build do código no celular:** o gerador com 63 % menos memória, sem UBSan / stack protector / debug info do zig,
  o arquivo de uma função gigante a -O0 e sozinho, compiladores conforme a memória livre e a temperatura, retomada de
  onde parou, um serviço em primeiro plano (tela desligada), de volta à ordem do gerador (o "maior primeiro" do
  LAS-Plus derrubava builds em celulares com pouca memória: issue #17 do BOTW).
- **App:** giroscópio do celular (ou do controle) como sensor de movimento do controle, com o viés em repouso tirado;
  toque estilo FPS (câmera arrastando, analógicos só ao tocar); zona morta dos analógicos do controle; menus em 11
  línguas, seguindo a língua do jogo; Configurações > Saves (exportar como .zip, importar de .zip ou de uma pasta de
  emulador - o layout do TOTK: `slot_00/` ... com `progress.sav`); FXAA; o diálogo de erro de driver (instalar um ou
  usar o do sistema); a versão do app no canto da tela inicial; a configuração de jogo que confere o romfs (no TOTK:
  304.243 arquivos e três arquivos-chave, sem `System/Version.txt`); diagnósticos (`LARECOMP_LOG_GPU_IDLE`,
  `LARECOMP_FRAME_TIMELINE`, `LARECOMP_LOG_STALE_UPLOAD`, `LARECOMP_UCLAMP_MIN`, `LARECOMP_TEST_CRASH`, o dump da
  textura vigiada).
- **Sync lag** (descoberta do MacNativeGames no TOTK: a espera do jogo pela sua cerca mais nova vai para a do quadro
  anterior, e o próximo quadro sobrepõe a GPU): a opção em Vídeo veio, mas **desligada por padrão** no TOTK - no BOTW
  ela deu água branca e as pernas do Link afundando no chão; falta testar aqui.

Ficou de fora: o que é só do BOTW (versões do app, DLC, 1.9.0, o mapa do relógio Zepp, a resolução dinâmica pelos
contadores - o TOTK prende a faixa direto), a importação do heap só no Xclipse (o TOTK a mantém desligada em todo
celular; o driver da Qualcomm nem a oferece) e documentação do BOTW. O gerador novo produz **o mesmo código** do
TOTK 1.4.3 (nenhum arquivo reescrito: a correção da tabela de salto não pega nenhuma função dele). Testado no PC:
o app compila, `tools/test_translate_ops.sh` 31 casos sem falha, o fuzz de GLSL (1.500 programas) só as falhas
antigas de `textureGather`. Falta no celular: os recursos novos e as traduções (as frases só do TOTK seguem em inglês
nas línguas novas).

## As novidades do BOTWRecompAndroid de 27 e 28/10 (2026-10-28)

Mais 16 commits do BOTW (depois do `4572f06`, até o 0.2.3-beta), por cherry-pick:
- **Imagem:** os gathers arredondados a 1/256 de texel, como o Switch (os pontos escuros na grama do BOTW: a oclusão
  de meia resolução pegava o 2x2 de outro pixel no Adreno); os deslocamentos .RS / .LS e as metades do IADD3 (do
  SavageWindRecomp; divisões por constante); write-backs aplicados na ordem da fila (a água branca e os pés do Link
  afundando, #43); o sync lag só depois de 300 quadros com o mundo na tela (cada carregamento roda sem ele); a arena
  de memória global e as leituras de volta em memória coerente (o tipo "cached" do Mali não é).
- **Desempenho:** o buffer de constantes de 64 KB atualizado por draw só nas linhas reescritas (a leitura no lugar
  veio desligada por padrão, como no BOTW).
- **Arquivos do jogo no lugar** (Arquivos do jogo > Usar no lugar): o romfs é lido da pasta escolhida, sem a cópia de
  ~17 GB (o runtime monta a imagem em memória); pede o acesso a todos os arquivos. Mods no lugar também. A parte do DLC
  do BOTW ficou de fora (o TOTK não tem).
- **Mods** que acrescentam arquivos e pastas que o jogo não tem.
- **Saves** que outro app colocou (um gerenciador com root, um editor): copiados para o app, ou uma mensagem do que
  fazer em vez do svcBreak. **Memória do jogo na GPU** só a partir de ~7 GB de RAM. O giroscópio de um controle só
  substitui o do celular quando se mexe.
- **Build no celular:** as flags no carimbo de retomada; uma biblioteca com símbolos faltando é compilada de novo.
- **Diagnóstico:** `LARECOMP_PROF_SVC_TID` com duas threads, `LARECOMP_LOG_FS_CB`, `LARECOMP_DEBUG_PICK` (o histórico
  de um pixel, em coordenadas da cena 1600x896 do TOTK; o `LARECOMP_DEBUG_SWEEP` do TOTK continua ao lado).

O **modo nativo** veio depois, a pedido do usuário (ver abaixo). Ficou de fora: as cutscenes a 60 fps e o aspecto livre (do
BOTW); o arredondamento do passo das partículas do BOTW (lê `c[10][0x2c]` dos shaders de efeito dele - no TOTK essa
constante pode ser outra coisa). Testado no PC: o app compila, `tools/test_translate_ops.sh` 31 casos sem falha, o
fuzz de GLSL só as falhas antigas de `textureGather`. Falta no celular, principalmente o "usar no lugar" com os 304 mil
arquivos do TOTK.

## Modo nativo: o código do jogo direto na CPU (2026-10-28)

Do BOTWRecompAndroid (`053e80a`, `3678eda`, `b451ec4`, `ba93baa`; o estudo dele em `docs/botw/EXECUCAO-NATIVA.md`):
com Vídeo > "Código nativo do jogo (experimental)" ou, na configuração dos arquivos, "Código do jogo: Nativo", o
`main` e o `sdk` do TOTK rodam como estão, sem o `totkgame.so` - sem os ~20 min de compilação do código do jogo.
Depois de carregados são reescritos: `svc` vira um stub para `native_svc_gate`, as leituras e escritas de
`tpidr`/`tpidrro` vão para o `Cpu` da thread, `cntpct` vira `cntvct`; as funções do runtime (NVN, imports) ficam nos
slots HLE como código que entra em `native_hle_gate`. No TOTK: ~63 KB de stubs no `main` (814 leituras de TLS, nenhum
`svc`) e ~14 KB no `sdk`.

Os 15 patches do `tools/patches.py` viram stubs (`runtime/native.c` `fixes[]`, a palavra original conferida antes):
a escala da resolução dinâmica e as três gravações da faixa, o passo do mundo (a leitura da razão, as três instruções
seguintes descartadas, o intervalo), o relógio do pano e do cabelo (dois em linha, dois nos argumentos da chamada) e
o tamanho da cena - cada um um handler no `Cpu` que faz o mesmo que o C do código gerado. No BOTW o nativo chegou à
tela título e ao mundo, sem ganho de fps (51-52 contra 55 recompilado).

**No TOTK, no celular (2026-10-28, Turnip, cópia do save na caverna, 200 s de roteiro):** roda. Os 15 patches
conferem e entram (`main: 0 svc, 814 TLS, 0 counter reads rewritten, 15 fixes; stubs ... (80 KB)`; `sdk: 58 svc, 129
TLS, 43 counter reads rewritten`), o save carrega, Link anda e escala, a faixa da resolução dinâmica fica segura.
Contra o recompilado no mesmo roteiro, os dois a 30 fps travados:

| | nativo | recompilado |
|---|---|---|
| média dos intervalos de 2 s (de 96-97) | 29,1 fps (82 a 30) | 29,7 fps (85 a 30) |
| thread do jogo (overlay, 4 capturas) | 0,9-1,3 ms | 0,7-0,9 ms |
| thread de render | 16,0-19,5 ms | 13,8-19,1 ms |
| CPU do app | 16-19% | 19-24% |

Sem diferença que se separe do clock da CPU (1152 a 1785 MHz entre as capturas): o gargalo é a thread de render, onde
fica a tradução NVN -> Vulkan, não o código do jogo. As quedas a mais do nativo são do começo (compilação de shaders
no carregamento). O ganho dele continua sendo não precisar do `totkgame.so`.

Testes no celular: a pasta de save do teste tem de ser feita pelo app (`LARECOMP_SAVE_DIR` para uma pasta nova +
`LARECOMP_SAVE_SEED` com o save do jogador, só lido): uma pasta feita pelo adb agora para no `own_save` (`fs.c`, do
BOTW #77: arquivos de outro dono, e o app não consegue trocar a pasta pela sua cópia, errno 13). E o app novo recusa
um `totkgame.so` de antes de uma mudança no gerador (`GAMELIB_ID`): instalar o app pede regerar e compilar o código
do jogo junto (aqui: `ec5c256`/`8c3e54d` mudaram o id; a regeração reescreveu 1 chunk, 3 min no total).

## 60 fps e as cutscenes (2026-10-28/29)

A pedido do usuário ("é possivel liberar os 60 fps?"). Vídeo > Taxa de quadros passa a ter, no TOTK, Auto (30 / 60),
30 e 60 (antes só o BOTW 1.8.2; o padrão continua 30). No recompilado (`tools/patches.py`) e no nativo
(`runtime/native.c`), os mesmos sítios:

- **O controlador de quadros** (`main+0x2db180`, que chama `nvnWindowSetPresentInterval` de `0x2db2e8`): os 7 sítios
  do MacNativeGames (`0x2db204` ... `0x2db36c`) tomam como menor intervalo 1 em vez do do jogo (`[x20]`, 2). No
  recompilado cada sítio leva as duas instruções (o C do próprio lifter para cada uma) escolhidas em tempo de execução
  por `rt_fps60()` (Auto ou 60; `LARECOMP_FPS60=0/1` força); no nativo, `w8 = 1` antes do primeiro `cmp` faz o que os
  `cmp` / `csel` com #1 deles fazem (4 stubs bastam, sem calcular flags).
- **As transparências** (4 sítios, `fcvtzs` de razão x 2,1): um passo positivo é pelo menos 1 (a 60 a razão é ~0,5 e
  um quadro mais curto dava 0: objetos presos invisíveis).
- **Auto:** a escolha do próprio controlador - a 60 onde o quadro cabe. No celular (POCO F7 Ultra, Turnip, a caverna)
  ele tenta 1 e volta a 2 em menos de 1 s (6 vezes em 200 s, 30,8 fps de média): o trabalho medido do quadro inclui as
  esperas da thread de render e da GPU.
- **60:** as apresentações a cada vsync (`nvnWindowSetPresentInterval` força 1; o limitador usa 1 seja o que for que
  ficou na janela). Na caverna: 35-40 fps (GPU + thread de render de ~18 ms; com a cena a 960x540,
  `LARECOMP_RENDER_RES`, 42 fps). O mundo, o pano e as transparências andam pelo tempo medido: sem câmera lenta.

**As cutscenes.** A 60 o usuário as viu aceleradas; seguradas a 30 por baixo, em câmera lenta. Parte da animação de
uma cutscene anda pelo intervalo em que o jogo *acredita* (como a da interface do BOTW, MacNativeGames #103:
`max(intervalo, 1) x 0,5` por quadro), não pelo tempo medido: o jogo acreditando em 2 com quadros a 50-60 fps (uma
cutscene é leve) corria; acreditando em 1 com apresentações a 30, ia devagar. A duração total da abertura não muda
(42 s a 30 e a 60): a linha do tempo é a certa, a animação não. Agora, durante uma cutscene, o jogo acredita em 2 e
apresenta a 30, como no Switch:
- **Detectar:** `LARECOMP_FNPROF` (agora com `LARECOMP_FNPROF_KEEP=1`: um arquivo por intervalo, as contagens de cada
  trecho por diferença) na abertura de um jogo novo achou ~50 funções que rodam uma vez por quadro só durante a
  cutscene (nenhuma no título nem em 2 min de gameplay). Quatro, de três cadeias de chamada diferentes (`0x45c700`,
  `0x2b23c7c`, `0x42c3bc`, `0xa5a188`; `0x44196c` é um `ret`), marcam o quadro na entrada (`rt_cutscene_tick`):
  cutscene enquanto uma delas rodou nos últimos 200 ms. `LARECOMP_CUTSCENE_FPS30=0` desliga.
- **O jogo acredita em 2:** `main+0x2db238` (`mov w8, #0x3c`), onde todo caminho do controlador chega com a escolha em
  `w21` antes de gravá-la e passá-la à janela: `rt_interval_pick` a faz pelo menos 2 durante a cutscene.
- **Apresenta a 30:** o limitador espera 2 vsyncs, e o 60 forçado não vale durante ela.
- Medido (abertura a 60): cutscene 08:28:04-08:28:50 a 30, o jogo pedindo 2 ao começar e 1 ao acabar; depois, 36-39
  fps. Só a abertura foi vista; outras cutscenes ainda não.
- O travamento depois da primeira cutscene (Link parado, a segunda não vem) acontece a 30 também: o jogo espera o
  jogador andar.

`LARECOMP_LOG_FSSEEN=1` (cada fragment shader na primeira vez e quando volta depois de 3 s sem desenhar) achou a cadeia
de profundidade de campo (`d076cb66...`, `27697787...`, `64d33b60...`, `1a0e22d6...`, `a6a2c498...`, `6c864c34...`)
só nas cutscenes - mas ela some por mais de 3 s em alguns planos: não serve de detector.

O menu de taxa de quadros na tela inicial (antes do jogo carregar, versão 0) escondia o 60 e voltava um 60 salvo para
30: lá vale a versão da pasta de dados, 1.4.3.

**Modo nativo a 60:** um travamento no carregamento (pc fora do `libmain.so`, talvez no Turnip), uma vez; não repetido.

## As paredes só névoa (2026-10-29) - provavelmente certo

**2026-10-30, comparado com o Switch** (capturas de um walkthrough que o usuário mandou): no console, a caverna do
prólogo também é névoa verde-azulada. As paredes e o templo aparecem lavados de névoa, e só o Link e o que uma luz local
alcança (o dispositivo Zonai) ficam iluminados. É o que o port mostra. O dump abaixo, com os passes de luz somando zero
nas paredes, combina com uma caverna sem luz direta. Fica para conferir de perto: a parede iluminada por uma luz
local (a 3ª captura do Switch) e o tom exato da névoa.


O usuário, jogando: "as paredes estão com uma nevoa [...] a agua esta escura", a 30 e a 60. É o sintoma das cutscenes
(26/10), agora em gameplay e sempre no mesmo lugar: com a cópia do autosave dele (ruínas com um anel de pedra, Link
sem roupa; `totk_spot.sh` no scratchpad: o save mais novo, A para carregar, nenhum movimento) tudo é névoa
verde-azulada e só o Link tem luz.
- `TU_DEBUG=nolrz`, `LARECOMP_FEEDBACK_COPY=1`, `LARECOMP_NO_EARLY_Z=1`: nada muda.
- Dump do quadro (`LARECOMP_DEBUG_RAW_PASSES=1`, valores lidos dos `.raw`; a cena é 1600x896 no jogo, 1280x717
  aqui): a geometria está no G-buffer, mas a cena iluminada (`097`, B10G11R11) é névoa pura: média (0,022; 0,055;
  0,049), e o mínimo da tela é a cor da névoa. (O alvo de 800x448 RGBA16F zerado, `@35c5e6c00`, não é luz: é o de
  partículas / efeitos em meia resolução - e ocupa a memória do alvo 2 do G-buffer, 1600x896 RGBA8, no mesmo endereço.)
- A iluminação é diferida: o G-buffer de 6 alvos (`@35cb5ee00` bandeiras de material, `@35c06ea00` f0x38,
  `@35c5e6c00`, `@35b9d8200` f0x3f - a cena, onde a luz se soma ...); `13fe41b7645b246c` lê as bandeiras e marca o
  stencil; 11 passes de tela cheia, um por classe de material (`275e821b13b0d814`, `71c7ef99e317a9bd`,
  `321fa39d58ff156e`, `c4eea0f30814163d`, `ccd909d10be0ffa5`, `70586f818c71a3fa`, `0685d1c554a90936`,
  `54a5255cdbe96206`, `f22828104512b59a`, `2178807956b82cf1`, `7bf74eff0a7d4935`; stencil `010003`, blend aditivo),
  somam a luz em `@35b9d8200`. Leem o G-buffer, a profundidade, as máscaras de meia resolução `@35e218200`,
  `@35dbd7c00`, `@35dd67e00`, `@35e3a8400`, `@35e87a200` (200x112) e a sonda de ambiente 64x64 `@3b647b000`.
- A sonda de ambiente (capturada no quadro, passes 64x64 `008`-`019`) sai normal: média (0,21; 0,20; 0,19), nenhum
  pixel zero. Suspeito principal: a máscara `@35e218200` (`092`-`096`), que todo passe de luz lê - nas paredes
  R 1, G 0,22, B 1 e **A 0**, no Link outros valores. Se o alfa for oclusão / visibilidade, apaga a luz da parede.
- Não é o volume 3D (32x32x32, `[vk] 3D target`): `LARECOMP_LOG_3D_RT=1` mostra cada fatia e cada mip num draw seu, e
  nenhum vertex shader escreve o layer (o tradutor descarta o atributo `0x64`; agora marca `writes_layer` e o log diz
  quando).
- MacNativeGames: nada sobre isso nos issues / PRs / docs do TOTK nem do iPhone. O mais próximo é o #188 (terreno
  escuro quando uma escrita de volta da GPU era pulada), que o `502088f` do BOTW (já aqui) cobre do nosso lado.
- Próximo (no celular): (1) o GLSL dos 11 passes de luz e dos que escrevem as máscaras (`LARECOMP_DUMP_FS=`
  os hashes acima + `c0f9071f5ef7027b`, `05a99cd0aa4bcffb`, `899e096727d434c2`, `de9787afcc1963cf`,
  `b99e27201422cf02`, `8b67d5ac78f78619`): o que cada canal da máscara é; (2) o mesmo dump num lugar onde as paredes
  saem certas - a caverna dos saves de 26/10 (slot_00 a 02): uma pasta-semente feita pelo adb só com um desses slots
  (legível: `own_save` faz a cópia do app) em `LARECOMP_SAVE_SEED` - e comparar a máscara e os passes de luz.

## O que trabalhar - o vídeo do usuário (2026-10-29)

Atualização de 2026-10-30:
- **Nuvens em blocos: corrigido** (`c193510`, TMML sem sinal).
- **Paredes sem textura e cenário sumindo nas cutscenes: corrigido** (renderização condicional do NVN,
  `exec.c` `CMD_SET_RENDER_ENABLE_COND`). A causa e como foi achada:
  - **A causa:** `nvnCommandBufferSetRenderEnableConditional` era uma operação vazia e `SetRenderEnable` nunca era
    executado. O TOTK limpa os alvos temporários "no primeiro uso" (main+0x36da0) assim: por slot de textura há dois
    reports de contador de 16 bytes; a CPU marca o primeiro com 1 e grava `SetRenderEnableConditional(modo 1 =
    renderiza se diferente, slot)` + `ClearColor` + `SetRenderEnable(true)`, e um `ReportCounter` de timestamp no
    slot logo depois (o valor volta a 0). Na GPU só o primeiro clear passa; os seguintes, gravados pela CPU antes de
    a GPU rodar, são pulados. Sem a condição, o array de luz de meia resolução 800x448x2 (`t6` de `71c7ef99`,
    `@35dd67e00`) era limpo 10 vezes por quadro, antes de cada grupo de luz e antes da leitura, em vez das 3 do
    começo: a pedra ficava só com a ambiente fraca do cubemap e a névoa, e o fundo das cutscenes ficava preto.
  - **A correção:** a condição avaliada ao executar a lista, em ordem com os `ReportCounter` (que o runtime grava
    nesse mesmo momento): o valor de 64 bits de cada report, em +0 e em +16, como o Maxwell (yuzu
    `ProcessQueryCondition`); modo 0 renderiza se iguais, 1 se diferentes. Enquanto desligado, draws e clears são
    pulados. `LARECOMP_NO_RENDER_ENABLE=1`: o comportamento antigo; `LARECOMP_LOG_RENDER_ENABLE=1`: as avaliações.
  - **Resultado** (save do usuário no lugar da 2ª captura, `android/out/cave-eden-before-after.png`, contra o Eden):
    piso de pedra (38, 54, 51) -> (48, 62, 54), Eden (57, 72, 60); paredes (48, 72, 68) -> (54, 78, 70), Eden
    (54, 85, 76); o vapor sobre a água e as plantas voltam. Clears de `t6` por quadro: 10 -> 3, como no Eden; clears
    no quadro: 72 -> 40. 30 fps estáveis. A abertura de um jogo novo (`android/out/newgame-old-vs-fix.png`): sem a
    correção o fundo dos closes da Zelda é preto; com ela, as paredes iluminadas pela tocha. O mergulho da Ilha do
    Céu continua igual (`android/out/sky-re-sheet.png`).
  - **A referência que resolveu:** o Eden no PC com o save do celular, capturado pelo RenderDoc 1.46 portátil
    (`D:\RenderDoc_1.46_64`). No Vulkan o replay falha (`VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS`: os endereços de
    buffer do Eden); no **OpenGL GLSL** funciona. Sem instalar a camada Vulkan (pede administrador):
    `VK_ADD_IMPLICIT_LAYER_PATH` no ambiente e `renderdoccmd capture ... eden.exe -g <nsp>`. Scripts de análise
    (`qrenderdoc --python`) e capturas em `D:\RenderDoc_captures`. O save do celular sai por uma cópia do app numa
    pasta interna (`LARECOMP_SAVE_DIR=<files>/dbg/savepull` + `LARECOMP_SAVE_SEED`) e vai para
    `%APPDATA%\eden\nand\user\save\0000000000000000\<perfil>\0100F2C0115B6000`.
  - O histórico abaixo tem pistas falsas: o `t6` preto **não** era do jogo (o Eden o lê com luz, (0,131; 0,156;
    0,088) na pedra), e a conclusão de que era "o esperado também no Switch" estava errada.
  - Antes: a névoa verde-azulada é a mesma do Switch, mas nas capturas do Switch
  (`android/out/switch-ref/02.jpg`, `03.jpg`; comparação em `android/out/cave-vs-switch.png`) as paredes mostram os
  blocos de pedra, o musgo amarelo e a areia; aqui saem lisas, cinza-azuladas. A correção da água não muda isso.
  - **A cor base está certa:** `LARECOMP_DEBUG_RT=1` (o alvo 1 de cada passe, o sRGB do G-buffer) mostra a pedra
    e o musgo como no Switch (`android/out/cave-rt1-sheet.png`). A textura se perde na luz.
  - **A luz:** só o buffer de luz antes da névoa (`android/out/cave-light-only.png`): o Link sai com luz quente e
    forte, e o terreno ao lado com uma ambiente azul quase preta. Rastreio da parede (`android/out/cavepick.log`,
    ponto 400,300): o classificador grava a classe 0, e só `71c7ef99` a ilumina, com (0,015; 0,052; 0,047).
  - **Os dois passes da classe 0** se dividem pelos tiles: `275e821b` desenha onde o registro do tile tem `.w = 1`,
    `71c7ef99` onde não tem. Quem marca é o fs `24298e300` (hash `7589e5f2c7390f90`): pelo alfa da cor base (bits) e
    por `t1` = alvo 0 do G-buffer; `.w = 1` quando o bit 4 do alfa está ligado e `t1.g != 0`. Na parede, o G-buffer
    grava `00000000` no alvo 0: pelos dados do próprio jogo o tile não ganha `.w` (provavelmente "recebe sol"), então
    `71c7ef99` deve ser o certo também no Switch.
    - `LARECOMP_FEEDBACK_COPY=1` (esse fs lê o alvo 0 que escreve): nada muda.
  - **As entradas de `71c7ef99`** (shader de diagnóstico por `LARECOMP_SHADER_SUBST` em `2486b4a00_fs.glsl`,
    `android/out/cave-ambdbg-dbg/pass_108.png`): cor base (`t4`) certa, `t3`/`t5` com conteúdo, cubemap `t7` com
    valores, `t8` quase preto e **`t6` preto nas duas camadas**. `t6` é um array 800x448x2 R11G11B10 em
    `@35dd67e00`.
    - Quem escreve `t6`: os passes de luz em meia resolução (8 alvos; alvo 2 = camada 0, alvo 3 = camada 1). O alvo 2
      salvo no fim do passe tem a caverna iluminada (`android/out/cave-rt2-pair.png`).
    - `LARECOMP_DUMP_SAMPLED=800x448:3f@35dd67e00` (agora grava em `LARECOMP_DEBUG_DIR`): na primeira leitura,
      as duas camadas estão zeradas.
    - Copy trace + `LARECOMP_LOG_CLEARS`: o jogo limpa `t6` com zero como alvos 0/1 depois da luz de meia
      resolução e antes de `05a99cd0` / `275e821b` / `71c7ef99` lerem. O mesmo objeto de textura do jogo é
      reinicializado ora como esse array, ora como uma 256x256 `0x11` no mesmo endereço.
    - `LARECOMP_SKIP_CLEAR=800x448:3f:3` (pula esses clears): as paredes continuam escuras.
    - `LARECOMP_VK_NO_PLACED=1`: as paredes continuam escuras, e o Link também escurece.
    - Com o clear pulado, `t6` continua zerado na leitura. Os dois grupos de luz de meia resolução escrevem os mesmos
      alvos, e o jogo limpa `t6` antes do 2º grupo e de novo antes da leitura (os alvos são resolvidos na gravação
      do comando, `cmdbuf.c`). Pelo jeito, `t6` preto é o esperado também no Switch: pista falsa.
  - **O draw sem anexo** (`24298c900`, hash `45041f98c918780e`, viewport 1x1, o do totk-pc-recomp `7ceda94`) projeta
    o cubemap 64x64 e grava coeficientes de ambiente em `cb0[81]` + 320.... Ele roda (passe 16x16), mas não
    aparecia em `LARECOMP_LOG_STORES`: o tradutor marcava `uses_gmem_store` só no contexto, não no `ShaderInfo`,
    para STG fora do compute. Corrigido (`translate.c`): agora ele aparece. As paredes continuam escuras.
  - Os constant buffers de `71c7ef99` (o `[cbdump]` agora mostra o endereço): `cb4` `3b60df300`, `cb9` `3b612c200`,
    `cb13` `3b612da00`; o draw 1x1 grava no buffer de armazenamento `3b64a3000`, que `71c7ef99` não lê (nem `gmem`
    nem `cb0`). Na caverna não há `CopyBufferToBuffer` (`LARECOMP_VK_GPU_BUFCOPY=0`: nada muda).
  - O cubemap `t7` (64x64x6, 5 níveis, `@3b647b000`, alisado no tempo com blend `CONSTANT_ALPHA` 0,2): níveis 0-3
    coerentes; o nível 4 vermelho com uma faixa preta (`android/out/cave-cube-strip.png`). É de propósito: o jogo
    o grava de um cubemap 16x16 R16F com swizzle (R, 0, 0, 1), um dado empacotado, não uma cor.
  - **Luzes locais:** os draws instanciados `9d6bec86951f1e9b` depois dos passes de luz são partículas de poeira
    (`android/out/cave-locallights.png`); nenhuma luz local é desenhada no quadro. Na 3ª captura do Switch a parede
    é iluminada por um objeto escuro com luzes (um constructo?) encostado nela, que não existe no nosso G-buffer
    (`android/out/cave-albedo-vs-switch.png`, com outro enquadramento): a comparação com ela não é justa.
  - Brilho médio das capturas (vídeo, não exato): Switch (56-61, 77-88, 68-79), aqui (45-47, 61-63, 55-58), uns
    20% abaixo e com menos contraste.
  - **No lugar da 2ª captura** (save do usuário, `android/out/spot2-vs-switch.png`): no Switch a cor da pedra
    (musgo, bordas) atravessa a névoa; aqui tudo sai verde-azulado uniforme.
  - **Eliminação no `71c7ef99`** (`android/out/abl/`, buffer de luz no chão, 8 bits): original (4,0; 10,2; 8,9);
    sem `t8` (1,8; 1,6; 1,3); sem o cubemap `t7` (2,1; 8,5; 7,8); sem `t5` igual a sem `t7`. Uns 80% da luz da
    pedra vêm do `t8`, um termo verde-azulado tipo névoa que segue a distância (alvo 1 dos passes de meia
    resolução, `android/out/spot2-t8.png`); a luz ambiente do cubemap, a que traz a cor da pedra, é minúscula.
    `t5` (alvo 0, R e B perto de 1) a multiplica.
  - O cubemap `t7` vem, com alisamento temporal, de outro cubemap (`@3b5fe6200`, 64x64x6, 4 níveis): uma face
    por quadro é desenhada como mini-renderização do terreno (fs `246dde500`, hash `478b355ae8e7e8f1`, com o atlas
    1024x1024 `0x8e` e uma 96x96 `0x6`), depois os mips são filtrados.
  - A cor final de `71c7ef99`: (difusa do cubemap + especular) · `t3.w` + `t8`, depois uma mistura de saturação
    com peso 0,2 + `t3.z` · `cb1[1].x`. Na pedra (`android/out/subst-t3/`): `t3.z` ≈ 0,46, `t3.w` ≈ 0,41, `t5` ≈
    (1; 0,45; 1). Em valores absolutos, a difusa do cubemap fica em ~0,007 e `t8` em ~0,03.
  - `LARECOMP_SCALE=1` (cena sem a escala 0,8): igual (`android/out/spot2-scale-cmp.png`).
  - (Resolvido com a referência do Eden: acima.)
- **Água escura: corrigido** (`precise gl_FragDepth`, abaixo). Antes: No Switch, o lago da ilha e as poças da caverna são verde-azulados; aqui saem quase
  pretos. O que se sabe (mergulho da Grande Ilha Celestial, `android/out/waterpick2*`):
  - **Como a água é desenhada:** no G-buffer (fs `d31be35c2ce14b2b`, prog `24887b000`, 8 alvos, profundidade escrita),
    com bandeira de material `0x33` no alvo 0. O próprio draw lê esse alvo 0 (tex6) enquanto o escreve.
  - **O que falta:** o classificador (`13fe41b7645b246c`) grava a classe como profundidade, mas nenhum dos 11 passes
    de luz (EQUAL) toca esses pixels. O pixel fica 0 até a composição das nuvens.
  - **Testes:**
    - `LARECOMP_VK_EQUAL_TOL=1` (LEQUAL com viés): os lagos ficam azulados. Mas a luz do Link na caverna fica
      manchada, então não serve de correção.
    - D16 em D32F: nada muda.
    - `LARECOMP_FEEDBACK_COPY=1`: nada muda.
  - **A conta da classe:** classificador e passes fazem a mesma conta. O classificador parte da bandeira R
    (`trunc(R·255)`, a máscara `cb3[1].x`, os nibbles reordenados, `/255`); cada passe parte de `cb11[25].x`.
    - Testado: o classificador com arredondamento no lugar do `trunc` (`LARECOMP_SHADER_SUBST` com
      `242900e00_fs.glsl`). Os lagos continuam escuros, então não é a conversão do UNORM8.
  - **A causa:** a água tem passe próprio (`2178807956b82cf1`, id 35 = classe 0x32; a máscara do classificador é
    `0xaf`, e 0x33 & 0xaf = 0x23 vira 0x32). Pintado de magenta por `LARECOMP_SHADER_SUBST`, ele não aparece em lugar
    nenhum: o EQUAL nunca passa. Em D16, 65535·(0,5 + classe/510) = 32767,5 + 128,5·classe: toda classe par (todas
    as daqui) cai num degrau de arredondamento ,5, e um ulp de diferença troca o valor. O VS tem `precise
    gl_Position`; o `gl_FragDepth` do classificador não, então o driver reassocia `fma(a,-k,a)+k` e a profundidade
    sai um ulp longe da do quad. Cada classe erra para um lado; a da água, para o errado. (Por isso D32F não
    ajudava: lá o EQUAL pede igualdade exata.)
  - **A correção:** o tradutor declara `precise gl_FragDepth;` em todo fragment shader que escreve profundidade.
    Os lagos saem verde-azulados, com reflexo (`android/out/water-fix/051.png`).


Gravação do usuário, 164 s (`Screenrecorder-2026-10-29-09-13-39-739.mp4`, cópia em `android/out/uservid/`, um
quadro por segundo em `f*.jpg` e folhas em `sheet*.jpg`): o despertar, a caverna até a saída, o salto das ilhas, o
lago e a Grande Ilha Celestial. Em ordem de prioridade:

1. **Paredes só névoa (as cavernas inteiras, 0-62 s):** tudo verde-azulado, só o Link iluminado (acima, "As paredes
   só névoa"). Na saída da caverna (62 s), com luz do sol, a pedra volta ao normal: o que falta é a luz do interior.
2. **Água preta (lago 130-150 s, poças da caverna 6-40 s):** a superfície sai preta, sem reflexo nem refração; o Link
   dentro da água aparece recortado / posterizado (145 s). Provavelmente a mesma causa que a das paredes (a água lê
   a cena iluminada e a sonda de ambiente) ou a cópia da cena para a refração.
3. **Respingos brancos opacos (6 s, 27 s, 36 s, 136-147 s):** a espuma e os anéis de onda saem brancos e sólidos; o
   mergulho no lago (136 s) enche a tela de manchas brancas. Partículas com a cor / alfa errados (o mesmo buffer de
   efeitos em meia resolução que ocupa a memória do alvo 2 do G-buffer?).
4. **Nuvens em blocos (salto, 72-125 s):** quadrados de 8-16 px nas nuvens, com o terreno de baixo aparecendo pelos
   buracos - as nuvens volumétricas em resolução baixa reconstruídas por blocos (temporal / checkerboard) que não
   enchem. Nas ilhas vistas através delas também.
5. **Desempenho no salto:** 23-25 fps, com a thread de render em 29-31 ms (o gargalo é a CPU da tradução, não a GPU:
   "waiting on the GPU" 13-17%). No chão da ilha, 27-30 fps.
6. Menores: o retângulo translúcido sobre a folha de nenúfar (145 s); o overlay mostrou "Power 51.8 W" (leitura do
   sensor de potência, não do jogo).

O que está certo no vídeo: a saída da caverna e a Grande Ilha Celestial (árvores, sombras, céu), o logo do título,
o inventário e os tutoriais, a câmera e o controle no salto.

## Diferenças em relação ao BOTW

| # | Diferença | Onde | Gravidade |
|---|---|---|---|
| 1 | **Tamanho.** O `main` tem o dobro do código do BOTW 1.8.2 (43,6 MiB de texto contra 20,6) e 228 mil funções (o BOTW 1.8.2 tem 122.961). Gerar e compilar leva o dobro, no celular também, e o `totkgame.so` fica grande. | `tools/gen.py`, `android/build_gamelib.sh`, `runtime/gamebuild.c` | Alta (tempo, memória) |
| 2 | **O renderizador é novo.** O TOTK refez boa parte do renderizador do BOTW (G3d2, Vfx2 e o agl / gsys da EPD), com mais passes de computação. Instruções Maxwell e usos de NVN que o BOTW não tinha devem aparecer no tradutor de shaders e no backend Vulkan. | `runtime/nvn/`, `runtime/nvn/maxwell/translate.c` | Alta |
| 3 | **Memória.** O TOTK usa quase toda a memória que um app tem no Switch (~3,2 GB); o heap do runtime vai até 3,25 GB (`HEAP_MAX`). Celulares de 12 GB ou mais. | `runtime/rt.h`, `kernel.c` | Média |
| 4 | **Nenhum hook de jogo.** Os do BOTW 1.8.2 (ritmo de quadros a 60, widescreen, o travamento ao recarregar) são de endereços do BOTW e não há decompilação do TOTK. As funções do runtime que eles chamavam ficam, sem uso, até os pontos do TOTK serem achados. O jogo roda a 30 fps no Switch, com resolução dinâmica. | `tools/patches.py`, `runtime/hle/` | Média (para melhorias) |
| 5 | **Desempenho.** Mais física (Ultramão, Zonai), céu e subterrâneo, mais shaders: o TOTK já sofre no Switch. | tudo | Alta |
| 6 | **304 mil arquivos no romfs** (7,7 vezes os do BOTW). Copiar pasta por pasta é lento: o `adb push` do romfs/ perdeu a conexão no meio (2026-10-24), e a configuração no celular abre cada arquivo pelo seletor de pastas do Android. | `android/push_game.sh`, `TOTKRecompAndroidActivity.java`, `romfs_build.c` | Média |

## Fases

### Fase 0: repositório e dump — **feita (2026-10-24)**

- Clone do BOTWRecompAndroid com o histórico (que inclui o do LASRecompAndroid e o do LAS Recomp). Remotos: `botw` /
  `botwl` (GitHub / `../BOTWRecomp`), `las` / `lasl`, `upstream` (peiikai/LASRecomp), `pkmsvl` (`../PKMSVRecomp`).
  Nada publicado (sem `origin`).
- `android/deps/out` e `tools/venv` são *junctions* para os do LASRecompAndroid, como no BOTW. Para apagar um link
  desses use `cmd /c rmdir <link>`, nunca um `rm -rf` (segue a junction e apaga o original).
- `third_party/libadrenotools` (submódulo) inicializado a partir da cópia local do BOTW.
- `Dump/` fora do git; `tools/totk_dump.py` confere os módulos, a versão, o NPDM e o romfs.
- O repositório usa `core.autocrlf=false` (arquivos com LF: o `GAMELIB_ID` é um hash deles).

### Fase 1: tirar o BOTW e compilar sem o jogo — **feita (2026-10-24)**

O PKMSVRecompAndroid (o porte do *Pokémon Scarlet*, também saído do BOTWRecompAndroid) já tinha feito esse trabalho de
forma genérica: os commits dele foram trazidos com os nomes trocados.

- Renome para **TOTKRecompAndroid**: pacote Java e applicationId `com.totkrecompandroid` (um app novo, ao lado do
  BOTW e do LAS), `TOTKRecompAndroidActivity`, `android/totkrecompandroid`, `totkgame.so`, `totkgen`,
  `totkrecompandroid.ini`, log `TOTKRecompAndroid`, chave de release em `~/.totkrecompandroid/`. As variáveis de
  ambiente continuam `LARECOMP_*` (o motor é o do LAS).
- Saiu o companion ZeppOS (o mapa do BOTW no relógio) e o servidor dele no app (sem permissão de INTERNET). As artes
  de Zelda (ícone da Triforce, fundo da tela inicial, atlas Sheikah dos controles de toque) e o visual Sheikah dos
  menus ficam.
- Versões: `GAME_V_142` (TOTK 1.4.2) era a versão principal, na raiz de dados (desde 2026-10-25, `GAME_V_143`). O mecanismo de versões lado a lado do
  BOTW (pastas `v<versão>`) ficou, sem nenhuma outra versão (`paths.c other_version`).
- `game_new_sdk()` vale para o TOTK: NVN 55 e as esperas do SDK 20, os caminhos do BOTW 1.8.2.
- O launcher e o app acham o romfs pelas pastas `Pack/` e `Mals/` (o do BOTW não tem `Mals/`).
- O program id vem do `main.npdm` (`g_program_id`: GetInfo 18, base das DLCs).
- Idiomas: os 13 do menu existem no jogo, nenhum vira inglês.
- `tools/patches.py` ficou vazio para o 1.4.2 (os patches do BOTW estão no histórico; os da 1.4.3: acima).
- **O app compila** (`gradlew assembleDebug`: 3 min 21 s, APK de 98 MB).

### Fase 2: gerar o código do jogo — **gerado (2026-10-24)**

O gerador do LAS rodou no TOTK (`--simd asm --dyn-base`, em `generated-android-dyn/`) sem mudança além dos pares
exclusivos do PKMSV:

| Módulo | Funções | Tabelas de salto | Arquivos C | C gerado | Tempo |
|---|---|---|---|---|---|
| `sdk` | 40.461 | 555 | 215 | 123 MB | 25 s |
| `main` | 228.251 | 2.243 | 1.678 | 907 MB | 3 min 49 s |

- **0 instruções sem suporte** no `main`. O TOTK tem 6 pares exclusivos de 16 bytes (`LDXP` / `STXP`): sem o lifter
  do PKMSV (`tools/test_excl_pair.py`) seriam 6 armadilhas. No `sdk`, 1 (dados lidos como código, igual ao BOTW 1.8.2).
- `android/build_gamelib.sh Dump/0100F2C0115B6000/exefs` compila o `totkgame.so` com o NDK: **compilou e linkou
  (2026-10-24)**, 1.897 arquivos, 11 no -O0 (os que têm uma função de mais de 15 mil rótulos: `heavy.txt`), 272,6 MB
  sem debug info. O ID (`7f464d46a5a632a7`) bate com o do app, e os 8 símbolos que ele pede ao runtime (`rt_dispatch`,
  `rt_svc`, `rt_udf`, `rt_ticks`, `rt_setjmp_slot`, `hle_longjmp`, `fnprof_hit`, `g_fnprof`) existem no `libmain.so`.
- Tempo: ~4 h de relógio (65 min até ser interrompida, 2 h 56 min ao retomar), dividindo a máquina com outra
  compilação. Os mais lentos são arquivos com funções grandes logo abaixo do limite do -O0: `main_591.c` (9.745
  rótulos) levou mais de 2 h, `main_001.c` (14.661) e `main_584.c` (12.348) perto de 1 h cada. Baixar o limite para
  ~9 mil encurtaria a compilação (no celular também), ao custo de código mais lento nessas funções.

### Fase 3: boot até o primeiro quadro — **feita (2026-10-24)**

Feito sem rodar o jogo (`tools/imports.py <exefs>`):

- **Importações do `main` (679):** 629 vêm do `sdk`, 42 de implementações do runtime (26 do SDK, 16 do `nn::movie`).
  Sobram só os 8 `glslc*` (os mesmos do LAS e do BOTW, nunca chamados). Nada de rede além do que o BOTW 1.8.2 já
  importa.
- **Serviços:** os mesmos 49 do BOTW 1.8.2.

Levar o jogo ao celular:

- `android/build_romfs.sh Dump/0100F2C0115B6000` faz o `romfs.bin` aqui no PC com `tools/romfs_build.py`, o
  `romfs_build.c` do app em Python. O `cc` do WSL foi tentado primeiro: lendo o romfs pelo `/mnt/d`, levou 31 minutos
  para varrer 9 das 52 pastas. O `tools/test_romfs_build_py.py` confere que os dois dão a mesma imagem, byte a byte
  (uma pasta sintética e `Mals/`, `System/` e `Pack/` do jogo). Só que no Windows o Defender examina cada arquivo que
  ele abre (~50 ms por arquivo, horas para os 304 mil). **O que se usa no Windows:** a imagem do próprio jogo, que o
  hactoolnet grava inteira (`--romfs Dump/0100F2C0115B6000/romfs.bin` no lugar do `--romfsdir`, e um
  `touch Dump/0100F2C0115B6000/.romfs.bin.built`): 17,05 GB em 2 min 53 s. O runtime lê qualquer imagem RomFS pelo
  cabeçalho (`services/fs.c rom_index`), e essa tem o mesmo layout da nossa (dados em 0x200, tabelas depois). Conferida
  contra a pasta extraída (2026-10-24): os 304.243 arquivos com os mesmos tamanhos, e 328 de 328 iguais byte a byte
  (300 sorteados e os 28 vídeos).
- `ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000` manda o exefs, esse `romfs.bin` num arquivo só, os 28
  vídeos e o `totkgame.so`, em vez dos 304 mil arquivos do romfs. Sem o `ROMFS_HOST`, o caminho antigo (romfs/
  inteiro, `romfs.bin` feito no celular).
- O `push_game.sh` agora só dá uma parte por enviada com o marcador `.<parte>.pushed`, depois de conferir o número de
  arquivos, e faz o `romfs.bin` em `romfs.bin.part` (renomeado no fim). Antes, uma pasta não vazia contava como
  enviada: a cópia que caiu no meio (2026-10-24) viraria um `romfs.bin` faltando arquivos.
- O envio com `ROMFS_HOST=1` levou 9 min 43 s (o `romfs.bin` inteiro, os vídeos, o `totkgame.so`).
- O HyperOS do POCO recusa `adb install` em silêncio (`INSTALL_FAILED_USER_RESTRICTED`, sem aviso na tela) até
  "Instalar via USB" ser ligado de novo nas opções do desenvolvedor.

O primeiro boot (POCO F7 Ultra, driver Vulkan da Qualcomm, 2026-10-24), boot a boot (`android/run_boot.sh`):

| Boot | Até onde foi | O que parou | Correção |
|---|---|---|---|
| 1 | `nn::init`, os 10.717 inicializadores do `main`, o índice do romfs (304.243 arquivos), o idioma (pt-BR), o NVN e a janela | `VammManager::AllocateAddressRegion` num gerenciador nunca criado: o SDK só liga a memória virtual (`nn::os::AllocateAddressRegion` / `AllocateMemoryPages`, sobre a região de alias) se o `GetInfo 16` (system resource) não for 0, e o jogo a usa sem perguntar | `GetInfo 16` = o system resource do `main.npdm` (24 MiB no TOTK, 0 no BOTW) |
| 2 | mais 13 threads, mais leituras do romfs | `no function at 0x1001bfabc`: o `memcpy` do jogo volta por `adr x19, volta` ... `br x19`, que o gerador mandava ao despacho | `tools/gen.py`: um `br xN` com `adr xN` na função vira um switch sobre esses alvos (6 `br` do `main`, 4 arquivos C) |
| 3 | idem | `too many regions`: o mapa de memória do kernel tinha 1.024 entradas fixas, e o VammManager mapeia memória física de 128 KiB em 128 KiB | mapa que cresce, regiões físicas vizinhas juntas, `UnmapPhysicalMemory` por faixa e devolvendo a memória (`madvise`) |
| 4-5 | o renderizador de áudio, o `hwopus` | ponteiro nulo: o heap de som do jogo (do tamanho que o `System/HeapSize` dele diz) acabou | o `GetWorkBufferSize` do `audren` dizia 16 MB para qualquer renderizador (agora ~1 MB, pelos parâmetros); o `hwopus` virou um serviço de verdade (`runtime/services/hwopus.c`, Opus pelo FFmpeg do app) |
| 6 | saves montados, o relógio | `nn::fs::OpenCacheStorageList`: o `fsp-srv` cmd 62 respondia "ok" sem o objeto | `OpenSaveDataInfoReader*` (60, 61, 62, 68) devolvem um leitor sem saves |
| 7 | **o primeiro quadro: a abertura ("Nintendo apresenta"), a 30 fps, por uns 10 s**; o jogo procura os saves (nenhum), o álbum, toca o áudio | a GPU perdida (*device lost*) depois de ~450 envios | Fase 4 |

Visto no 7º boot e ainda não tratado:

- `am` ICommonStateGetter cmd 66 e ISelfController cmd 62, `prepo:u` cmd 10105 (relatórios de jogo: o stub serve),
  `nfp:user` (amiibo).
- `audren`: os efeitos 2, 5 e 7 (não implementados no mixer) e *wave buffers* "fora da memória do guest" (os
  parâmetros de voz lidos num layout errado para a revisão do TOTK).
- `[nvn] ReadTexels` e `WriteTexels` parcial: não implementados (o jogo os chama em série). (Feitos na Fase 5.)

### Fase 4: gráficos — **a tela título (2026-10-24)**

O 7º boot chegava à abertura e caía quando o jogo passava dela. O que faltava:

- **Instruções Maxwell** (`runtime/nvn/maxwell/translate.c`): `VOTE` (subgroupAll / Any / AllEqual e o ballot, com as
  extensões de subgrupo só nos shaders que o usam), `RED` / `ATOM` (atômicos de 32 bits na memória global, pelo mesmo
  endereço e cópia local do `LDG` / `STG`), `ATOMS` (na memória compartilhada), `VMNMX` (min / max de vídeo: min de
  três, *clamps*) e `LEA` / `LEA.HI` (registrador, cbuf, imediato). O fuzz do `tools/test_translate.c` (8.000
  programas, o GLSL compilado pelo shaderc do WSL) não mostrou falha nova - as 35 que restam são de `textureGather`,
  antigas. Para rodá-lo sem o MSYS2: compilar `translate.c` e o teste no WSL (`-lshaderc -lSPIRV-Tools`) com um stub do
  tradutor de referência, modo `glsl`.
- **Compute shaders com cabeçalho:** os do TOTK começam com um cabeçalho NVN de marca `0x12345679` (os gráficos:
  `0x12345678`) e o código em 0x100; o tradutor lia o cabeçalho como código. Os do LAS e do BOTW não tinham cabeçalho.
- **Driver:** com o da Qualcomm a GPU se perde (*device lost* em `vkQueueSubmit2`, sem informação de falha) logo
  depois da abertura, sempre no mesmo ponto - o rastro (`LARECOMP_VK_DEVLOST_TRACE=1`) não aponta um passe. **Com o
  Turnip (Gen8 V37, o do BOTW) não**: o jogo chega à tela título e fica nela (20-30 fps, 25-40 mil draws e 500-850
  dispatches a cada 2 s). Para testar pelo adb (o app de debug aceita `run-as`): `adb push libvulkan_freedreno.so
  /data/local/tmp/`, `run-as com.totkrecompandroid` para copiá-lo em `files/drivers/Turnip_Gen8_V37/`, e
  `android/run_boot.sh 150 LARECOMP_VK_DRIVER=/data/user/0/com.totkrecompandroid/files/drivers/Turnip_Gen8_V37/libvulkan_freedreno.so`
  (a activity não acha sozinha um driver copiado assim: o caminho tem de vir na variável).
- **A tela título** (Turnip, 2026-10-24): o logo, o fundo animado dos murais Zonai, o menu em português (Continuar /
  Novo jogo / Opções / amiibo), a 30 fps; os controles de toque navegam (o `input tap` do adb é curto demais: `input
  swipe x y x y 300`). Na primeira vez fica preta, só com o indicador de carregamento, por minutos: os shaders sendo
  compilados (o cache de pipelines passa de 5 MB); depois, ~40 s até ela.

Falta:

- O *device lost* no driver da Qualcomm.
- `textureGather` em alguns tipos de textura (`ReadTexels` / `WriteTexels`: feitos na Fase 5).
- A cena só ocupa 16:9 no meio da tela de 20:9 (widescreen: Fase 7).

### Fase 5: áudio, vídeos, saves, entrada — **em andamento**

- **Novo jogo — o prólogo, jogável (Turnip, 2026-10-24).** Antes caía em ~15 s, quando os sons começavam: o TOTK
  manda o `RequestUpdate` do `audren` na **revisão 15** (`REV?`), cujas vozes têm **0x188 bytes**: os dois biquads
  viraram float (ativo, 3 reservados, f32 numerador[3] e denominador[2]: 0x18 cada, não os 0xC dos Q14) e todo campo
  depois deles anda 0x18. Lidas de 0x170 em 0x170, toda voz depois da primeira era lixo (wave buffers em
  `0x380000000000`). Conferido num dump real de uma atualização (`LARECOMP_AUDIO_DUMP=<pasta>`: ids, formatos ADPCM,
  taxas, wave buffers e ids de canal no lugar). Agora: "Nintendo apresenta", "Tears of the Kingdom", a abertura (as
  tochas na caverna sob o castelo) e o **Link controlável** no prólogo, com o HUD e as dicas em português, a ~20 fps.
- **A caverna em magenta — resolvido (2026-10-24).** Não eram os suspeitos do log: o `nvnTextureGetViewOffset` era
  um stub que devolvia 0. O carregador do TOTK descomprime cada mip dos `TexToGo/*.txtg` (um quadro zstd por mip, já
  em block-linear, cortado no último byte usado: o mip 3 de um BC5 128x128 tem 384 bytes, não 256) no deslocamento
  que esse `GetViewOffset` dá para a vista daquele nível; com 0, só o nível 0 ficava na textura e os mips liam zero.
  Em ASTC (os albedos do TOTK) um bloco zerado é bloco de erro: **magenta**. As rochas tinham a textura certa de perto
  e magenta de longe, e o cubemap de ambiente que o jogo captura da própria caverna saía magenta e iluminava tudo de
  magenta. O próprio jogo lia zeros: `ReadTexels` do nível 1 de uma textura BC4 1024x1024 para compor o terreno.
  Como se achou (ferramentas que ficaram): `LARECOMP_DEBUG_FRAME` + `LARECOMP_DEBUG_DIR=/data/user/0/com.totkrecompandroid/files/dbg`
  (os passes num lugar que o `run-as` lê: a pasta externa do app o shell do HyperOS não lê) e `LARECOMP_DEBUG_RT=<n>`
  (o alvo de cor n do passe: o albedo do G-buffer é o 1), `LARECOMP_DUMP_FS=<hash>[,...]` (o GLSL),
  `LARECOMP_LOG_CBUF=<hash>:<bancos>` + `LARECOMP_LOG_CBUF_FROM=<quadro>` (os bancos de constantes),
  `LARECOMP_LOG_ASTC_ZERO=1` (níveis ASTC enviados com blocos zerados) e `LARECOMP_NO_ASTC=1` (decodifica ASTC na CPU:
  separa dado inválido de decodificador).
- Também nesta rodada: `ReadTexels` / `WriteTexels` (com e sem stride) de qualquer região, nível e camada
  (`tegra_copy_region`), e as instruções de ponto flutuante duplo (`DADD`, `DMUL`, `DFMA`, `DMNMX`, `DSETP`, F64 em
  `F2F` / `F2I` / `I2F`) emuladas em fp32 - o Adreno não tem `shaderFloat64`.
- Vistos depois disso (a investigar): as **vozes** dos personagens mudas e o **corpo do Link se deformando**
  (resolvidos, abaixo) e **piscadas** na imagem.
- **O corpo deformado - resolvido (2026-10-25).** Não eram os ossos: o tronco, os braços e a cabeça do Link e quase
  toda a Zelda viravam "varetas" esticadas para um mesmo ponto, e no G-buffer o Link só tinha pernas e espada. O
  skinning de 4 ossos do TOTK (VS `247c7c700`) guarda o endereço da matriz de um osso + 32 e lê as duas primeiras
  linhas em -32 / -16: `LDC c[5][r + 0xFFE0]`. O deslocamento do `LDC` é **com sinal** (s16; Ryujinx `Imm16ToSInt`,
  yuzu `BitField<20, 16, s64>`) e o tradutor o lia sem sinal: o endereço passava 64 KB, o `cbr` o limitava ao último
  vec4 e duas das quatro matrizes saíam lixo. Índices, pesos e paletas conferidos nos dumps (todos dentro, nenhuma
  matriz zerada). `tools/test_translate_ops.sh`: 31 casos (2 de `LDC`), 0 falhas.
  Ferramentas que ficaram: `<LARECOMP_DEBUG_DIR>/dump_now` (criado pelo adb: o F9 no celular, despeja o próximo
  quadro), `LARECOMP_LOG_DRAWS=1` (cada draw do quadro despejado: shaders, formato / deslocamento / stride de cada
  atributo, tamanho de cada cbuf do vertex shader) e `LARECOMP_DEBUG_SWEEP=<fs hash>` + `LARECOMP_DEBUG_FRAMES=<n>`
  (o passe que começa por esse shader, um quadro sem cada um dos seus draws: qual draw estraga a imagem).
- **As piscadas - a causa (2026-10-25).** Quadros escuros (a cena 3D preta, só partículas e HUD) logo depois de cada
  troca de tamanho da resolução dinâmica (1600x896 / 1432x808 / 1280x720): 7 em 20 s de vídeo. No quadro da troca
  (`LARECOMP_DEBUG_SCENE_CHANGE`) o passe do cenário no G-buffer (`be4508`, ~97 draws) e os seguintes **não existem**:
  o jogo nem os pede (não são indiretos - `DrawIndirect` e `MultiDraw*IndirectCount` seguem não feitos, e o TOTK não
  os usa aqui; nenhuma leitura de textura de volta para a CPU). Cada tamanho tem seus objetos de textura no mesmo
  endereço, e aqui cada um é uma imagem Vulkan separada (no Switch, a mesma memória). Contornos medidos:
  `LARECOMP_FAST_COUNTERS=2` (os contadores de tempo de GPU do jogo limitados a 8 ms: o controlador fica em 100 %) tira
  as trocas e os quadros escuros (0 em 20 s) - mas o jogo passa a mirar 30 fps e roda a ~25, então o passo nominal
  dá câmera lenta. Na 1.4.3 os patches de resolução fixa e de passo em tempo real (acima) resolvem os dois.
- **Desempenho medido na caverna:** com o early-Z do NVN e a altura de bloco do NVN, o log mostrou 30,0 fps estáveis
  em trechos onde antes eram 20 (a captura de tela no mesmo teste ainda pegou 20: inconclusivo).
- **As vozes - resolvido (2026-10-25).** Eram duas coisas:
  - **Opus:** o FFmpeg do app não tinha o decodificador (ele precisa do `libswresample`, que o
    `android/deps/build_ffmpeg.sh` deixava de fora) e o `hwopus` devolvia silêncio. Agora o script compila o FFmpeg
    com o swresample numa pasta só do TOTK, `android/deps/ffmpeg` (o `android/deps/out` é o do LASRecompAndroid, uma
    junction: um `libavcodec` que pede o `libswresample` lá impediria os apps do LAS e do BOTW de carregar), e o
    CMake / Gradle a preferem quando existe. As falas são Opus mono a 48 kHz; o ambiente da caverna, 6 canais Opus.
  - **Splitters:** a maior parte dos sons do TOTK vai por *splitters* (a voz com mix `0x7FFFFFFF` e um splitter id;
    o "não usado" do mix é `0x7FFFFFFF`, não negativo, e o aviso que existia nunca disparava): o `audren` os
    descartava. Agora cada canal c da voz vai aos destinos c, c + canais, ... do splitter, cada um ao seu mix com os
    seus volumes (e os sub mixes por splitter, o destino k levando o buffer k % buffers). O layout da REV15, visto nos
    dumps (`LARECOMP_AUDIO_DUMP` grava as primeiras atualizações que mudam splitters, `audren_split_<n>.bin`): `SNDI`
    {id, taxa, n, n ids} + 12 bytes; `SNDD` de 0xA8 bytes (os biquads float antes) com `is_used` em 0x9C. Música e
    ambiente vêm em pares de vozes ADPCM mono meio / lado (volumes (a, a) e (b, -b)) para os mixes 3, 4 e 5.
  - Conferido sem ouvir: `LARECOMP_OPUS_RAW=<pasta>` grava o que cada decodificador Opus decodificou e
    `LARECOMP_AUDIO_WAV=<arquivo>` a mixagem final; a fala está na mixagem com correlação 0,995 (antes 0,10), e
    `LARECOMP_LOG_OPUS=1` mostra o pico de um em cada 200 pacotes.
- Efeitos de áudio 2 (aux), 5 e 7 do `audren`: não implementados.

### Fase 6: Android

- `totkgame.so` com o NDK (`android/build_gamelib.sh Dump/0100F2C0115B6000/exefs`); `romfs.bin` feito direto da pasta
  escolhida; a compilação no celular (`runtime/gamebuild.c`) com 228 mil funções.
- **`romfs.bin` pronto na pasta de dados (2026-10-29, trazido do BOTWRecompAndroid `47270e8` / `66ca80e`):** se a pasta
  escolhida pelo usuário tiver `exefs/` e um `romfs.bin` já pronto ao lado (em vez dos 304 mil arquivos soltos de
  `romfs/`), o app valida o cabeçalho, as tabelas, a contagem mínima de arquivos e o `System/Version.txt` (`1.4.3`)
  direto pelo descritor (`romfs_check_image`), permite tanto copiar (`romfs.bin` em `extracted/`) quanto usar no lugar
  (`romfs.link` apontando para o arquivo `romfs.bin` original, aberto direto em `services/fs.c`), e extrai apenas os
  vídeos soltos (`Movie/*.mp4` / `.webm`) da imagem para `extracted/romfs/` (`romfs_image_extract`).

### Trazido e catalogado de `valentderah/totk-pc-recomp` (2026-10-29)

Análise do repositório [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) (porte Windows/Vulkan
do TOTK 1.4.3 derivado da mesma árvore LAS Recomp + MacNativeGames):

- **Aplicado no runtime (sem alterar `GAMELIB_ID`):**
  1. **Passes sem anexos (attachment-less render passes) — `runtime/nvn/vulkan/exec.c` (crédito: `totk-pc-recomp`
     `7ceda94`):** o TOTK desenha um fragment shader que só escreve num storage buffer usando um viewport 1x1 com
     deslocamento (`x, y`). Aqui `begin_pass()` fixava `16x16` quando não havia alvos de cor/profundidade, `scissor_rect()`
     ignorava `st.scissor[0]` e `draw_()` não reabria o passe quando o canto (`x + w`, `y + h`) do viewport passava de
     `pass_w x pass_h` — deixando o viewport deslocado fora da área do passe e fazendo a pedra da caverna perder a luz
     ambiente do interior (candidato direto para "As paredes só névoa").
  2. **Formatos NVN empacotados de 16 bits (`0x39`..`0x3C`) e correção do off-by-one em `0x95`..`0x9C` —
     `runtime/nvn/formats.c` e `runtime/nvn/vulkan/textures.c` (crédito: `totk-pc-recomp` `c4108c4`):** o NVN enumera
     componentes empacotados de 16 bits do LSB para o MSB (`RGBA4` = `0x39`, `RGB5` = `0x3A`, `RGB5A1` = `0x3B`,
     `RGB565` = `0x3C`), ao contrário dos formatos `PACK16` do Vulkan (MSB para LSB). O TOTK usa `RGB5A1` e `RGB565`
     nos mapas de normais e mistura do terreno (R e B trocados esticavam as texturas do chão num eixo). Além disso,
     `0x95`..`0x98` são todos de 2 bytes (`BGR565`, `BGR5`, `BGR5A1`, `A1BGR5`) e `0x99`..`0x9C` são de 4 bytes
     (`BGRX8`, `BGRA8`, `BGRX8_SRGB`, `BGRA8_SRGB`); `formats.c` e `textures.c` tinham `0x98`..`0x9B` deslocados em 1.
  3. **`SVC 0x2C` (`MapPhysicalMemory`) preservando páginas já mapeadas — `runtime/kernel.c` (crédito: `totk-pc-recomp`
     `runtime/memory.c`):** o kernel Horizon mapeia apenas as páginas ainda não mapeadas do intervalo e preserva o
     conteúdo das que já estavam mapeadas; os heaps do TOTK crescem remapeando faixas que já possuem em parte. Antes, o
     `case 0x2C` chamava `plat_discard(x[0], x[1])` na faixa inteira (zerando páginas já mapeadas), sendo que o
     `SVC 0x2D` (`UnmapPhysicalMemory`) já descarta as páginas ao desmapear.
- **Catalogado para próximas rodadas (quando `totkgame.so` for regerado ou no backend Vulkan):**
  - **Patch de quique da física a 60 fps (`totk-pc-recomp` `tools/totk_patches.py` / `59eb6be`):** em `0x7000562a64`
    (`ldr s0, [x9, #0x28]`, palavra `0xbd402920`), clampar o dt do solver de contato a `<= 1/60 s` quando o passo real
    passa de `1/60 s`, evitando que corpos rígidos quiquem ao pousar em quadros mais longos no modo 60 fps.
  - **Zstd nativo nos call-sites seguros (`totk-pc-recomp` `1f4c6fc`):** substituir apenas os 4 call-sites de
    `ZSTD_decompressStream` (`0x02be2a34`, `0x02dc33dc`) e `ZSTD_decompressDCtx` (`0x028eb388`, `0x02dc3444`) que usam o
    `ZSTD_DCtx` padrão pela `libzstd` nativa, mantendo o `0x02c896d8` (dicionário pré-digerido customizado da Nintendo
    para `zs.dic`) no código recompilado.
  - **Barreiras de compilador e `rt_spin_gate` em laços de spin (`totk-pc-recomp` `c294397`):** `atomic_signal_fence`
    em back-edges de mesmo bloco no gerador (evita que `clang -O1` promova leituras de memória compartilhada para fora do
    spin-loop) e `rt_spin_gate` nos 6 pontos de spin-wait do agendador de tarefas do TOTK (`0x02c279cc`, `0x02c27bb8`,
    `0x02b361dc`, `0x02dcde8c`, `0x02bd64a4`, `0x025db384`).
  - **Write-back de alvos pequenos (`<= 64 KB`) ao passar a cerca (`totk-pc-recomp` `runtime/vk_texture.c`):** o sistema
    de grama do TOTK renderiza um mapa RGBA8 128x128 (64 KB) num pool CPU-cached a cada quadro e lê os bytes na CPU sem
    chamar `nvnMemoryPoolInvalidateMappedRange` (reações de corte de grama e insetos).
  - **Otimizações de replay Vulkan (`totk-pc-recomp` `runtime/vk_exec_draw.c`, `166e5a7`):** máscara de canais usados
    por shader para só checar/vincular os slots lidos (`RsList`), `VK_KHR_descriptor_update_template` e evicção
    incremental de texturas fora do passe crítico.

### Fase 7: desempenho

- Ritmo de quadros, resolução, widescreen: achar os pontos do TOTK (`tools/patches.py`).
- **Trazido do LASRecompAndroid e do BOTWRecompAndroid (2026-10-25)** - o que o motor ganhou lá depois que o BOTW saiu
  do LAS (2026-10-17) e não depende do jogo:
  - **Memória global por ponteiro** (LAS `1883b8d`): o heap do TOTK (2144 MB) passa do alcance de um storage buffer
    no Adreno 830 (2047 MB); acima disso os shaders liam zero, salvo os storage buffers ligados ao draw (as janelas
    do BOTW, `gwin_mode`). Agora, com `bufferDeviceAddress`, todo acesso alcança o heap inteiro; as janelas só sem
    ele. `LARECOMP_VK_GMEM_BDA=0/1`.
  - **UBWC** (LAS `4380fe5`): os render targets listam os formatos das suas vistas e o Turnip os comprime (-8% de
    GPU no S20 FE da medição original). `LARECOMP_VK_FMTLIST=0` desliga.
  - **Cópias de cor preguiçosas** (LAS `d0b1c66`): cópias de imagem inteira viram alias e só se fazem quando alguém
    lê o destino (~1 ms de GPU no LAS). `LARECOMP_VK_COPY_LAZY=0` desliga.
  - **Checagem de texturas** (LAS `05b5f2b`, `e811c6e`): hash completo em 8 trilhas; sem amostra para texturas
    dinâmicas; e cada textura identificada uma vez por conteúdo (o `-1` de "nenhuma" virava 0, "não checada": toda
    textura sem patch era identificada de novo a cada checagem). No Android a amostra fica nos 1024 pontos do BOTW.
  - **Build do código do jogo** (LAS `d2da382`): o maior arquivo pronto primeiro e `-mllvm -dag-maps-huge-region=500`
    (o escalonador quadrático em sequências enormes de stores: 4,3 s -> 0,7 s num arquivo) - no celular e no PC
    (`android/gamelib/CMakeLists.txt`, fora das flags que entram no id do código: o `totkgame.so` atual vale).
  - Correções do tradutor: `LDC.64` com o registrador de endereço também destino (LAS `764e4bf`), `I2I .SAT`, modos
    do `SHF`, `HSETP2 .H_AND` (LAS `d813760`; `tools/test_translate_ops.sh`: 29 casos, 0 falhas). O fuzz GLSL
    (8.000 programas): 4.231 compilados e validados, as 26 falhas são as de `textureGather` de antes.
  - Compatibilidade: sem import do heap por padrão (LAS `4a8749f`, Xclipse / Mali), BC4/BC5 com sinal na CPU (LAS
    `bf48501`), Vulkan sem synchronization2 / estado dinâmico / demote / 16-bit (BOTW `2c08088`), a causa de uma falha
    da preparação dos arquivos na tela (BOTW `ffca023`).
  - Ficaram de fora: o que é do LAS (ganchos do jogo, versões 1.0.1/1.1.0, PhysX, warps, ultrawide), os tamanhos
    próprios dos alvos de brilho/água do LAS (`95c5b52`), o build quente/frio (precisa de uma lista de funções
    quentes do TOTK) e a **geração de quadros** por reprojeção (LAS `4ab0b3d` e seguintes, ~700 linhas com
    heurísticas da cena do LAS) - candidata para depois, com o TOTK a 20-30 fps.

### Fase 8: distribuição

- APK sem nada do jogo.

## Regras

- Nada da Nintendo no repositório: nem o dump, nem código gerado, nem assets (ver `CONTRIBUTING.md`).
- Mudanças no motor que não dependem do TOTK devem ficar fáceis de levar de volta ao LAS e ao BOTW (e vice-versa).
