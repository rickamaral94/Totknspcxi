# Amaral.12 — captura de 10 minutos

Base: 6d8c3d0fc6e53129b682e1cfa2cd930392b700df (.11). Esta entrega amplia o diagnóstico, sem mudar o algoritmo de framegen, o driver, os caches ou a sincronização gráfica.

## Como usar

Instalar a APK como atualização, abrir Configurações → Arquivos e a seção de logs/desempenho. Pressionar **Medir desempenho (10 min)** e iniciar/retomar o jogo. A captura começa depois de 5 segundos consecutivos fora dos menus. A partir daí são 600 segundos de tempo decorrido; pausas e menus são classificados separadamente, não prolongam o prazo. O contador no canto indica o tempo restante. Ao terminar, o resumo entra no log e no relatório perf-diag indicado pelo aplicativo. Exportar os logs antes de reiniciar repetidamente o app.

Para o teste atual, manter framegen 2x e as mesmas configurações da .11. Percorrer uma área semelhante, com movimento de câmera, fogo e fumaça, e manter o aplicativo em primeiro plano até concluir. A opção de 60 segundos e as comparações de 120 segundos continuam disponíveis.

## Cobertura e custo

O limite anterior de 16384 apresentações cobria somente cerca de 273 segundos a 60 imagens/s: mudar apenas a duração perderia a parte final. Agora são 131072 posições para intervalos, tempos de CPU/GPU e apresentações efetivas na tela. Isso cobre 10 minutos até 120 apresentações/s com margem. São aproximadamente 7,2 MiB adicionais de arrays estáticos; não há crescimento ilimitado. O amostrador de funções mantém seu limite existente e informa amostras perdidas caso o atinja. Não afirmar cobertura ilimitada de amostras individuais.

Uma linha `[diag-minute]` a cada aproximadamente 60 segundos registra FPS reais da janela recente, temperatura SoC, estado térmico e apresentações armazenadas/capacidade. O resumo final conserva p50/p95/p99 e demais métricas. O FPS dessa linha usa o contador de quadros reais; sem amostras válidas informa zero. O décimo checkpoint pode não coincidir com o fim exato, pois a coleta de sensores ocorre a cada dois segundos; o relatório final é a referência para os 600 segundos.

As quatro capturas detalhadas de passes GPU são distribuídas pela janela longa (aproximadamente 10, 203, 396 e 589 segundos de jogo disponível), em vez de concentradas no primeiro minuto. Mantém-se o limite de quatro e a exclusão dos frames afetados por essas capturas. O amostrador continua alternando 5 s ligado / 5 s desligado para avaliar seu próprio custo.

O arquivo de log atual é escrito de forma incremental, sem rotação periódica por tamanho nesse caminho; a rotação para previous ocorre no próximo início. Não há coleta de vídeo ou aumento da frequência dos logs gráficos de dois segundos. A exportação depende do espaço disponível e da pasta escolhida, como antes.

## Validação

Teste host do diagnóstico existente, acrescido de 600 segundos simulados a 120 apresentações/s e 30 quadros reais/s: verifica mais de 71000 apresentações e intervalos de tela retidos, cerca de 18000 quadros reais, cobertura temporal superior a 599 segundos e conclusão da sessão. Isso verifica que o antigo limite não trunca o fim. Também passam os testes anteriores de percentis, accounting, sampler, relatório, A/B, passes GPU e framegen.

Build Android arm64 Release NDK r30; pacote 0.2.1-beta-amaral.12, versionCode 15, assinatura fixa anterior e GAMELIB_ID 9588e7371082fde1 preservados. Compilação nativa e reempacotamento dos recursos/Java anteriores, sem build Gradle completo. Validados manifesto, CRC ZIP, assinatura e preservação dos outros membros do APK. A execução de 10 minutos no Odin ainda precisa ser realizada pelo usuário.
