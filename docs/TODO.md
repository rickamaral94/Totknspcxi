# Lista de Tarefas (`TODO`) — TOTKRecompAndroid

Documento consolidado com o que já foi implementado nesta etapa, os testes pendentes no celular e o roteiro técnico a fazer (incluindo os achados mapeados de **[valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)** e do vídeo/logs em [docs/TOTK-PORT.md](TOTK-PORT.md)).

---

## 1. Concluído nesta etapa (2026-10-29)

- [x] **Suporte a `romfs.bin` pronto na pasta do jogo** (trazido do `BOTWRecompAndroid` `47270e8` / `66ca80e`):
  - Detecção de `romfs.bin` ao lado de `exefs/` em [`TOTKRecompAndroidActivity.java`](../android/totkrecompandroid/app/src/main/java/com/totkrecompandroid/TOTKRecompAndroidActivity.java).
  - Validação da imagem (`romfs_check_image`: cabeçalho, tabelas, contagem mínima de arquivos do TotK e `System/Version.txt` `1.4.3`) e extração apenas dos vídeos soltos (`Movie/*.mp4` / `.webm`) em [`runtime/romfs_build.c`](../runtime/romfs_build.c) e [`runtime/romfs_build.h`](../runtime/romfs_build.h).
  - Suporte a **"Usar no lugar"** (`romfs.link` apontando para o arquivo `romfs.bin` original, aberto diretamente em [`runtime/services/fs.c`](../runtime/services/fs.c) e [`runtime/main.c`](../runtime/main.c)) e **"Copiar"** (copiando `romfs.bin` para `extracted/romfs.bin`).
  - JNI e mensagens traduzidas em [`runtime/ui/launcher.mm`](../runtime/ui/launcher.mm) e nos 11 arquivos `runtime/ui/i18n_*.inc`.
- [x] **Correções de runtime trazidas de [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)** (sem alterar `GAMELIB_ID` / sem exigir recompilação do `totkgame.so`):
  - **Passes sem anexos (`attachment-less render passes`)** em [`runtime/nvn/vulkan/exec.c`](../runtime/nvn/vulkan/exec.c) (crédito: `totk-pc-recomp` `7ceda94`): dimensionamento pelo canto final `(x + w, y + h)` do viewport/scissor em `noatt_dims()`, interseção correta do `st.scissor[0]` em `scissor_rect()` e reabertura do passe quando um draw expande a área — evitando que o shader de iluminação que escreve em storage buffer num viewport `1x1` deslocado caia fora do scissor (luz ambiente interior das rochas da caverna).
  - **Formatos NVN empacotados de 16 bits (`0x39`..`0x3C`) e correção do off-by-one em `0x95`..`0x9C`** em [`runtime/nvn/formats.c`](../runtime/nvn/formats.c) e [`runtime/nvn/vulkan/textures.c`](../runtime/nvn/vulkan/textures.c) (crédito: `totk-pc-recomp` `c4108c4`): mapeamento LSB->MSB para `RGBA4`, `RGB5`, `RGB5A1` e `RGB565` (usados pelo TotK nos mapas de normais e mistura do terreno) via `VkFmt.chan`, e alinhamento dos tamanhos/formatos `BGR565`, `BGR5`, `BGR5A1`, `A1BGR5`, `BGRX8`, `BGRA8`, `BGRX8_SRGB`, `BGRA8_SRGB`.
  - **Preservação de páginas já mapeadas no `SVC 0x2C` (`MapPhysicalMemory`)** em [`runtime/kernel.c`](../runtime/kernel.c) (crédito: `totk-pc-recomp` `runtime/memory.c`): removido o `plat_discard` incondicional ao mapear faixas parcialmente já mapeadas pelos heaps do TotK.
- [x] **APK compilado e instalado no POCO F7 Ultra** (`gradlew assembleDebug` + `adb install -r`).

---

## 2. Testes no Celular (A Fazer)

- [ ] **Testar iluminação no interior da caverna ("As paredes só névoa")**:
  - No teste rápido de 2026-10-29 (`spot_after_75.png`), o save mais recente na pasta `save/` do aparelho já estava na saída da caverna / Grande Ilha Celestial (onde o jogo rodou a ~20–23 fps com 184 mil draws/2 s).
  - Para testar especificamente o ponto escuro dentro da caverna (ruínas com anel de pedra): preparar uma pasta-semente contendo apenas um dos slots internos da caverna (`slot_00`, `slot_01` ou `slot_02` de 26/10) e rodar com `LARECOMP_SAVE_SEED` + `LARECOMP_SAVE_DIR` para comparar as paredes da caverna antes e depois do fix de `noatt_dims` (`7ceda94`).
