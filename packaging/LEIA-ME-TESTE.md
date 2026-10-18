# Directory Open — candidato local de teste

Este pacote foi preparado para verificacao. **Nao publicar ou redistribuir.**
A revisao dos avisos/licencas e o teste em outro Windows ainda estao pendentes;
consulte package-manifest.json e docs/AVISOS-TERCEIROS.md.

Extraia toda a pasta antes de abrir DirOpen.exe. Nao execute de dentro do ZIP.
Mantenha as DLLs, plugins e qt.conf ao lado do aplicativo. Nenhum instalador e
executado; o pacote nao instala Qt, Visual Studio ou o runtime da Microsoft.

Requisitos: Windows x64 compativel com a versao Qt registrada no manifesto e
Microsoft Visual C++ Redistributable x64 compativel com o compilador usado.
Obtenha o runtime somente pelo site oficial Microsoft:
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

Use somente dados de demonstracao na validacao. Para evitar a sessao pessoal:

~~~powershell
& .\DirOpen.exe --no-session --left 'C:\Teste\Esquerda' --right 'C:\Teste\Direita'
~~~

--no-session desativa historico/desfazer persistente. Para verificar esse recurso,
use --session-file com um caminho separado e preservado durante o teste.
O uso normal guarda a sessao em %LOCALAPPDATA%\Navega por compatibilidade.
Feche o aplicativo antes de mover/excluir a pasta do candidato; as sessoes ficam
fora dela. O pacote nao fornece desinstalador nem apaga essas sessoes.

O manual e os atalhos estao em docs/USO.md. A licenca MIT do codigo proprio esta
em LICENSE.txt. Ela nao substitui as licencas das dependencias.
