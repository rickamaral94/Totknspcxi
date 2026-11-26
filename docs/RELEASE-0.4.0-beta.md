**Beta 0.4.0.** Bem menos trabalho de CPU na **Grande Ilha do Céu**, a correção para **celulares de 12 GB** que caíam para menos de 10 fps ([#78](https://github.com/mamcjr/TOTKDroid/issues/78), [#71](https://github.com/mamcjr/TOTKDroid/issues/71)), o fim do **fundo preto nos menus** ([#76](https://github.com/mamcjr/TOTKDroid/issues/76)), o **mapa em tela larga** ([#66](https://github.com/mamcjr/TOTKDroid/issues/66)), a proporção **Esticada** ([#59](https://github.com/mamcjr/TOTKDroid/issues/59)) e várias melhorias para **Mali**.

> **Nenhum arquivo do jogo vem junto.** Você precisa do seu próprio dump de *Tears of the Kingdom* com a atualização **1.4.3** aplicada (`exefs` e `romfs`, ou o `romfs` como um único `romfs.bin`).

Atualiza por cima das versões anteriores sem apagar os saves. **Não desinstale o app antes**: isso apaga os saves.

## O que mudou

- **Desempenho na Grande Ilha do Céu:**
  - Os vértices da ilha ficavam fora da área que a GPU lê direto, e a CPU copiava ~2,5 GB a cada 2 s. Agora são ~250 MB, e o trabalho de vértices da thread de render caiu de ~250 para ~75 ms a cada 2 s (POCO F7 Ultra, Turnip).
  - **Celulares de 12 GB ([#78](https://github.com/mamcjr/TOTKDroid/issues/78), [#71](https://github.com/mamcjr/TOTKDroid/issues/71)):** o Android informa ~11,2 GB, abaixo do limite de 11 GB, e essa área ficava desligada neles. Com isso a CPU copiava de 0,5 a 1,1 GB de vértices a cada 2 s, e o jogo caía para menos de 10 fps depois de alguns minutos. O limite agora é 10 GB.
- **Menos travadas enquanto os shaders são montados** (do BOTWRecomp):
  - Um desenho que precisa esperar o seu shader passa à frente da fila.
  - O formato dos vértices virou estado dinâmico, então há menos pipelines para compilar.
  - O resultado da tradução de cada shader fica salvo ao lado do SPIR-V (`.smeta`). Da segunda vez em diante, a espera pelos shaders caiu de ~0,9 s para ~0,17 s numa partida.
- **Menus com fundo preto ([#76](https://github.com/mamcjr/TOTKDroid/issues/76)):** em algumas aberturas da roda de armas, escudos, flechas e poderes, o jogo não desenha o mundo no quadro que ele copia para o desfoque do menu, e o fundo saía preto. Agora o último quadro do mundo é mantido para essa cópia. No teste, com a roda aberta muitas vezes, não houve nenhum fundo preto.
- **Tela larga:**
  - **O mapa ([#66](https://github.com/mamcjr/TOTKDroid/issues/66)):** com a proporção Livre, os ícones e nomes do mapa andavam junto com a câmera. Agora eles ficam nos seus lugares, como no BOTWRecomp.
  - **Proporção Esticada ([#59](https://github.com/mamcjr/TOTKDroid/issues/59)):** uma terceira escolha em *Vídeo > Proporção da tela*. Ela estica a imagem 16:9 até preencher a tela, sem faixas em telas 4:3 e outras mais estreitas.
- **Vídeo, opções novas:**
  - **Taxa de quadros > Sem limite (até 120 fps):** até a taxa de atualização mais alta da tela (no máximo 120 fps). As cenas continuam a 30.
  - **Sombras:** desligadas, o jogo é desenhado sem sombras (A/B a 60 fps: 52,4 -> 54,4 fps). Vem ligada.
  - **Cascatas da sombra do sol** e **Sombras de luzes pontuais:** limites opcionais nessas sombras.
  - **Manter 60 fps em quedas curtas:** o jogo só cai para 30 depois de 7 quadros atrasados, não 3. Vem desligada.
- **Mali:**
  - O modo *Automático* das leituras do alvo de desenho no lugar agora vale para Mali também. No primeiro santuário, uma Mali-G720 copiava o alvo ~4.000 vezes por quadro ([#73](https://github.com/mamcjr/TOTKDroid/issues/73)).
  - As texturas BC agora são decodificadas em quatro partes ao mesmo tempo.
  - Menos cópias no modo de cópia: vértices, tabelas e compute em sequência.
  - Do Swap2-Native: a profundidade que um passe só lê não é gravada de volta, e desenhos sem saída de cor ficam sem fragment shader.
  - **Para testar (opcional):** o heap do jogo em blocos de AHardwareBuffer, para o driver de sistema da Mali. Coloque `LARECOMP_VK_MEMFD=1 LARECOMP_VK_MEMFD_AHB=3` (separadas por espaço) em *Variáveis do driver*. Na Mali simulada num POCO, foram 26,4 fps contra 16,5. Se testar, mande o log.
  - Tudo isso foi testado simulando Mali num Adreno 830, não numa Mali de verdade.
- **Drivers Qualcomm 891.x no Android 15:** caíam no driver do sistema sem avisar. Agora carregam.
- **Outras:**
  - Um aviso no log quando nenhum quadro termina por 5 s, dizendo onde cada thread está.
  - Os caches de programas e pipelines crescem, em vez de travar quando enchem.
  - A economia de RAM devolve as texturas sem zerá-las, o que acaba com o magenta ([#57](https://github.com/mamcjr/TOTKDroid/issues/57)).
  - Um sensor de digital não conta mais como controle (ele escondia os controles touch).
  - O modo 40/45 fps pede a taxa da tela a partir da mais alta que ela oferece.
  - O overlay completo mostra o nome do pacote do driver Turnip.
- **Permissão de internet:** o app agora pede a permissão INTERNET. Ela serve só para baixar o banner da tela inicial (o agradecimento pelas 300+ estrelas), que antes nunca aparecia.

Não testados num celular: a decodificação BC e o heap em blocos numa Mali de verdade, e a correção dos 12 GB num celular de 12 GB. O POCO usado nos testes tem mais memória, e a causa foi achada nos logs do [#78](https://github.com/mamcjr/TOTKDroid/issues/78).

**Licença:** com a geração de quadros, este APK é distribuído sob a **GPL-3.0-or-later** (a pasta `runtime/nvn/vulkan/lsfg/` vem do Swap2-Native/Eden). O resto do código continua MIT. Veja `NOTICE.md`.

> Como o LAS Recomp, este projeto é feito quase todo com IA (Claude, da Anthropic, pelo Claude Code), dirigido, testado e revisado por uma pessoa.

---

**Beta 0.4.0.** Much less CPU work on the **Great Sky Island**, the fix for **12 GB phones** that fell under 10 fps ([#78](https://github.com/mamcjr/TOTKDroid/issues/78), [#71](https://github.com/mamcjr/TOTKDroid/issues/71)), no more **black background behind menus** ([#76](https://github.com/mamcjr/TOTKDroid/issues/76)), the **map on wide screens** ([#66](https://github.com/mamcjr/TOTKDroid/issues/66)), a **Stretched** aspect ratio ([#59](https://github.com/mamcjr/TOTKDroid/issues/59)) and several improvements for **Mali**.

> **No game files are included.** You need your own dump of *Tears of the Kingdom* with update **1.4.3** applied (`exefs` and `romfs`, or the `romfs` as a single `romfs.bin`).

Installs over earlier versions and keeps your saves. **Don't uninstall the app first**: that deletes the saves.

## What changed

- **Performance on the Great Sky Island:**
  - The island's vertices sat outside the area the GPU reads directly, and the CPU copied ~2.5 GB of them every 2 s. Now it's ~250 MB, and the render thread's vertex work went from ~250 to ~75 ms every 2 s (POCO F7 Ultra, Turnip).
  - **12 GB phones ([#78](https://github.com/mamcjr/TOTKDroid/issues/78), [#71](https://github.com/mamcjr/TOTKDroid/issues/71)):** Android reports ~11.2 GB, under the 11 GB cut, so that area was off on them. The CPU then copied 0.5-1.1 GB of vertices every 2 s, and the game fell under 10 fps after a few minutes. The cut is now 10 GB.
- **Fewer stutters while shaders are built** (from BOTWRecomp):
  - A draw that has to wait for its shader goes to the front of the queue.
  - The vertex layout is now dynamic state, so there are fewer pipelines to compile.
  - Each shader's translation result is saved next to its SPIR-V (`.smeta`). From the second run on, the wait for shaders went from ~0.9 s to ~0.17 s in a session.
- **Menus over a black background ([#76](https://github.com/mamcjr/TOTKDroid/issues/76)):** on some openings of the weapon, shield, bow and ability wheels, the game doesn't draw the world on the frame it copies for the menu's blur, and the background came out black. Now the last frame of the world is kept for that copy. In testing, with the wheel opened many times, there was no black background at all.
- **Wide screens:**
  - **The map ([#66](https://github.com/mamcjr/TOTKDroid/issues/66)):** with the Unlocked aspect ratio, the map's icons and names moved with the camera. Now they stay on their places, as in BOTWRecomp.
  - **Stretched aspect ratio ([#59](https://github.com/mamcjr/TOTKDroid/issues/59)):** a third choice in *Video > Aspect ratio*. It stretches the 16:9 image to fill the screen, with no bars on 4:3 and other narrower screens.
- **Video, new options:**
  - **Frame rate > Unlocked (up to 120 fps):** up to the screen's fastest refresh (at most 120 fps). Cutscenes stay at 30.
  - **Shadows:** off, the game is drawn without shadows (A/B at 60 fps: 52.4 -> 54.4 fps). On by default.
  - **Sun shadow cascades** and **Point light shadows:** optional caps on those shadows.
  - **Hold 60 fps through short dips:** the game only drops to 30 after 7 late frames, not 3. Off by default.
- **Mali:**
  - The *Automatic* mode of in-place reads of draw targets now covers Mali too. In the first shrine, a Mali-G720 copied the target ~4,000 times a frame ([#73](https://github.com/mamcjr/TOTKDroid/issues/73)).
  - BC textures are now decoded in four parts at once.
  - Fewer copies in copy mode: vertices, tables and back-to-back compute.
  - From Swap2-Native: depth a pass only reads isn't written back, and draws with no colour output get no fragment shader.
  - **To test (optional):** the game's heap in Android hardware buffer blocks, for Mali's system driver. Put `LARECOMP_VK_MEMFD=1 LARECOMP_VK_MEMFD_AHB=3` (separated by a space) in *Driver variables*. On a simulated Mali on a POCO it ran at 26.4 fps against 16.5. If you try it, please send the log.
  - All of this was tested by simulating Mali on an Adreno 830, not on a real Mali.
- **Qualcomm 891.x drivers on Android 15:** they fell back to the system driver without a word. Now they load.
- **Other:**
  - A log report when no frame finishes for 5 s, saying where each thread is.
  - The program and pipeline caches grow, instead of hanging when full.
  - RAM saving gives textures back without zeroing them, which ends the magenta ([#57](https://github.com/mamcjr/TOTKDroid/issues/57)).
  - A fingerprint sensor no longer counts as a controller (it hid the touch controls).
  - The 40/45 fps mode asks for its rate from the screen's fastest refresh.
  - The full overlay names the Turnip driver package.
- **Internet permission:** the app now asks for the INTERNET permission. It's only used to download the start screen's banner (the 300+ stars thank-you), which never showed before.

Not tested on a phone: the BC decoding and the block heap on a real Mali, and the 12 GB fix on a 12 GB phone. The POCO used for testing has more memory, and the cause was found in the logs from [#78](https://github.com/mamcjr/TOTKDroid/issues/78).

**License:** with frame generation, this APK is distributed under **GPL-3.0-or-later** (the `runtime/nvn/vulkan/lsfg/` folder comes from Swap2-Native/Eden). The rest of the code stays MIT. See `NOTICE.md`.

> Like LAS Recomp, this project is made almost entirely with AI (Claude, by Anthropic, through Claude Code), directed, tested and reviewed by a person.