- [ ] **Testar importação de `romfs.bin` pela interface do app**:
  - Na tela inicial (`Configurações > Arquivos > Trocar pasta do jogo`), selecionar uma pasta contendo `exefs/` + `romfs.bin` e validar tanto a opção **"Usar no lugar"** quanto **"Copiar"**, conferindo a extração dos 28 vídeos em `extracted/romfs/Movie/`.

---

## 3. Melhorias de `valentderah/totk-pc-recomp` para Aplicar (A Fazer)

### 3.1. No Runtime / Backend Vulkan (não exigem regerar `totkgame.so`)

- [ ] **Prioridade de FPS: evitar cópias de vértices na região de memória virtual / alias do TotK.** Implementado caminho experimental em `runtime/nvn/vulkan/alias_memory.c`, desligado por padrão. A faixa de gameplay `0x27…–0x28…` foi atendida com teto de 768 MB e início relativo de 1792 MB: upload 59,39 → 5,01 MB/quadro; vértices 5,04 → 1,57 ms. **FPS não melhorou de forma consistente (21,02 → 20,72)**; não habilitado automaticamente. Testes da política, regressão do kernel, build e teste curto de movimento passaram. Faltam validação ampla de unmap/reuso/reboot e uma política de seleção/RAM independente da cena. Ver [experimento VAMM](VAMM-FPS-2026-10-29.md) e [perfil inicial](FPS-PROFILE-2026-10-29.md).
- [ ] **Próxima investigação de FPS:** dependências de `CMD_COPY_BUFFER_TO_BUFFER` e custo restante de descritores/constantes. Medir as esperas antes de alterar a sincronização; manter A/B e snapshots de save isolados.
- [x] **Evitar inicialização de constantes do driver que o shader não lê:** limitar cb0 ao tamanho vinculado e evitar tabelas fora desse intervalo. 24.600 comparações byte a byte passaram no celular; APK compilado. A/B mostrou pequena redução de CPU (32,62 → 31,95 ms de tradução por quadro), sem ganho consistente de FPS (19,29 → 19,36). Três outros candidatos sem ganho foram removidos; ver relatório acima.

- [x] **Write-back automático de render targets pequenos (`<= 64 KB`) ao passar a cerca da GPU** (crédito: `valentderah/totk-pc-recomp`, `runtime/vk_texture.c` — `vk_texture_write_back_small`):
  - **Por quê:** O sistema de grama do TotK renderiza a cada quadro um mapa RGBA8 `128x128` (`64 KB`) de grama cortada/pisoteada em um pool `CPU_CACHED` e lê os bytes na CPU sem chamar `nvnMemoryPoolInvalidateMappedRange` (controlando reações de corte de grama e insetos).
  - **Onde aplicar:** [`runtime/nvn/vulkan/textures.c`](../runtime/nvn/vulkan/textures.c) e [`runtime/nvn/objects.c`](../runtime/nvn/objects.c) (no `nvnSyncWait`).
  - **Implementado em 2026-10-29:** aproveitado o caminho existente em `runtime/nvn/vulkan/exec.c`: alvos em pools `CPU_CACHED` ou `CPU_UNCACHED` passam a aceitar até 64 KB, além dos limites anteriores para outros pools e alvos lineares. Os alvos maiores que 4 KB usam os slots grandes existentes, sem aumentar a alocação de staging. A conclusão continua aplicando os resultados em ordem de submissão.
  - `nvnSyncWait` passa a cobrir a submissão dos alvos pequenos acessíveis pela CPU mesmo com sync lag, limitada à cerca aguardada. Política compartilhada em `runtime/nvn/vulkan/writeback_policy.h`.
  - **Validação:** `assembleDebug` passou; os 14 casos de `tools/test_writeback_policy.c` passaram no Android conectado (limites de tamanho, flags de pool e espera com sync lag). O teste de afinidade também passou. A suíte completa continua pendente: `test_nvn_lifetime` não liga sem o stub de `rt_fatal_message`; uma tentativa com stub local também abortou. O stub experimental foi removido.
  - [ ] **Validar em gameplay:** conferir o write-back `128x128 RGBA8` com `LARECOMP_LOG_WRITEBACK=128`, o corte/pisoteio da grama e insetos, e medir o impacto na espera de sync lag. Os testes de política não validam a cópia Vulkan nem a reação da grama.
    - **Teste estático no celular `192.168.1.6:40089`:** APK instalado; 14 testes de política passaram; jogo carregou com saves isolados e sem crash nas duas rodadas (~77 s cada). O alvo `128x128 f0x25 @359dda000` apareceu com a regra ligada e desligada: o diagnóstico confirmou `flags 0x40`, já coberto pelo caminho linear antigo. Os artefatos verticais também aparecem nas duas condições. Corte de grama/insetos e aplicação dos bytes continuam pendentes; ver [relatório do teste](WRITEBACK-TEST-2026-10-29.md). `LARECOMP_WB_CPU_SMALL=0` permite reproduzir o comportamento anterior para comparação.
