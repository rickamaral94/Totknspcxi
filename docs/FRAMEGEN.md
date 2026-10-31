# Geração de quadros (lsfg-vk + o Lossless.dll do usuário)

> **TOTK (31/10/2026):** este documento veio do BOTWRecompAndroid. As medições, os testes no Odin 2 Portal e os nomes
> de alvos abaixo são do BOTW 1.8.2. Ainda nada disso foi rodado no TOTK. A parte do TOTK está em
> [No TOTK](#no-totk), no fim.

Estado (30/10/2026):
- **B0** (licenças e contrato da API): feita.
- **B1** (extração dos shaders): feita. Falta o teste com o seu DLL no aparelho.
- **B2** (integração no present): primeira versão feita (2x, 30 → 60), falta o teste no Odin 2 Portal.

Este projeto não inclui, não baixa e não redistribui o `Lossless.dll`, os shaders dele ou o SPIR-V traduzido deles.
O usuário escolhe o próprio DLL, comprado no Steam (Lossless Scaling, app 993090).

## B0 — origem e licenças

Biblioteca: `third_party/lsfg-vk-android`, submodule do
[GameNative/lsfg-vk-android](https://github.com/GameNative/lsfg-vk-android), commit `d33caceb5e26` (23/10/2026). É um
ramo do lsfg-vk **1.0.0** com correções para Android.

| Peça | Usada para | Licença (arquivo lido) |
| --- | --- | --- |
| `framegen/` (LSFG 3.1 e 3.1P) | a geração de quadros | MIT, "Copyright (c) 2025 lsfg-vk" (`framegen/LICENSE.md`) |
| raiz do lsfg-vk-android | só o `trans.cpp` como referência da tradução | MIT (`LICENSE.md`) |
| `thirdparty/dxbc` (o tradutor DXBC→SPIR-V do DXVK, v2.6.2) | traduzir os shaders na importação | zlib/libpng, Philip Rebohle et al. (`LICENSE.md`) |
| `thirdparty/pe-parse` (trailofbits) | ler os recursos do DLL | MIT, Andrew Ruef (`LICENSE`) |
| `thirdparty/volk` | carregar o Vulkan dentro do framegen | MIT, Arseny Kapoulkine (`LICENSE.md`) |
| `thirdparty/toml11` | não usado (é da camada) | MIT |

- **O upstream atual do lsfg-vk não é usado.** Pelo blog de 27/08/2026 do autor, ele passou para CC BY-NC-ND 4.0, que
  proíbe derivados. As versões MIT já publicadas, como a 1.0.0 de que este ramo deriva, continuam MIT.
- **Nenhum shader do Lossless Scaling está no repositório do fork.** Foram procurados `.spv`, `.comp`, `.glsl` e
  arrays de bytes.
  - O único SPIR-V embutido é o `src/extract/mipmaps_replacement_spv.h`, compilado de
    `shaders/mipmaps_replacement.comp`.
  - Esse `.comp` é GLSL escrito por eles "a partir só da interface do passe" (pirâmide de luma R8, 7 níveis), sob a
    MIT do repositório.
  - Motivo: o backend gen8 do Mesa compila errado o original traduzido. A Adreno 740 é gen7.
- **A camada do lsfg-vk não entra:** `src/layer`, `hooks`, `config`, `context` e a UI. Só o `framegen/`, mais
  pe-parse e dxbc na extração.
- **O app LSFG-Android não é usado nem lido.**

## B0 — contrato da API (lido do código, não só do header)

Fonte: `framegen/v3.1_src/lsfg.cpp`, `v3.1_src/context.cpp`, `src/core/{device,image}.cpp` e, como exemplo de uso, a
camada `src/context.cpp`.

### Inicialização

`LSFG_3_1::initializeExternal(gipa, VkInstance, VkPhysicalDevice, VkDevice, queueFamily, VkQueue, isHdr, flowScale,
generationCount, loader)`:
- Usa **o nosso device e a nossa fila**. Não cria segundo device.
  - `generationCount` é o número de quadros gerados (2x = 1).
  - O `flowScale` é passado como `1 / flow_scale` pela camada.
  - Chamar duas vezes não faz nada; é preciso `finalize()` antes.
- O `loader(name)` devolve o **SPIR-V** de cada passe pelo nome:
  - `mipmaps`, `alpha[0..3]`, `beta[0..4]`, `gamma[0..4]`, `delta[0..9]` e `generate`;
  - no 3.1P, os mesmos com `p_`.
- **Nosso device precisa ter, ele mesmo, as features que os shaders usam.** No modo adotado o framegen não habilita
  nada. Ele só registra o que a GPU anuncia e assume que `nullDescriptor` está desligado (usa uma imagem 1x1 de
  reserva).
- **Os shaders traduzidos pelo dxbc declaram:**
  - `OpCapability VulkanMemoryModel`, que exige `vulkanMemoryModel`;
  - leitura e escrita de storage image sem formato, que exigem `shaderStorageImageReadWithoutFormat` e
    `WriteWithoutFormat`;
  - storage images R8, R16G16B16A16_SFLOAT e outras, que exigem `shaderStorageImageExtendedFormats`.
  - **A lista exata sai da B1:** a extração lê as `OpCapability` de cada SPIR-V traduzido e grava a união no
    MANIFEST. Na criação do device, cada capability vira uma feature **obrigatória**; se alguma faltar, o framegen
    não liga.
- **Atenção: o volk do framegen.**
  - O `Device` adotado chama `volkLoadDevice` e depois **zera o ponteiro global `vkCmdPipelineBarrier2`**.
  - O nosso renderer também usa volk, com ponteiros globais.
  - **Se os dois compartilharem símbolos, o nosso `vkCmdPipelineBarrier2` vira nulo.**
  - Por isso o framegen vai num `.so` próprio (`liblsfg_framegen.so`), com o volk dele estático, `-fvisibility=hidden`,
    `-Wl,-Bsymbolic` e um version script que exporta só `LSFG_3_1*` e `LSFG_3_1P*`.
- O framegen chama `std::srand(time)` e usa `std::rand()` para os ids de contexto. O runtime não usa `rand()`
  (conferido com grep).

### Contexto e imagens

`createContextFromImages(VkImage in0, VkImage in1, vector<VkImage> outN, VkExtent2D, VkFormat)`:
- As imagens e a memória delas **são nossas**. O framegen só cria uma view com o `format` dado e não as destrói.
- **Layout:** as imagens são entregues em **`VK_IMAGE_LAYOUT_GENERAL`** e devolvidas em GENERAL.
- **No modo de imagens o framegen não faz barreira nenhuma de entrada ou de saída.** As barreiras de aquisição e de
  liberação só existem para imagens AHB e de fd.
- **Usos:**
  - as entradas são lidas como imagem amostrada (`mipmaps`, `generate`);
  - as saídas são escritas como **storage image** (`generate`);
  - todas precisam de `STORAGE | SAMPLED`, mais `TRANSFER_SRC` nas saídas e o que usarmos para gravar nas entradas.
- **Formato:**
  - precisa de suporte a STORAGE_IMAGE e SAMPLED_IMAGE no tiling ótimo;
  - os formatos do swapchain, B8G8R8A8 e os _SRGB, em geral não têm STORAGE. Ver "Formatos e cor" em B2.
- **Alternância:** o quadro real de número `frameIdx` deve estar em `frameIdx % 2 == 0 ? in0 : in1`. O
  intermediário é gerado entre o real anterior (na outra entrada) e esse.
- **Recursos internos:**
  - 8 slots por contexto (`frameIdx % 8`);
  - pirâmides e imagens de fluxo em R8 e R16G16B16A16_SFLOAT, em `flowScale × extent`.

### Apresentação

`presentContext(id, inSem, outSem[])`:
- **`inSem` e `outSem` são file descriptors** de semáforo (`VK_KHR_external_semaphore_fd`, OPAQUE_FD).
  - Com `inSem = -1`, o framegen **não espera nem sinaliza semáforo nenhum**. É o modo que a própria camada usa no
    Android: os comentários dizem que o Turnip não exporta OPAQUE_FD.
  - **É o modo que usaremos:** `presentContext(id, -1, {})`.
- **Com `inSem = -1`, a ordem vem só da fila.** O framegen grava em `vkQueueSubmit` na **nossa** fila:
  - um command buffer com mipmaps, alpha e beta;
  - por quadro gerado, um com gamma, delta e generate, mais um fence de conclusão.
  - Portanto:
    - **antes de `presentContext`**, as gravações na entrada da vez precisam estar submetidas na mesma fila, com uma
      barreira cujo segundo escopo cubra o compute (escrita de cor ou transferência → `COMPUTE_SHADER`,
      `SHADER_READ`, layout → GENERAL). Uma barreira de pipeline vale para comandos submetidos depois na mesma
      fila;
    - **depois de `presentContext`**, quem lê a saída começa com uma barreira `COMPUTE_SHADER` / `SHADER_WRITE` →
      o estágio da leitura, com GENERAL → o layout da leitura, e devolve para GENERAL ao terminar.
  - **A fila não pode ser usada por duas threads ao mesmo tempo** (`vkQueueSubmit` exige sincronização externa).
    `presentContext` é chamada da mesma thread que submete o resto (a thread de render, em `vk_present_now`).
- **Reuso:**
  - antes de gravar no slot `frameIdx % 8`, o framegen espera os fences de conclusão desse slot de 8 quadros
    atrás, com limite de 2 s. No limite, lança `vulkan_error(VK_TIMEOUT)` **antes** de mudar qualquer estado, o que
    permite repetir;
  - as **nossas** entradas e saídas são reusadas a cada 2 quadros (entradas) e a cada quadro (saídas). A proteção
    é nossa: a entrada `k` só é regravada depois do fence da submissão que leu dela, e a saída só depois que a
    cópia ou desenho dela para a janela terminou.

### Erros

- **Exceções C++:**
  - `LSFG::vulkan_error`, com o `VkResult` em `error()`;
  - `std::runtime_error`, por exemplo shader vazio.
  - Lançadas por `initializeExternal`, `createContextFromImages` (criação de pipelines e imagens),
    `presentContext` (fence de 2 s, contexto inexistente) e `deleteContext`.
  - Nenhuma pode atravessar para o C: todas as chamadas ficam num wrapper C++ com `catch (...)`.
- **`deleteContext` e `finalize`** esvaziam a fila com um fence de até 2 s, sem `vkDeviceWaitIdle` no modo adotado.
- **Device lost:** vem como `VK_ERROR_DEVICE_LOST` nas nossas próprias chamadas (submit, wait), não como exceção da
  biblioteca. Não tem recuperação local; ver "Falhas" em B2.

## B1 — extração (feita; falta o teste com o seu DLL)

Fica em **Vídeo > Geração de quadros (Lossless Scaling) > Lossless.dll**. O seletor do Android entrega o descritor ao
nativo (`android/.../cpp/lsfg_extract.cpp`, `lsfg_translate.cpp`):
- **Leitura:** sequencial até o fim, só em memória, com limite de 512 MB. Funciona também com provedores que não
  fazem seek. O DLL **não é gravado** em lugar nenhum: nem cópia, nem cache.
- **Recursos:** o pe-parse (`ParsePEFromPointer`) lê os recursos RT_RCDATA pelo id. A tabela de nomes e ids
  (`mipmaps` 255, `generate` 256, …; o 3.1P vai até 302) é a do `src/extract/extract.cpp` do fork.
- **Validação em quatro camadas.** Todas precisam passar, senão nada é instalado e o set anterior fica:
  1. **Ids:** os 52 nomes (3.1 e 3.1P) com recurso no DLL. Se faltar algum, o erro é `MISSING`, com os nomes que
     faltaram.
  2. **Tradução:** cada recurso passa pelo dxbc do DXVK (o dxbc grava SPIR-V 1.6). Depois os bindings são
     renumerados na ordem em que aparecem, como no `trans.cpp` do fork. Exceções do dxbc são capturadas.
  3. **SPIR-V válido:** `spvValidate` do SPIRV-Tools, que já vem no `libshaderc_shared` do app, no ambiente Vulkan
     1.3.
  4. **Bindings:** conferidos contra o que o framegen dá a cada shader: buffers uniformes, samplers, imagens
     amostradas e storage images. A tabela `lsfg_expect.inc` é **gerada** dos fontes do framegen por
     `tools/lsfg_expect.py`. Também é exigido um entry point GLCompute.
- **Instalação transacional:**
  - os arquivos vão para `filesDir/lsfg/spirv.importing/<nome>.spv`, mais um `MANIFEST` com o formato, a versão do
    extrator, os commits do lsfg-vk-android e do dxbc, o SHA-256 e o tamanho do DLL, o número de shaders, as
    `OpCapability` e a marca `complete`. Cada arquivo e o diretório levam `fsync`;
  - depois vem a troca `spirv` → `spirv.old`, `spirv.importing` → `spirv`, e o `spirv.old` é apagado;
  - no início do app, `lsfg_startup` apaga o stage e, se o app morreu entre os dois renames, devolve o `spirv.old`;
  - um MANIFEST de outra versão do extrator conta como "não importado".
- **Estado no menu:** "Não importado", "Importando…", "Shaders prontos — 52 shaders, Lossless.dll <sha>" ou
  "Incompatível — <motivo>". O botão **Remover shaders** apaga `filesDir/lsfg/`.
- **Log:** `[lsfg] Lossless.dll: result N, …, sha256 …` e as capabilities dos shaders. As capabilities são as features
  que a B2 vai exigir do device.
- **Testes de host:** `tools/lsfg_extract_tests`, com ASan e UBSan. Usam um PE32+ sintético com recursos falsos e um
  tradutor falso que gera SPIR-V com os bindings pedidos. **Nenhum arquivo do Lossless Scaling** é usado. Cobrem:
  - instalação completa;
  - id faltando, tradução falhando, bindings errados e validador recusando, sempre com o set anterior intacto;
  - não-PE, PE truncado em vários pontos e bytes trocados;
  - parser de SPIR-V com prefixos e bit flips;
  - recuperação: stage interrompido, troca cortada e versão do extrator;
  - o dxbc real com lixo, que precisa falhar de forma limpa.
- **Não verificado:** o caminho completo com um Lossless.dll real, em particular se o `spvValidate` aceita tudo o que
  o dxbc gera. Se recusar, a mensagem com o shader e o motivo aparece no menu e no log.

## B2 — especificação (para revisão; nada implementado)

### Escopo da primeira entrega

- **2x, SDR, 30 reais → 60 apresentados, multiplicador fixo, LSFG 3.1** (o 3.1P fica para depois).
- **Ligar exige reiniciar o jogo.**
  - As features obrigatórias (a lista do MANIFEST) só são pedidas na criação do device, e só se a opção estiver
    ligada **e** os shaders forem válidos.
  - Com a opção desligada, a criação do device é **idêntica** à de hoje: mesmas extensões, mesmas features e as
    mesmas linhas `[vk-cap]`.
- **Desligar ao vivo:**
  - espera os fences das submissões do framegen;
  - faz `deleteContext` e `finalize`, libera as imagens e volta ao caminho direto;
  - o device mantém as features até o fim da sessão.
- 3x/4x, flow scale ao vivo e 3.1P ficam para depois dos testes no Odin 2.
- **Taxa da tela:** o aparelho de teste é o Odin 2 Portal, com tela de **120 Hz**.
  - Ali o 2x pode partir de 60 (60 → 120) e o 3x de 40 (40 → 120). O 30 → 60 com 2x continua sendo o primeiro passo.
  - Hoje o app não pede uma taxa de atualização ao Android: nem `ANativeWindow_setFrameRate`, nem
    `preferredDisplayModeId`. A tela pode ficar em 60 Hz mesmo podendo 120.
  - Antes de oferecer saídas acima de 60, a B2 precisa:
    - registrar no log a taxa real da tela;
    - pedir 120 Hz quando a saída do framegen passar de 60;
    - limitar o multiplicador ao que a tela entrega.

### Contrato temporal (2x)

Notação: `R(n)` são os quadros reais; `I(n-1,n)` é o intermediário entre `R(n-1)` e `R(n)`; o período de saída é
`P` = 1/60 s.

- **Ordem na tela:** `… R(n-1) → I(n-1,n) → R(n) → I(n,n+1) → R(n+1) …`, cada um por um `P`.
- **Quando cada um fica pronto:**
  - `R(n)` fica pronto quando a tradução do quadro n termina (`vk_present_now`);
  - `I(n-1,n)` só pode ser gerado depois disso. Então `R(n)` é apresentado **um `P` depois** do que seria sem
    framegen, e `I(n-1,n)` ocupa esse `P`.
  - Latência adicional: 1 `P` (16,7 ms) mais o tempo de GPU do framegen (medido pela Fase M, rótulo `framegen`).
- **Agendamento:**
  - em `vk_present_now(n)`, submeter a gravação de `R(n)` na entrada `n % 2` e chamar `presentContext`;
  - apresentar `I(n-1,n)` (saída mais overlay) com alvo `t0`;
  - apresentar `R(n)` (entrada mais overlay) com alvo `t0 + P`.
  - O `t0` vem do grid do `framepace`: o tempo de present que ele já calcula para o quadro real.
  - A FIFO do swapchain espaça os dois. A thread de render **não dorme** até `t0 + P`: apresenta os dois em seguida
    e deixa a fila do swapchain segurar, como a camada faz. Os quadros em fila ficam limitados (abaixo).
- **Primeiro quadro de uma sequência:** sem `R(n-1)` válido, `R(n)` é apresentado sozinho e só grava a entrada. O
  próximo já gera.
- **Real atrasado:** o slot do intermediário pertence ao par que já chegou. Se `R(n+1)` não chegar a tempo, a tela
  repete `R(n)` (a FIFO mantém a última imagem). Nada é gerado sem o par completo.
- **Descartes, para a fila não crescer:** antes de apresentar `I(n-1,n)`, verificar:
  - se o momento atual já passou de `t0 + P/2`, ou seja, `R(n)` chegou atrasado; ou
  - se a fila do swapchain já tem mais de 1 imagem nossa pendente (aquisição que demoraria).
  - Em qualquer dos casos, **descartar `I(n-1,n)` e apresentar só `R(n)`**. Descartes são contados.
- **Política de base:**
  - em gameplay, o `framepace` mira 30, e cada real gera 1 intermediário;
  - **menus, pausa, mapa, eventos e filmes** mantêm a política deles (30 hoje). Nesses estados o framegen fica
    **pausado** e só apresenta reais;
  - o alvo de 60 com a opção ligada **não** muda para 30 fora do gameplay.
- **Reset do histórico** (o próximo real conta como primeiro): entrada e saída de menu, evento ou filme;
  carregamento (tela de loading); recriação do swapchain; mudança da resolução de saída; e um salto de mais de 3 `P`
  entre reais (pausa).

### Sincronização, formatos e recursos

- **Formatos e cor:**
  - as imagens do framegen são criadas com **`VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`**, armazenamento
    **`R8G8B8A8_UNORM`** e uma lista de formatos de view {`R8G8B8A8_UNORM`, `R8G8B8A8_SRGB`}, se o swapchain for
    SRGB;
  - o framegen recebe e escreve a view UNORM, com valores já codificados como na tela. Interpolar valores
    codificados em gama é o que a camada faz sobre o swapchain, então o resultado é o mesmo;
  - nós desenhamos na entrada com a view do **mesmo** formato de cor do swapchain (SRGB → view SRGB);
  - para apresentar, copiamos com um desenho de tela cheia que amostra com a view do mesmo formato. Assim **não há
    conversão de gama** e a ordem R/B é tratada pelo amostrador;
  - na criação, conferir STORAGE e SAMPLED no `R8G8B8A8_UNORM`. Sem suporte, o framegen não liga.
- **Conjuntos:**
  - 2 entradas (exigência da API);
  - **2 conjuntos de saída**: o I do quadro n é lido pela janela enquanto o framegen já pode escrever o do quadro
    n+1;
  - **fence por quadro real**, guardado com a entrada e a saída usadas. Antes de regravar a entrada `k`, esperar o
    fence do quadro que leu dela (o framegen também espera os seus);
  - no caminho normal o limite é 2 quadros reais em voo, o mesmo do renderer de hoje (`FRAMES`).
- **Barreiras, por quadro real n:**

| Passo | Fila | Barreira |
| --- | --- | --- |
| Desenhar o jogo na entrada `n%2` | a nossa | GENERAL → COLOR_ATTACHMENT; depois COLOR_ATTACHMENT_OUTPUT/COLOR_WRITE → COMPUTE_SHADER/SHADER_READ e → FRAGMENT_SHADER/SHADER_READ, layout → GENERAL |
| `presentContext(id,-1,{})` | a nossa (submissão do framegen) | nenhuma (ordem da fila) |
| Desenhar I na janela | a nossa | saída: COMPUTE_SHADER/SHADER_WRITE → FRAGMENT_SHADER/SHADER_READ, GENERAL → SHADER_READ_ONLY; ao fim, de volta a GENERAL |
| Desenhar R na janela | a nossa | entrada: já em GENERAL legível (amostrada em GENERAL) |

- **Recriação:** swapchain, resolução, desligar ou finalizar seguem esta ordem:
  1. esperar os fences dos quadros em voo e o fence do framegen, via `deleteContext`, que esvazia a fila;
  2. destruir o contexto;
  3. destruir as nossas imagens.

### Falhas

- **Recuperáveis**, desligam o framegen nesta sessão, mostram o motivo em Configurações e no log, e o jogo segue:
  - exceção do framegen (`vulkan_error` ou `runtime_error`), incluindo o fence de 2 s;
  - feature obrigatória ausente;
  - shaders ausentes ou inválidos;
  - formato sem suporte.
- **Não recuperáveis:**
  - `VK_ERROR_DEVICE_LOST` ou travamento do driver. O tratamento existente de perda do device segue igual;
  - além disso, uma marca em `files/lsfg/disabled_after_device_lost` mantém a opção **desligada** na próxima sessão,
    com o aviso "a geração de quadros foi desligada porque o driver perdeu o device".

### Critério de sucesso (medido com a Fase M)

- Intervalo entre presents, com p50, p95 e p99 perto de 16,7 ms em gameplay.
- Contagem de quadros **realmente apresentados**, reais e gerados, mais descartes. O esperado é gerados ≈ reais
  menos descartes.
- Tempo de GPU do framegen por quadro (timestamps com o rótulo `framegen`).
- Latência adicional medida (present do real com e sem framegen).
- O contador "X reais / Y totais" sozinho não comprova nada.

### Overlay e HUD

- O ImGui (controles de toque, menus e contador) é desenhado **depois**, por cima de cada imagem apresentada, real
  ou gerada. Nunca é interpolado.
- O HUD do próprio jogo está na imagem do jogo, então **é interpolado**. É uma limitação conhecida da primeira
  versão. Uma melhoria futura é a máscara por diferença com a cópia de antes do HUD (`FRAMEGEN-AVALIACAO.md`).

## B2 — implementação (30/10/2026)

Código:
- `runtime/nvn/vulkan/framegen.c`: o caminho do present;
- `android/.../cpp/lsfg_framegen_c.{h,cpp}`: a interface C do framegen, em `liblsfg_framegen.so`;
- `device.c`: as features e o desvio em `vk_present_now`;
- `exec.c`: o `vk_present_draw` em dois passos;
- `hle/framepace.c`: base 30;
- `menu.mm` e `perf_overlay.mm`.

### Isolamento

- O `liblsfg_framegen.so` tem o volk próprio e só exporta `lsfgfg_*`: há version script, visibilidade oculta e
  `-Bsymbolic`. Conferido com `llvm-nm`: nenhum `vk*` ou `volk*` importado ou exportado.
- Nenhuma exceção C++ sai dessa interface.

### Device

- Com **Vídeo > Geração de quadros 2x** ligada e os shaders instalados no início do app, o device é criado com:
  - `vulkanMemoryModel`, mais `DeviceScope` se o driver tiver;
  - `shaderStorageImageWriteWithoutFormat`, mais `ReadWithoutFormat` se o driver tiver;
  - `shaderStorageImageExtendedFormats`.
- Capability desconhecida no MANIFEST, ou feature ausente, deixam a opção indisponível, com o motivo no menu.
- Com a opção desligada, a criação do device é a mesma de antes.

### Diferenças em relação à especificação

- **Um conjunto de saída, não dois.**
  - O quadro I é lido, e a janela recebe a imagem, antes de o próximo `presentContext` ser submetido na mesma fila.
  - Barreiras de memória completas no início de cada command buffer nosso ordenam as leituras e as escritas: as do
    framegen depois das nossas, e as nossas depois das dele.
- **Ritmo por espera, não pela FIFO.**
  - A swapchain é MAILBOX (`present_mode`): duas apresentações seguidas fariam a segunda substituir a primeira.
  - Então I vai à janela assim que fica pronto, a thread de render espera um período P (metade do intervalo medido
    entre quadros reais, entre 6 e 25 ms), e só então vai B.
  - I é descartado se a última imagem mostrada for de menos de P/2 atrás.
- **Desenho em vez de cópia.** A swapchain não tem `TRANSFER_DST` (`vk_backend_swapchain_usage`), então as imagens
  do framegen vão para a janela pelo pipeline do present.

### Formato

- `R8G8B8A8_UNORM`: storage, sampled e color attachment, conferidos no driver.
- A janela precisa ser UNORM (B8G8R8A8 ou R8G8B8A8): os bytes passam sem conversão de gama. Se não for, a opção fica
  indisponível.

### Medições

- `[fg]` a cada 2 s, durante a medição de desempenho ou com `LARECOMP_FG_LOG=1`:
  - quadros reais, gerados e descartados;
  - tempo de GPU do framegen, por timestamps antes e depois do trabalho dele na fila;
  - período de saída.
- O "Medir desempenho" conta cada imagem apresentada; o intervalo entre presents deve ficar perto de 16,7 ms.
- O overlay mostra "framegen 2x: X real / Y shown".

### Primeiro teste no Odin 2 Portal (30/10, SoC no modo padrão, 1920x1080, flow scale 100%)

- **Funcionou**, sem crash: ~1 quadro gerado por quadro real, quase sem descartes.
- **O framegen custa ~9,8 ms de GPU por quadro gerado.** Somado aos ~19 ms do jogo, os quadros reais caíram para
  ~25 por segundo, e a tela mostrou ~49.
- **O que mudou por causa disso:**
  - **Vídeo > Escala do fluxo da geração de quadros:** 100%, 75% ou **50% (o novo padrão)**. O movimento é estimado
    nessa fração do tamanho da tela; o framegen recebe `1 / escala`, como na camada do lsfg-vk.
  - **Modo desempenho:** LSFG 3.1P, com os shaders `p_*` que o extrator já instala.
  - **Os dois valem na hora:** o framegen é finalizado e iniciado de novo entre dois quadros.

### Ritmo por tempos de apresentação (30/10, à noite)

- **O problema:** a espera de um período entre I e B prendia a thread de render, que não traduzia o quadro seguinte
  nesse tempo. Isso explica parte dos ~26–28 quadros reais por segundo, em vez de 30, e a irregularidade (p95 ~25 ms).
- **O que mudou:** com o framegen, o device habilita `VK_GOOGLE_display_timing` quando a plataforma oferece (é o
  swapchain do Android). A swapchain passa a FIFO, com uma imagem a mais.
- **Como fica o ritmo:**
  - I e B vão para a fila juntos, com tempos de apresentação um slot de 60 Hz um do outro, e a thread de render não
    espera;
  - a grade segue os quadros reais: um slot já passado, ou mais de dois períodos à frente, recomeça a grade a partir
    de agora.
- **Sem a extensão,** fica o caminho por espera de antes. O log diz qual vale, em
  `[fg] presentation times (VK_GOOGLE_display_timing): yes/no`.
- **Desligar a opção** faz a swapchain ser recriada em MAILBOX antes do quadro seguinte. Ligar de novo a recria em
  FIFO.
- **Resultado no Odin 2 Portal** (60 s, escala do fluxo 50%, SoC no modo padrão):
  - `presentation times: yes` (present mode 2, FIFO);
  - **30 quadros reais por segundo** (60–61 a cada 2 s) na maior parte da janela, contra 26–28 antes, sem descartes;
  - framegen GPU ~4 ms por quadro;
  - no trecho final, mais pesado, 25–26 reais;
  - na FIFO, o `vkQueuePresentKHR` às vezes bloqueia (média de 5,7 ms), mas a thread de render ainda sobra 38% do
    tempo ociosa.
- **O intervalo que o diagnóstico mostrava era entre as chamadas de present,** não entre o que a tela exibiu. Agora,
  com `VK_GOOGLE_display_timing`, o relatório também lê do compositor os tempos reais (`actualPresentTime`), na linha
  "interval between images as the screen showed them": o critério de cadência da especificação.
- **Cadência real** (60 s, o compositor via `actualPresentTime`): **p50 16,72 ms**, média 17,4, p95 33,4, p99 41,8.
  - A mediana está no alvo de 60 Hz.
  - Os ~5% de intervalos de 33 ms são imagens que perderam o slot e ficaram dois vsyncs na tela. Acontecem sobretudo
    no trecho mais pesado, onde os quadros reais caem para ~28,5 por segundo.
  - 30 quadros reais por segundo no resto da janela, sem descartes.
  - **Impressão do usuário:** "até que está fluido".

### 3x e 4x (30/10)

- **Vídeo > Multiplicador da geração de quadros:**
  - **2x** (30 → 60);
  - **3x** (40 → 120);
  - **4x** (30 → 120).
- **Ritmo:** o `framepace` mantém o jogo na base do multiplicador. O framegen é criado com `generationCount = mult - 1`
  e uma imagem de saída por quadro gerado. Os gerados entram na fila em ordem, um slot da grade de saída cada (1/120
  s), e B entra no slot seguinte.
- **120 Hz:**
  - o app pede a taxa ao Android (`ANativeWindow_setFrameRate`, compatibilidade FIXED_SOURCE) e lê o que conseguiu no
    ciclo de refresh da swapchain (`vkGetRefreshCycleDurationGOOGLE`, a cada segundo; `[fg] the screen refreshes at
    ... Hz`);
  - sem tempos de apresentação, ou com a tela abaixo da taxa, o app volta a 2x e o menu diz por quê;
  - ao desligar a opção, o app deixa de pedir uma taxa.
- **Swapchain:** com o FIFO, ganha `mult - 1` imagens a mais, e é recriada quando o multiplicador muda.
- **Custo:** cada quadro gerado custa ~4 ms de GPU com escala do fluxo de 50%.
  - No 3x, a base de 40 com 2 gerados (≈ 19 + 8 ms) tende a não caber em 25 ms no modo padrão do SoC.
  - O 4x, com base de 30 e 3 gerados (≈ 19 + 12 ms), fica no limite dos 33 ms.
  - A grade se ajusta, mas a medição no aparelho é o que decide.
- **Primeiro teste do 4x no Odin 2 Portal** (60 s, escala do fluxo 50%, SoC no modo padrão):
  - o Android deu 120 Hz (`frame rate asked of the window: 120 Hz (0)`, `the screen refreshes at 120.0 Hz`);
  - o framegen custou 8 a 11 ms de GPU por quadro real (os 3 gerados juntos, ~2,7 a 3,8 ms cada);
  - **quadros reais:** ~25 por segundo e, num trecho mais pesado, ~20, contra a base de 30. A GPU não fecha os 33 ms
    (jogo ~19 ms + framegen);
  - **na tela:** ~100 imagens por segundo e, no trecho pesado, ~80, com mediana de 8,37 ms por imagem, a de 120 Hz.
- **A média da cadência real desse teste não vale.** O Android guarda poucos tempos passados, e o intervalo era
  calculado por cima das imagens que faltavam. Agora só conta entre `presentID` consecutivos.
- **Conclusão provisória:** com o SoC no modo padrão, o 2x (30 → 60 cravado) é o mais estável; o 4x mostra mais
  imagens, com a base abaixo de 30. Os dois ficam disponíveis.
- **Escolha do usuário (30/10):** no Odin 2 Portal, com o SoC no modo padrão, o **2x** ficou melhor na tela que o 4x.
  - Segue como padrão.
  - 3x e 4x ficam como opção para quem tiver GPU com folga ou rodar com clocks mais altos.


### Resolução dinâmica com framegen (30/10)

No trecho pesado, com 2x, os quadros reais caíam para ~28,5/s, e ~5% das imagens ficavam dois períodos na tela. Isso
acontece porque o quadro real perde o slot dele. A **Resolução interna = Dinâmica** (`nvn/output_scale.c`) olhava só
o tempo de GPU do jogo. O trabalho do framegen é enviado à parte e não entrava nessa conta, então a escala só caía
tarde demais.

Agora o orçamento de GPU do quadro real desconta o custo do framegen:

`want_gpu = max(want/2, want − fg_gpu_ms_per_real())`

- O custo é a média dos timestamps do framegen em janelas de 2 s, com EWMA de 0,5.
- O limiar "lento" e o de "folga" passam a usar `want_gpu`.
- Com o framegen desligado, o custo é 0, e o comportamento fica igual ao de antes.
- A linha `[dynres]` mostra `gpu X ms of Y`, onde Y é esse orçamento.

**Resultado no Odin 2 (30/10, 2x, Dinâmica):**

- A cadência na tela melhorou: o p95 caiu de 33 para 25 ms, o p50 ficou em 16,73 e o p99 em 33,5 ms.
- A escala, porém, desceu até 0,55 sem ganho real: os quadros continuaram em ~36 ms no trecho pesado.

**Por que a escala desceu sem ganho:** o tempo de GPU por quadro que a dinâmica usava chega em lotes, porque os
timestamps só são lidos quando o slot da submissão é reutilizado. Os quadros que liam 0 ficavam fora da média, e
isso a dobrava: ~23 ms, contra ~13 ms medidos pelo diagnóstico, que somam ~17 ms com os 4,5 ms do framegen. As
janelas só com zeros caíam no critério de "só tempo de quadro" e baixavam a escala mesmo assim.

**Correção:** a GPU agora é medida pela janela inteira (o total ocupado no início e no fim, dividido pelos quadros).
Uma janela sem leitura nenhuma não decide nada.

**Conclusão:** o trecho pesado é limitado pela CPU, e não pela GPU.

- A thread principal espera 9 ms por quadro em `ArbitrateLock`/`WaitForAddress`, aguardando as threads de trabalho
  do jogo.
- O cluster principal fica em média a 1,9 GHz de 3,2 GHz no modo padrão do SoC.
- Baixar a resolução não ajuda nesse caso.

### Máscara do HUD (31/10)

**Problema:** os quadros gerados entortam e borram o HUD (corações, minimapa, textos), porque o HUD faz parte da
imagem interpolada.

**Solução** (Vídeo > "Máscara do HUD na geração de quadros", ligada por padrão; `framegen_hud_mask` no ini):

1. **Captura** (`exec.c` `hud_capture`): o jogo desenha o HUD na imagem final depois da cena 3D, com os layouts do
   nn::ui2d. Esses desenhos têm a projeção da câmera do layout no início das constantes de vértice, com `[12..14] =
   0, 0, -1`, a mesma assinatura que o widescreen usa. No primeiro desenho desses em um alvo RS_OUT do tamanho da
   imagem apresentada, o passe é encerrado e a imagem é copiada (post image 7).
2. **Máscara** (`vk_fg_hud_mask`, `fg_hud_mask.frag`):
   - é a diferença entre a imagem final e essa cópia, no máximo dos canais;
   - é alargada 2 texels para cada lado, porque os quadros gerados espalham um pouco as bordas do HUD;
   - é suave (smoothstep de 3/255 a 40/255), então um painel translúcido recebe peso parcial;
   - por fim, é desenhada no espaço da janela com o mesmo enquadramento do quadro real.
3. **Composição** (`vk_fg_hud_composite`, `fg_hud.frag`): cada quadro gerado mostra
   `mix(gerado, real B, máscara)`. Onde há HUD aparecem os pixels de B, o quadro real mais novo.

**Limitações e custo:**

- O HUD dos quadros gerados é o de B: ele não se move entre os quadros reais. Isso é o esperado, porque o HUD é quase
  sempre parado.
- Sob um painel translúcido, como o fundo do minimapa, o cenário anda a 30 quadros por segundo.
- Custo: uma cópia da imagem final por quadro real, a máscara e uma composição por quadro gerado, em torno de 1 ms.
- O log do framegen mostra "HUD mask on N real frames" a cada 2 s. Com a máscara funcionando, N fica perto da
  quantidade de quadros reais.

**Primeiro teste** (31/10, Odin 2): "HUD mask on 0 real frames", ou seja, nenhuma cópia da imagem apresentada.

- Os layouts desenham em mais de um alvo RS_OUT do mesmo tamanho. A captura copiava cada um a cada troca de alvo:
  até ~35 quebras de passe por quadro, e os quadros reais caíram de 30 para ~25.
- **Correção:** no máximo uma cópia por quadro, e só de uma imagem apresentada recentemente (as duas texturas da
  janela do jogo se alternam).
- Enquanto não houver cópia, o log mostra a cada 10 s em quais alvos os layouts desenharam e qual imagem foi
  apresentada, para identificar o caso.

**Segundo teste** (31/10): voltou a 30 reais / 60 mostrados nos trechos normais, mas ainda sem cópia. O log de
diagnóstico explicou o motivo:

- os layouts fazem ~98 desenhos por quadro num único alvo 1920x1080 A2B10G10R10 (formato 64);
- a imagem apresentada é outra: uma das duas texturas RGBA8 da janela (formato 37), para onde o jogo converte esse
  alvo.

**Correção:**

- a cópia passa a ser desse alvo, o que recebeu mais desenhos de layout no quadro anterior, desde que tenha o
  tamanho da imagem apresentada;
- a máscara compara o alvo atual com a cópia;
- a cópia é feita sem compressão (como a post image 5), porque a máscara a lê com `texelFetch`.

**Terceiro teste** (31/10): "HUD mask on N real frames", com N igual ao número de quadros reais em todas as janelas.

- Nos trechos normais ficou em 30 reais / 60 mostrados.
- Nos trechos pesados caiu para 24–26 reais, como sem a máscara.
- A máscara funciona e fica ligada por padrão: você confirmou que o HUD fica firme e sem contorno, inclusive no minimapa.

## No TOTK

O código é o do BOTWRecompAndroid (B0, B1, B2, 3x/4x, ritmo pelos tempos de apresentação, máscara do HUD), com estas
diferenças:

- **Ritmo do jogo:** no TOTK o `framepace.c` (o do BOTW 1.8.2) fica desligado. Com a geração ligada:
  - `nvnQueuePresentTexture` (`nvn/objects.c`) segura o jogo numa grade fixa no fps base do multiplicador, 30 ou 40.
    40 não é um número inteiro de vsyncs de 60 Hz, por isso a grade usa tempo, e não vsyncs.
  - `rt_fps60()` fica falso, para o controlador de quadros do jogo não escolher o intervalo 1.
- **Resolução dinâmica:** a meta é o fps base, e o custo de GPU da geração sai do orçamento do quadro real
  (`fg_gpu_ms_per_real`). Coberto em `tools/dynres_tests`.
- **Máscara do HUD:** usa o mesmo sinal do BOTW para achar o HUD, a projeção da câmera das layouts do nn::ui2d
  (`[12..14] = 0, 0, -1` nas constantes do vertex). O widescreen do TOTK já usa esse sinal para os painéis. Ela copia,
  uma vez por quadro, o alvo em que as layouts mais desenharam no quadro anterior, no tamanho da imagem apresentada.
  Ainda não se sabe se o TOTK desenha o HUD num alvo próprio, como o BOTW 1.8.2 faz (1920x1080 A2B10G10R10). Enquanto
  não há cópia, o log mostra os alvos das layouts a cada 10 s (`[fg] HUD mask: no copy ... the layouts drew into:`). É por ali que se confere.
- **Diagnóstico:** com a geração ligada, o resumo conta só os quadros reais (`diag_real_frame`). Os percentis vêm dos
  tempos em que o compositor mostrou cada imagem (`diag_displayed`, `VK_GOOGLE_display_timing`). A linha `run:` diz o
  multiplicador e se a máscara estava ligada.
- **Testes no host:**
  - `tools/lsfg_extract_tests`: a extração do Lossless.dll, 52 shaders;
  - `tools/diag_tests`: as contas da geração de quadros no relatório;
  - `tools/dynres_tests`: o orçamento da resolução dinâmica.

  O APK e a geração no aparelho não foram testados.
