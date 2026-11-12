# Turnip no TOTK

Registro de desempenho e correção gráfica para o Totknspcxi e o
[Amaral Adreno Tools](https://github.com/rickamaral94/Amaral-Adreno-Tools).
Ao preparar uma atualização do Turnip, usar o mesmo APK e os checkpoints
deste registro para repetir os casos ainda abertos e verificar regressões.

## Evidência atual

A comparação histórica foi feita na **amaral.19**, TOTK 1.4.2, Odin 2 Portal
(Adreno 740), Turnip Amaral 27.0.0-devel v5.8.17.1, alvo 60 FPS e escala 1×.
Duas sessões de 60 s variaram `gpu_heap`, `inplace_reads` e LRZ juntas.
A faixa térmica mudou de 45–60 °C para 53–74 °C.

| Métrica | Três opções desligadas | Três opções ligadas |
| --- | ---: | ---: |
| FPS reais | 29,2 | 41,4 |
| Frametime p95 | 56,8 ms | 44,0 ms |
| GPU por frame | 25,1 ms | 20,8 ms |
| CPU da thread de render | 21,9 ms | 16,0 ms |

O resultado dá prioridade a isolar as três opções. Heap e leituras in-place
são caminhos do port, e o teste também alterou LRZ no driver. A redução
de cópias de feedback e a diferença térmica impedem atribuir o ganho ao
Turnip. O valor de GPU de 20,8 ms ainda supera o orçamento de 16,67 ms
por quadro a 60 FPS. O relatório também anotou espera por lock no core 0
e amostras em libc sem símbolo. A origem dessa espera continua em aberto.
[Relatório da .20](../PERFORMANCE-AMARAL20.md)

A revisão v5.8.17.1 é a referência histórica. O lock atual do projeto de
driver registra Mesa `fd8ef2befafd01190e33f12dbfd109b924c8ebc3`. O ZIP
e o ELF do ensaio histórico precisam ter seus hashes conferidos antes de
serem tratados como um binário reproduzível.
[Lock do Mesa](https://github.com/rickamaral94/Amaral-Adreno-Tools/blob/main/config/mesa-lock.json)

A [amaral.22](https://github.com/rickamaral94/Totknspcxi/blob/c3cca75e956eceb984adc5619d0e6663bde72961/docs/PERFORMANCE-AMARAL22.md) coleta amostras instantâneas do clock KGSL a cada
2 s durante o diagnóstico, quando o Android permite a leitura. Usar a
contagem e a dispersão como triagem. Para o controle por residência,
guardar os contadores de residência separadamente.

## Matriz de testes

| Caso | A/B/A no mesmo APK e save | Evidência que orienta o próximo passo | Estado |
| --- | --- | --- | --- |
| **LRZ e piscadas** | LRZ ligado → desligado → ligado, com heap/in-place fixos. Reiniciar o processo em cada braço e conferir o estado efetivo. Percorrer saída da caverna e superfície. | Vídeo ou imagens do mesmo trecho e recorrência nos dois A. Se depender de LRZ, capturar o passe de profundidade e reduzir o caso antes de propor ajuste no Mesa. | Pendente |
| **Heap e feedback** | Dois ensaios, um para `gpu_heap` e outro para `inplace_reads`. Manter LRZ fixo. Usar combinações que realmente ativem o caminho indicado pelo log. | Upload, cópias de feedback, GPU/frame, CPU de render e imagem. Se o custo acompanha as cópias com o mesmo ELF, investigar primeiro o caminho do port. | Pendente |
| **Regressão entre builds do driver** | Turnip Amaral de referência → candidata → referência. Configurações explícitas e iguais. Cada braço usa seu cache compatível, aquecido com o mesmo percurso. | Se a diferença se repetir, comparar Mesa, patchset e caminho Vulkan efetivo. Usar builds com uma única alteração para localizar o mecanismo. Driver Qualcomm pode servir de controle adicional de compatibilidade. | Pendente de linha de base e candidata |
| **Espera de fila ou KGSL** | Medir a configuração de referência com cache quente. Se houver sinal, repetir com a candidata e a mesma coleta. | Separar tempo de submit na CPU, espera em fence, GPU ocupada e espera de compilação. Simbolizar amostras com ELF do mesmo build. Se houver diferença no caminho de submit/fence ligada ao ELF, reduzir e encaminhar. | Hipótese |
| **GMEM/SYSMEM e stores/resolves** | Se a captura apontar custo ou corrupção em attachments, comparar automático com GMEM forçado e com SYSMEM forçado em ensaios separados, quando suportado pela revisão. | Decisão por passe, GPU/frame, stores/resolves e comparação visual. As opções `TU_DEBUG` servem ao diagnóstico local. [Opções do Mesa](https://docs.mesa3d.org/envvars.html#turnip-driver-environment-variables) | Condicional à captura |

## Ficha de coleta

- Aparelho/GPU/Android, modo de energia, ventoinha, temperatura inicial e
  máxima, clocks de CPU e GPU e residência da GPU quando disponível.
- Commit, SHA-256 do APK, versão do jogo, modo de execução, save/checkpoint,
  trajeto, duração, framegen, escala interna, AA, upscaler e limite de FPS.
- Para cada braço, nome/variante do ZIP, versão, SHA Mesa, hash do patchset,
  SHA-256 do ZIP/ELF e build ID se disponível. Confirmar o driver carregado
  pelo log e guardar as opções efetivas, inclusive `TU_DEBUG` e LRZ.
- Repetir o aquecimento e restaurar o save antes de medir. Marcar como
  inconclusiva a comparação cujo clock GPU ponderado por residência difira
  mais de 3% entre os braços, exceto quando o clock for a variável testada.
  Quando não houver residência, marcar esse controle como indisponível.
- Anotar janela do `[diag] summary`, FPS reais, p50/p95/p99, GPU/frame
  com cobertura válida, CPU de render, stutter, compilação, cópias, memória
  e corrupção/device lost. Campo ausente ou timestamp inválido fica
  indisponível. Usar captura detalhada ou validação Vulkan em uma rodada
  separada da medição de desempenho.
- Referenciar log e vídeo sanitizados por nome e janela. Guardar os brutos
  fora do Git.

## Encaminhamento ao Amaral Adreno Tools

Abrir investigação de driver quando houver diferença reproduzível com o
mesmo APK e mudança do ELF, ou quando uma opção do Turnip isolar um caminho
e houver captura que indique o mecanismo. Anexar o resumo acima, o resultado
de A/B/A, os commits exatos e o menor cenário reproduzível disponível.
Registrar hipótese, critério de sucesso, próximo teste e link de volta para
este log. No TOTK, anotar a candidata devolvida e o reteste no Odin.

A/B/A é triagem do port. A qualificação da candidata do driver segue o
[registro em hardware](https://github.com/rickamaral94/Amaral-Adreno-Tools/blob/main/docs/tests/README.md)
e a [política de evidência](https://github.com/rickamaral94/Amaral-Adreno-Tools/blob/main/docs/EVIDENCE-POLICY.md),
que exige cinco rodadas balanceadas com controle térmico para o A/B de promoção.