- [ ] **Buffers de armazenamento (SSBO) lidos por ponteiro de 64 bits no UBO de suporte** (crédito: `valentderah/totk-pc-recomp` commit `05f3643`, `runtime/shader_decode.c` / `runtime/vk_exec.c`):
  - **Por quê:** Shaders do TotK que fazem `LDG`/`STG`/`ATOM` em SSBOs vinculados (`cb0[0x15 + s]`) funcionam sem copiar janelas de memória quando o shader usa `buffer_reference` lendo o endereço base GPU de 64 bits e tamanho no UBO de suporte.
  - **Onde verificar/aplicar:** Comparar com o modo `gwin_mode` / `bufferDeviceAddress` já presente em [`runtime/nvn/maxwell/translate.c`](../runtime/nvn/maxwell/translate.c) e [`runtime/nvn/vulkan/exec.c`](../runtime/nvn/vulkan/exec.c).
- [ ] **Máscara de recursos usados pelo shader (`RsList` / skip de binds não lidos)** (crédito: `valentderah/totk-pc-recomp`, `runtime/vk_exec_draw.c` lines 651–685):
  - **Por quê:** Como o gargalo atual no celular é a CPU da thread de render (~18–29 ms traduzindo draws NVN -> Vulkan), inspecionar apenas os slots de texturas/UBO/SSBO efetivamente referenciados pelos estágios ativos (`ub_mask`, `sb_mask`, `tex_mask`, `img_mask`) reduz o custo por draw.
  - **Onde aplicar:** [`runtime/nvn/vulkan/exec.c`](../runtime/nvn/vulkan/exec.c).
- [ ] **Atualização de descritores com `VK_KHR_descriptor_update_template`** (crédito: `valentderah/totk-pc-recomp`, `runtime/vk_exec_draw.c` lines 166–198):
  - **Por quê:** Substitui a montagem de `VkWriteDescriptorSet[152]` a cada draw que muda texturas/buffers por uma única chamada `vkUpdateDescriptorSetWithTemplate` com layout fixo.
  - **Onde aplicar:** [`runtime/nvn/vulkan/descriptors.c`](../runtime/nvn/vulkan/descriptors.c) e [`runtime/nvn/vulkan/exec.c`](../runtime/nvn/vulkan/exec.c).
- [ ] **Evicção incremental de texturas sob pressão de VRAM** (crédito: `valentderah/totk-pc-recomp` commit `166e5a7`, `runtime/vk_texture.c`):
  - **Por quê:** Evita varreduras completas ou *stalls* de `vkDeviceWaitIdle` ao liberar texturas antigas quando a memória de GPU passa do orçamento (~1,2 GB no celular).
  - **Onde aplicar:** [`runtime/nvn/vulkan/textures.c`](../runtime/nvn/vulkan/textures.c).

### 3.2. No Gerador / Patches do Jogo (exigem regerar `totkgame.so` + espelhar em `runtime/native.c`)

> [!IMPORTANT]
> Qualquer alteração em [`tools/gen.py`](../tools/gen.py) ou [`tools/patches.py`](../tools/patches.py) muda o `GAMELIB_ID` e exige regerar e recompilar o `totkgame.so` (`android/build_gamelib.sh`). Agrupar estas mudanças em uma única rodada de regeneração.

