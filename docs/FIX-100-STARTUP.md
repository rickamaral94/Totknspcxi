# Inicialização do TOTK 1.0.0 — amaral.2

O log do Odin 2 Portal da amaral.1 mostra main em `0x100000000`, com
`0x4668000` bytes, e sdk em `0x104000000`, com `0xddc000` bytes.
Esses intervalos se sobrepõem. A chamada abortada em `nninitStartup+18`
lê o destino em `x16=0x10453f3b0`, dentro dessa sobreposição, e obtém
`x17=0xeb1402b89a828115`, que não é um endereço de função válido.

O SDK agora começa no deslocamento de 128 MiB (`0x8000000`), tanto no
runtime como no gerador embarcado e no script de compilação no computador.
O carregador rejeita módulos que ultrapassem a próxima região antes de
copiar dados; o cálculo do tamanho usa 64 bits para evitar overflow.
Ferramentas de símbolos e perfis acompanham o endereço novo.
A identidade do código recompilado inclui o layout e o construtor, para
não aceitar bibliotecas antigas com endereços incompatíveis.

## Verificação

- `python tools/test_module_layout.py`: usa o caminho real de cópia e validação
  do carregador com NSOs sintéticos. Reproduz o tamanho do main e a posição
  do slot da chamada do log, verifica que carregar sdk preserva o slot,
  e rejeita sobreposição e overflow antes de qualquer escrita.
- `python tools/test_native_mode.py`: passou.
- Testes de resolução dinâmica com Clang do NDK: passaram.
- `libmain.so` ARM64 compilada com CMake 3.30.4 e NDK 30.0.16248370;
  headers SPIRV-Tools do commit fixado pelo shaderc, mantendo a validação
  de shaders do gerador de quadros.

## APK

`0.2.0-beta-amaral.2`, versionCode 5, mesmo applicationId.
A biblioteca corrigida foi compilada e reempacotada no APK amaral.1,
reutilizando seus executáveis, bibliotecas dependentes, DEX, recursos e
assets do compilador. Só libmain e os dois campos de versão do manifesto
foram alterados; a assinatura anterior foi substituída por uma nova assinatura
de debug, e o APK foi alinhado e verificado. Não foi um novo assembleDebug
completo: o Java/Gradle deste ambiente não conseguiu acessar a rede.

A chave de debug anterior não está disponível nesta sessão. A assinatura
nova pode impedir instalar por cima da amaral.1; não desinstale sem preservar
os dados desejados. Este APK ainda precisa de teste no aparelho.

Após instalar, reconstruir o código do jogo em Arquivos do jogo; o código
anterior é incompatível. Não é necessário obter outra cópia do jogo para
corrigir a sobreposição. Outros problemas do suporte experimental à 1.0.0
podem aparecer depois que esta inicialização avançar.
