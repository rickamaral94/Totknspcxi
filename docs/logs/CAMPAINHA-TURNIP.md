# Campainha do agente Turnip (Amaral Adreno Tools)

Pedido do Rick em 14/11/2026. Os agentes do Totknspcxi (Claude e Codex) podem acionar o agente do driver **Amaral
Adreno Tools** (Turnip/Mesa para Adreno, `rickamaral94/Amaral-Adreno-Tools`) quando precisarem de ajuste ou de
análise do lado do driver.

Isto é o canal para o item [Encaminhamento ao Amaral Adreno Tools](TURNIP.md#encaminhamento-ao-amaral-adreno-tools)
de [Turnip no TOTK](TURNIP.md): aquele tópico define quando há evidência suficiente; esta página diz como chamar.

A coordenação interna entre Claude e Codex continua no `collab/channel` deste repositório. A campainha é só para o
agente do driver.

## Quando acionar

Acione quando houver uma das condições de [Turnip no TOTK](TURNIP.md#encaminhamento-ao-amaral-adreno-tools):

- diferença reproduzível com o mesmo APK e mudança só do ELF do driver;
- uma opção do Turnip que isole um caminho, com captura que indique o mecanismo.

Também vale acionar para:

- **Dúvida de driver.** Por exemplo, se `TU_DEBUG=...`, `IR3_SHADER_DEBUG=...` ou um driconf ajuda; ou se um recurso
  Vulkan é confiável no Turnip: heap colocado (`VK_EXT_map_memory_placed`), leitura do próprio alvo, LRZ, GMEM/SYSMEM,
  feedback loop.
- **Pedido de build.** Um Turnip de laboratório com uma única alteração para A/B/A.

Não acione para problemas que também aparecem no driver da Qualcomm ou que acompanham um caminho do port (cópias,
heap, in-place) com o mesmo ELF. Esses ficam no port, como o próprio tópico Turnip já orienta.

## Como acionar

Comente no **PR de campainha** deste repositório (`turnip: campainha do agente Turnip (NÃO MESCLAR)`). O agente
Turnip está inscrito nele e é acordado por cada comentário novo.

Comece o comentário com `[totk]` e traga:

1. **Sintoma**, em uma frase, com o rótulo: medição no aparelho, relato, verificação no host ou hipótese.
2. A [ficha de coleta](TURNIP.md#ficha-de-coleta) de cada braço. Pelo menos:
   - `amaral.N`, commit e SHA-256 do APK;
   - TOTK 1.4.2 e checkpoint;
   - opções efetivas de `gpu_heap`, `inplace_reads` e LRZ, e as variáveis do driver;
   - ZIP e ELF com SHA-256, Mesa e patchset;
   - clock da GPU ponderado por residência, quando houver.
3. **Resultado A/B/A:** FPS reais, p95 e p99, GPU por quadro, CPU de render, cópias e defeitos visuais ou
   `device lost`.
4. **Onde está o registro:** caminho e commit em `docs/logs/` ou no relatório `docs/PERFORMANCE-AMARAL*.md`. Os logs
   brutos ficam com o Rick.
5. **O que você espera:** diagnóstico, variável para testar, driver de laboratório ou correção.

## O que o agente Turnip devolve

- Resposta no próprio PR de campainha, com a análise no código do Turnip/Mesa citando arquivo e commit.
- Quando der sem build, uma variável ou driconf para testar. `TU_DEBUG=flushall`, `syncdraw` e `nolrz` servem para
  diagnóstico, não para deixar ligados.
- Quando precisar de driver novo, um **ZIP de laboratório** no formato adrenotools, com SHA-256 e com o que muda em
  relação à versão publicada. Ele não substitui a release até o reteste no Odin.
- A correção definitiva vai para o Amaral Adreno Tools e, quando couber, para o Mesa upstream. A candidata devolvida
  e o reteste são anotados em [Turnip no TOTK](TURNIP.md).

O agente Turnip não altera código do Totknspcxi sem pedido. Merge, APK e Latest continuam sendo decisão do Rick.

## Contexto que o agente Turnip já levantou

Levantado entre 06 e 07/11/2026. Detalhes em
[`docs/research/2026-11-survey.md`](https://github.com/rickamaral94/Amaral-Adreno-Tools/blob/ccr-05e1d523-htngw2/docs/research/2026-11-survey.md)
§2 e na auditoria TOTK A740 (PR #41 do Amaral Adreno Tools).

- **Driver publicado.** Amaral `mesa-27.0.0-devel-v5.8.17.1` (Latest), Mesa `fd8ef2b`. ELF Standard:
  `e64f5306c5c8f52f013f01f3d2627a81704f07ba71ab9518c27be2b209863a96`.
- **Os três caminhos no Automatic.** Corrigido em 14/11 pelo agente do TOTK e conferido no código. Desde a
  `.20` (`fec6ad9`), o Automatic de `gpu_heap`, `inplace_reads` e LRZ liga em Adreno 8xx **e no 740** (`vk_lrz_auto`
  e `vk_turnip_path_on`, `runtime/nvn/vulkan/device.c`).
  - O upstream TOTKDroid restringe heap e in-place ao 8xx; o fork não.
  - O `TU_DEBUG=nolrz` da coleta da `.19` veio de `turnip_lrz=0` forçado nas configurações, não do Automatic.
  - O ganho de 29,2 para 41,4 FPS reais da `.19` foi medido com as três opções ligadas juntas. Isolar cada opção,
    como a matriz já pede, diz quanto é de cada uma.
- **Leitura do próprio alvo sem a extensão.** O port não usa `VK_EXT_attachment_feedback_loop_layout`. O Turnip a
  expõe e, com ela, tira o passe de GMEM. Se `inplace_reads=on` mostrar listras ou piscadas que somem com
  `TU_DEBUG=sysmem`, a causa provável é a leitura em GMEM. A correção portável é o port declarar o feedback loop com
  a extensão, não um patch de driver.
- **Timestamps de diagnóstico.** Um timestamp dentro de um passe GMEM, em estágio diferente de TOP_OF_PIPE, custa um
  `WFI` por bin. Meça com a instrumentação mínima antes de usar tempos por passe como baseline (Gate 0 da auditoria).