- [ ] **Patch de quique da física Havok a 60 fps** (crédito: `valentderah/totk-pc-recomp` commit `59eb6be`, `tools/totk_patches.py`):
  - **Endereço (TotK 1.4.3):** `0x00562a64` (base `0x7000000000`: `0x7000562a64`, instrução original `0xbd402920` = `ldr s0, [x9, #0x28]`).
  - **Por quê:** Quando o passo de tempo real está ativo no modo 60 fps e um quadro dura mais que `1/60 s`, o solver de contato aplica impulso de recuperação em excesso e corpos rígidos quicam ao pousar. Clampar `s0 = fminf(s0, 1.0f/60.0f)` quando `dt > 1/60 s` elimina o quique mantendo a velocidade do mundo em tempo real.
  - **Onde aplicar:** [`tools/patches.py`](../tools/patches.py) e [`runtime/native.c`](../runtime/native.c).
- [ ] **Zstd nativo (`libzstd`) nos 4 call-sites seguros do carregador** (crédito: `valentderah/totk-pc-recomp` commit `1f4c6fc`, `tools/totk_patches.py` + `runtime/hle_nn.c`):
  - **Endereços (TotK 1.4.3):**
    - `0x02be2a34` e `0x02dc33dc` -> `ZSTD_decompressStream` (`0x02c89710`)
    - `0x028eb388` e `0x02dc3444` -> `ZSTD_decompressDCtx` (`0x02c89340`)
  - **Atenção:** **Não** interceptar a função `0x02c89340` inteira nem o call-site `0x02c896d8`, pois `0x02c896d8` usa o dicionário pré-digerido customizado da Nintendo (`zs.dic`), incompatível com o `ZSTD_DDict` upstream.
  - **Onde aplicar:** [`tools/patches.py`](../tools/patches.py), [`runtime/native.c`](../runtime/native.c) e adicionar `libzstd` no CMake do runtime.
- [ ] **Barreira de compilador em spin-loops (`atomic_signal_fence`) e `rt_spin_gate` no agendador** (crédito: `valentderah/totk-pc-recomp` commit `c294397`):
  - **Por quê:** Em laços de espera ocupada de um único bloco básico, o `clang -O1` pode promover leituras não-voláteis da memória do guest para fora do laço; além disso, os 6 pontos de spin-wait do agendador de tarefas do TotK (`0x02c279cc`, `0x02c27bb8`, `0x02b361dc`, `0x02dcde8c`, `0x02bd64a4`, `0x025db384`) consomem CPU disputando núcleos com a thread de render.
  - **Onde aplicar:** [`tools/gen.py`](../tools/gen.py), [`tools/patches.py`](../tools/patches.py) e [`runtime/kernel.c`](../runtime/kernel.c).

---

## 4. Problemas Visuais e de Áudio Abertos do Porte ([docs/TOTK-PORT.md](TOTK-PORT.md))

- [x] **Grandes faixas verticais da vegetação na saída para a Grande Ilha Celestial:** corrigido o pitch do upload de render targets lineares (`flags 0x40`). O mapa R16 de 260×260 usa 640 bytes entre linhas, e o upload avançava apenas 520 bytes. A leitura agora usa `nvn_linear_rt_pitch`, como o writeback. Comparação no celular e 64 casos de regressão passaram; ver [relatório](GLITCH-PITCH-2026-10-29.md).
- [ ] **Artefatos residuais da vegetação:** validar pequenos fragmentos ainda presentes no céu, distribuição da grama sobre superfícies e outras regiões após a correção do pitch. A correção acima não encerra os demais problemas gráficos.
- [ ] **Paredes só névoa / luz interior das cavernas:** Validar no celular se o fix de passes sem anexos (`7ceda94`) já resolveu ou se a máscara de meia resolução `@35e218200` (canal A = 0 nas paredes) ainda requer correção nos shaders `c0f9071f5ef7027b` / `05a99cd0aa4bcffb` / `899e096727d434c2`.
- [ ] **Água preta e Link recortado dentro d'água:** Verificar após o teste de luz interior (a água lê a cena iluminada e a sonda de ambiente, ou depende de cópia da cena para refração).
- [ ] **Respingos brancos opacos:** Verificar o alvo de partículas/efeitos em meia resolução (`800x448 RGBA16F` em `@35c5e6c00`, que compartilha endereço com o alvo 2 do G-buffer `1600x896 RGBA8`).
- [ ] **Nuvens volumétricas em blocos (no salto das ilhas):** Investigar a reconstrução temporal / checkerboard das nuvens volumétricas em baixa resolução.
- [ ] **Widescreen (20:9):** Mapear os pontos de projeção/aspect ratio da câmera e do HUD no TotK 1.4.3.
- [ ] **Efeitos de áudio 2 (`aux`), 5 e 7 no `audren`:** Implementar no mixer em [`runtime/services/audren.c`](../runtime/services/audren.c) se necessário.
