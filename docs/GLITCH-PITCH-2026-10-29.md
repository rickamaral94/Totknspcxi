# Faixas verticais da vegetação — 2026-10-29

## Resultado

Corrigida a leitura das linhas de render targets lineares no upload Vulkan. As grandes faixas verticais desapareceram na cena estática testada na saída para a Grande Ilha Celestial, mantendo o desenho da vegetação. APK corrigido instalado no POCO F7 Ultra (`192.168.1.6:40089`, Adreno 830 / Turnip).

Ainda há pequenos fragmentos residuais no céu e a distribuição da grama sobre superfícies precisa de investigação. Outras regiões não foram validadas. Este resultado não significa que todos os glitches gráficos estejam resolvidos.

## Diagnóstico

- Desabilitar o atraso de sincronização (`LARECOMP_SYNC_LAG=0`) não removeu as faixas.
- Forçar hashes completos de textura também não removeu as faixas.
- O rastreador de pixels, lendo o attachment 1 do G-buffer, localizou faixas no shader FS `8e5e0a67825c2eaa` / VS `248900100`. O VS consulta mapas de alturas e normais para posicionar a vegetação.
- O teste de omitir esse shader removeu a maioria das faixas, mas também pode omitir vegetação legítima: foi usado apenas no diagnóstico e não faz parte da correção.
- Esse VS amostra um mapa R16_UNORM de 260×260, `flags 0x40`. O upload tratava as linhas como compactas: 260 × 2 = 520 bytes. O jogo usa o alinhamento informado pela propriedade 57 do dispositivo (128 bytes): pitch = 640 bytes.
- O writeback já usava `nvn_linear_rt_pitch`; o upload agora usa a mesma função. Texturas com `flags 0x10` continuam usando o stride explícito. Linhas de 256 pixels R16 continuam com pitch de 512 bytes.

## Validação

- Build Gradle `:app:assembleDebug --offline --console=plain`: passou.
- `tools/test_linear_rt_upload.py`: 64 casos passaram no Android. Extrai a função de pitch e o laço de cópia reais do runtime; compara todos os texels de mapas com padding preenchido com bytes sentinela. Inclui a largura 260, larguras alinhadas, quatro alinhamentos e o caminho com stride explícito.
- Comparação de imagens com a leitura antiga e corrigida na mesma posição, usando cópias isoladas do mesmo save. Nenhum shader é omitido nas medições finais; sem full hash, dumping, alteração de sincronização ou VAMM experimental.
- Movimento e rotação da câmera testados brevemente no save isolado após a medição, sem crash; captura em `movement.png`. Ao terminar, aplicativo reaberto sem extras de teste.
- Evidências locais em `build/glitch-tests-20261029/`: `fps-before.png`, `fps-after.png`, `fps-before.log`, `fps-after.log`; diagnóstico adicional em `geometry.log`, `suspect.glsl`, `sync0.png`, `fullhash.png` e `pitch-steady.png`.
- O arquivo temporário `files/build/pick.txt`, criado pelo diagnóstico no celular, foi removido. Os saves originais não foram usados para gravação nos testes.

## Desempenho

A comparação usa médias dos registros do profiler entre 55 e 81,3 segundos após iniciar cada processo, descartando a carga inicial. Mesma cena, save original de referência e parâmetros nos dois APKs; apenas a leitura das linhas difere. São execuções consecutivas, sujeitas a clocks, temperatura e atividade de fundo. Um ganho consistente exige novas rodadas.

| Leitura | Registros | FPS médio | CPU de execução / quadro |
| --- | ---: | ---: | ---: |
| Antiga, linhas compactas | 13 | 22,64 | 31,11 ms |
| Corrigida, pitch alinhado | 13 | 19,66 | 31,73 ms |

Não houve ganho de FPS: a rodada corrigida ficou 13,2% abaixo da anterior. Não é possível atribuir toda a diferença ao pitch com apenas uma execução de cada versão; a vegetação também passa a ser desenhada em posições diferentes. A mudança corrige a leitura das alturas, mas não deve ser anunciada como otimização de desempenho. Repetir a comparação e investigar o custo da vegetação continua pendente.
