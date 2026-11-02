# Amaral.6 — teste de snapshots e custo de writeback

VersionName 0.2.0-beta-amaral.6, versionCode9, pacote com.totkrecompandroid. Mesma chave fixa da amaral.5 (SIGNING-AMARAL.md).

## Evidência

O log da amaral.5 registrou 29,59 FPS reais, p95 39,89ms, framegen desligado, Turnip Amaral27/Adreno740, jogo1.4.2. As 64 janelas de escala não tiveram mudanças nem recriações por escala; isso não exclui DRS interno do jogo. Endereços do perfil simbolizados no ELF entregue:0x1d68c0 e0x1d6900 em gcopy_retire,0x1f0b40 guest_copy,0x1ea3c0 gres_prepare. Memcmp era37,95% das amostras da thread render; amostragem não fornece a distribuição de todos os chamadores de memcmp.

## Alterações

- Writeback: comparação de regiões GPU/shadow de até4096bytes para evitar chamadas memcmp de256bytes quando a região toda permanece igual. Verifica sequências por bloco antes do atalho; em regiões alteradas ou com upload mais novo mantém exatamente a política anterior por256bytes. Não remove verificações de conflito com a CPU. Ganho no Odin ainda não medido.
- Experimento restrito a jogo142 + driverTurnip + nomeGPU contendo740: não reutilizar snapshots de feedback, usando o caminho de cópias já existente. O log identifica o experimento. Pode aumentar custoGPU e reduzir FPS; não é correção validada nem novo padrão universal. Serve para decidir se a reutilização participa das piscadas. As demais GPUs e versões preservam a política anterior.
- Mantidos um worker de pipelines, resolução e opções do usuário. DriverTurnip não modificado.

## Validação

5000 casos determinísticos com comprimentos incluindo0, caudas parciais, GPU alterada, CPU alterada e sequências mais novas; resultados de memória e shadow idênticos à rotina anterior. Testes host com ASan/UBSan passaram com detector de leaks desabilitado por limitação de ptrace do ambiente. Build nativo arm64 Release API30/NDK30. Assinatura deve coincidir com a amaral.5; instalar como atualização. Não testado no Odin.

## Comparação

Manter a configuração da rodada anterior, framegenoff,30FPS,1x, mesmo trecho das fumaças e mesma câmera. Jogar60–90s e enviar log/vídeo. Observar se as piscadas mudam e a variação de FPS; não compensar a perda de FPS mudando outras opções durante a rodada.
