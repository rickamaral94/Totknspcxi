# Assinatura fixa TOTKDroid Amaral

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
