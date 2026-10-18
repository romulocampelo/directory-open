# Verificar, reparar ou reinstalar o Qt PDF

O DirOpen usa Qt PDF para leitura, pesquisa, seleção e miniaturas de PDFs.
Exportar relatórios para PDF usa Qt PrintSupport. Reinstalar Adobe Reader ou
alterar a associação de arquivos do Windows não reinstala essas bibliotecas.

## Diagnóstico antes de remover

Os testes locais da 0.24 verificaram PDFs reais. A investigação de distribuição
encontrou lacunas nos documentos de composição, fontes e avisos do componente;
isso não demonstrou defeito de renderização nem corrupção da instalação.
O arquivo original do Qt PDF 6.12.0 passou na integridade do 7-Zip e seu SHA-1
coincidiu com o registro de download. Em 18/10/2026, os 181 arquivos instalados
registrados no mapa de instalação foram reconferidos: todos coincidiram com seus
hashes de origem, sem ausências ou divergências. Isso não substitui os testes
funcionais nem atesta os documentos do produtor. Reinstalar os mesmos bytes não acrescenta
documentos ausentes do arquivo de origem.

Não misture DLLs de versões ou kits distintos. Uma troca exige conferir também
headers, bibliotecas de ligação, plugins, metadados CMake, Qt Base e Multimedia.
Não copie DLLs avulsas de sites de terceiros para a pasta do programa.

## Instalação administrada pelo Qt Maintenance Tool

1. Preserve o pacote/build atual e anote versão, arquitetura e compilador do kit.
   Feche somente os aplicativos que você estiver usando com esse kit antes de
   alterá-lo. Não remova builds antigos ou suas sessões.
2. Abra o **Qt Maintenance Tool** da instalação usada na compilação. Selecione
   **Add or remove components** e localize o componente PDF correspondente ao
   kit MSVC x64. Examine a lista de alterações antes de aplicar.
3. Se o componente estiver instalado e disponível para nova instalação, desmarque
   somente esse componente e aplique a remoção. Abra novamente a seleção,
   marque a mesma versão compatível e aplique a instalação. Não selecione
   atualização geral, outro compilador ou remoção completa do Qt.
4. Se o componente não estiver listado ou já não estiver disponível no repositório,
   interrompa essa remoção. Uma instalação manual pode não ser administrada pelo
   Maintenance Tool. Preserve o kit e prepare um **kit coerente em outra pasta**,
   com Base, Multimedia e PDF compatíveis; só troque o build depois da validação.
5. Na série 6.12, confira a distribuição separada **Qt WebEngine**, que passou a
   incluir os módulos PDF. Não procure apenas um componente chamado PDF 6.12.0:
   o anúncio do Qt informa que os binários finais desse módulo usam o versionamento
   separado. Confira a versão mínima do Qt exigida pelo pacote realmente oferecido.

O Qt Maintenance Tool oferece adicionar, atualizar e remover componentes:
[documentação oficial](https://doc.qt.io/qt-6/add-update-remove-components.html).
A mudança de distribuição está no
[anúncio oficial de 24/09/2026](https://www.qt.io/blog/qt-webengine-will-release-separate).
Essas páginas não atestam a composição do pacote local antigo.

## Recompilar e conferir a troca

Use uma pasta de build nova. No PowerShell, na raiz dos fontes:

~~~powershell
# Informe o kit completo que foi instalado e conferido:
$env:NAVEGA_QT_DIR = 'C:\Qt\KIT-ESCOLHIDO\msvc2022_64'
& .\build.cmd --build-dir build-pdf-reinstalado
if ($LASTEXITCODE -ne 0) { throw 'Build ou testes falharam.' }
& .\build-pdf-reinstalado\DirOpen.exe --no-session
~~~

O caminho acima é um exemplo a ajustar, não uma versão recomendada ou instalada.
O build não instala ferramentas. Verifique abertura, páginas, zoom, pesquisa,
seleção/cópia de texto, miniaturas e erro em PDF inválido com arquivos de teste.
Confira também mídia e relatórios, pois compartilham dependências Qt. Não use
preferências/sessões pessoais nos testes automatizados nem imprima fisicamente.

Depois de validar, atualize `IniciarDirOpen.cmd` para o novo build. Para retornar,
aponte o inicializador para o build anterior preservado. A troca do kit não
altera o conteúdo de um candidato antigo: gere outro pacote, com novos hashes,
inventário, fontes/avisos aplicáveis e teste em Windows independente antes de
disponibilizar executáveis. Veja [LANCAMENTOS.md](LANCAMENTOS.md).
