# Amaral.14 — corrigir mesclagem de buffers residentes durante passes

Base: bb5c27fd90b4ef2bfbaaa8f4fa70d11188571308 (.13).

## Evidência e decisão

O usuário confirmou piscadas fora da primeira caverna e forneceu teste com FG desligado. O log confirma framegen=0, LRZ off, Turnip Amaral v5.8.17.1, 28,75 FPS reais. Portanto a interpolação não é condição necessária para reproduzir o defeito. O log anterior registrava 130542 recusas acumuladas de mesclagem dentro de passes, ring chegando a 6382,71 MB lógicos em um intervalo e apenas 9,84 MB residentes de 48 MB. Não é evidência de arena cheia.

Revisados commits atuais de mamcjr/TOTKDroid (58754913ed8456f11810b52a95e85a30b9504e36, com integração Mali 309f1e0e80f7d2be8dd70e6078fb0a6c79a76656), issue #7 e alterações recentes de exec.c em mamcjr/BOTWRecompAndroid. O upstream TOTKDroid ainda contém `if (no && in_pass) return NULL;`. Não há nesses commits uma correção pronta desse caminho. O trabalho da comunidade que manteve dados globais residentes na GPU continua sendo a base; esta entrega corrige uma lacuna na sua utilização por draws.

## Defeito identificado

A arena preserva resultados escritos pela GPU entre compute e desenhos. Se um range precisa crescer ou unir entradas, gres_get copia os slots anteriores para o novo, na ordem da GPU. Mas gcopy_prepare era executado depois de iniciar o passe e registrar estado/pipeline. A recusa de mesclar dentro do passe devolvia false; o draw caía nas cópias transitórias da memória guest. Essa RAM pode ainda não conter os resultados de compute. O caminho de fallback também não devolve escritas transitórias quando a arena está disponível. Logo, substituir uma entrada residente por RAM neste caso pode quebrar a continuidade dos dados. Isso é um defeito concreto; ainda não comprova que explica todo artefato de toda cena.

## Correção

- Permitir a mesclagem encerrando o passe anterior antes de gravar barriers/cópias GPU→GPU. Reaproveitar o código upstream que copia resultados residentes, sem trocar esses resultados por uma leitura da RAM.
- Preparar gcopy/gwin no draw antes de obter seu command buffer e registrar pipeline/estado dinâmico. Quando a mesclagem encerra o passe, restaurá-lo antes de gravar o desenho.
- Manter a submissão normal de início de passe ANTES de preparar os bindings da arena. Na restauração após a mesclagem, adiar a submissão automática de chunk/debug: os writebacks já preparados precisam continuar associados à mesma submissão que gravará o draw. Não apenas mover gcopy_prepare acima de begin_pass: esse movimento isolado permitiria que begin_pass submetesse os bindings antes do desenho.
- Ranges já cobertos não encerram passes. Alocação de uma entrada sem sobreposição continua sem essa cópia/mesclagem. Não inserir wait de CPU/GPU por draw.
- Renomear o contador para merge_pass_breaks: agora são encerramentos para efetuar mesclagens, não recusas.

Mantidos tamanho da arena, política de conflitos de CPU, writeback da .9, caches, LRZ, driver, barreiras de memória e a captura de 10 minutos. A correção não elimina todos os fallbacks possíveis, como falhas de alocação. A .13 de pacing permanece; o novo teste deve inicialmente manter FG desligado para isolar a correção gráfica.

## Validação

Teste extrai gres_get real e simula a memória guest, a arena e comandos Vulkan. Em 100 casos, escreve um valor somente na GPU, expande o range dentro de um passe e verifica que a nova entrada conserva esse valor; a RAM e o shadow ainda contêm a versão antiga. As cópias/barreiras simuladas exigem que o passe esteja encerrado. Verifica união de dois residentes, propagação de stored e ausência de encerramento para range coberto. Confere também a ordem de preparação/command buffer no draw e a proteção da submissão de chunk. Executado com ASan/UBSan, LeakSanitizer desligado pela restrição de ptrace do ambiente. O teste não executa Vulkan real.

Passaram também 1000 fixtures de ranges upstream, 5000 ciclos de invalidação do guest cache e 10000 mutações do cache de leitura da arena. Build Android arm64 Release NDK r30; APK 0.2.1-beta-amaral.14 versionCode 17; assinatura fixa e GAMELIB_ID 9588e7371082fde1 preservados. Compilação nativa e reempacotamento, sem build Gradle completo. Validação de manifesto, assinatura, CRC e membros preservados do APK.

## Teste no Odin

Instalar por cima e testar imediatamente o mesmo trecho que pisca, com FG desligado. Não é necessário novo diagnóstico de 10 minutos para a primeira avaliação visual. Se estabilizar, ligar FG2x e repetir o movimento de câmera. Enviar um log curto/vídeo apenas se persistir ou para comparar o resultado. A compilação e os testes host verificam a implementação; a eliminação da piscada no aparelho ainda não foi confirmada.
