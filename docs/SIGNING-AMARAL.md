# Assinatura fixa TOTKDroid Amaral

## Chave atual (desde a amaral.24, 14/11/2026)

O Rick passou o projeto a um único agente (`claude`) e pediu uma chave nova, guardada por ele. A partir da
**0.2.2-beta-amaral.24 (versionCode 27)** todos os APKs usam esta chave:

- Pacote: `com.totkrecompandroid`
- Alias: `totkrecomp-amaral`
- RSA 4096, PKCS12, validade até 2126
- Certificado SHA-256: `0969fc0776fa8f3fe2f5361a8caa3c415bf7bfdfbc6a7cdbcd2e23c24a6079bf`
- Backup: o arquivo `.p12` e o `keystore.properties` (senha) entregues ao Rick fora do Git. **Nunca commitar nem
  publicar.** Para compilar, restaurar em `~/.totkrecompandroid/` (o `storeFile` do `keystore.properties` aponta
  para o `.p12`).

**Migração:** um APK com a chave nova não instala por cima de um assinado com a chave anterior (Android recusa
certificado diferente). Na primeira instalação da .24: exportar os saves (tela de início), desinstalar, instalar a
.24 e importar os saves. Os arquivos do jogo podem precisar ser configurados de novo. Depois disso, as atualizações
seguintes instalam por cima.

## Chave anterior (amaral.5 a amaral.22)

A partir da versão 0.2.0-beta-amaral.5 (versionCode 8), usar sempre a chave RSA 4096 criada em 2026-11-02. Nunca gerar uma chave temporária para uma atualização.

- Pacote: `com.totkrecompandroid`
- Alias: `totkdroid-amaral`
- Certificado SHA-256: `dfd0653e2a1df81b42a0381e39f4251ac82001a0f8bd8bfa7070157f4c36f1e3`
- Backup privado persistente: `TOTKDroid-Amaral-Assinatura-Fixa-BACKUP.zip`, Library ID `libfile_070893baee6881918043c7c82b9fd065`.
- O backup contém a chave PKCS12, certificado e credenciais em signing.json. Não publicar nem commitar o backup ou suas credenciais.
- Para Gradle, restaurar o backup fora do repositório e configurar ~/.totkrecompandroid/keystore.properties com storeFile, storePassword, keyAlias e keyPassword.
- O build release não deve recorrer silenciosamente a uma debug key se a chave fixa estiver ausente.
- Antes de publicar, verificar a assinatura do APK e comparar o SHA-256 do certificado com o registrado acima.

A migração dos APKs anteriores exige reinstalação por terem certificados diferentes. Preservar arquivos e saves antes disso. As versões posteriores devem manter o mesmo pacote, chave e versionCode crescente.
