# Dependencias do candidato Windows

O empacotamento atual produz um **candidato local, com requisitos de distribuição pendentes**.
Este arquivo registra o processo de conferencia; nao substitui os avisos/textos de
licenca e fontes correspondentes ainda necessarios para distribuir o pacote real.

## Inventario verificavel

package-manifest.json registra revisao Git, alteracoes locais, fingerprint dos
fontes, compilador, Qt e SHA-256 de cada arquivo. A origem qt/ representa caminhos
relativos ao kit efetivamente usado no build. O hash de origem e o hash implantado
sao separados: windeployqt pode ajustar Qt6Core.dll para caminhos relativos.
O manifesto nao contem o ambiente do usuario. O ZIP tem um SHA-256 adjacente.
Esses hashes detectam alteracoes acidentais; nao sao assinatura digital.

legal/qt-sbom contem os documentos SPDX completos fornecidos pelo Qt para modulos
com pelo menos um arquivo implantado. Sao evidencia do produtor, preservada sem
reinterpretacao: podem listar componentes de desenvolvimento, debug ou modulos que
nao estao no pacote. NOASSERTION/unknown nao significa ausencia de obrigacoes.
Instalacoes sem SBOM permanecem com essa evidencia ausente; nao inventar licencas.

No P0.1a, o inventário técnico local liga cada arquivo do manifesto à origem de
deployment, hash, recurso de versão Windows e eventuais entradas SPDX diretas.
As listas completas do produtor são conservadas separadamente e não provam que
todos os componentes citados foram embarcados. Licença/opção aplicável, textos,
atribuições e fontes correspondentes continuam pendentes para distribuição.
No P0.1b local de18/10/2026, os23 SHA-1 Qt divergentes foram reconciliados:
remover em memória somente a tabela terminal de certificados PE e zerar CheckSum
e o diretório dessa tabela faz todos coincidirem com o SPDX. Os bytes originais
e o SPDX foram preservados. Isso sustenta inventário anterior à assinatura;
não certifica cadeia de confiança ou a história de assinatura do produtor.
SHA-256 continua identificando separadamente origem e payload reais.
As cinco DLLs FFmpeg continuam sem entrada direta de arquivo naquele SPDX.
Foi preparada uma ponte derivada por hashes, imports PE do plugin, configuração
embutida idêntica e receita Qt fixada: FFmpeg9.0.1 e zlib1.3.1 têm fontes cujo
SHA-1 coincide com o script do produtor. Isso não é prova de build reproduzível
nem atestação de ausência de patches.

A opção de trabalho é LGPL-3.0-only para os23 Qt e LGPL-2.1-or-later exercida
em2.1 para FFmpeg, preservando as licenças cumulativas dos demais terceiros.
Textos FSF integrais, fontes/receitas e rascunho de avisos foram preparados fora
do candidato. O wrapper Qt PDF e o submódulo qtwebengine-chromium foram obtidos
nas revisões exatas. O arquivo comprimido mais cinco arquivos de controle separados
reconstituem a árvore Git completa do submódulo. Isso inclui as alterações rastreadas
da revisão, mas não comprova configuração gerada, alterações locais ou build do produtor.
Ainda faltam o inventário PDF específico com checksum anunciado, seu fechamento
efetivo de terceiros/avisos e os documentos externos antigos de Qt Base/Declarative.
As URLs anunciadas retornaram404. Os documentos atuais do kit não coincidem nos
checksums; não substituir por nome/versão. As arestas Declarative identificadas
pertencem a módulos não entregues, segundo o grafo declarado do produtor.
Na continuidade seguinte, o arquivo original do Qt PDF foi conferido: contém
quatro SBOMs qtpdf e nenhum inventário Chromium-PDF. A fonte exata instala este
último separadamente, enquanto a regra de separação do pacote seleciona
`**/sbom/qtpdf*`. Isso sustenta hipótese de omissão no empacotamento, ainda sem
logs/atestação do produtor. URI de namespace SPDX não precisa ser acessível;
o404 não demonstra indisponibilidade global. Doze fichas README.pdfium apontam
a13 textos:8 existem nos caminhos literais da árvore exata e5 não. Essa leitura
não resolve o grafo GN efetivo nem autoriza substituir textos por aproximação.
Relatório separado em `continuation-producer/VALIDACAO.md`; históricos preservados.
Os seis metadados com falha JSON estrita ganharam leitura suplementar explícita,
preservando controles, Unicode e originais. A alternativa de trabalho para Vulkan
XML é MIT; diferenças de copyright entre SPDX e texto específico foram mantidas.
WrapAtomic permanece NOASSERTION, sem uma licença inventada para esse wrapper.

Uma modificação compatível de Qt SVG foi recompilada com ferramentas já instaladas
e ensaiada com plugins originais/recompilados em cópias novas. Rasterização e a
entrada/UI de produção em executável isolado de teste passaram. O candidato e seu
DirOpen.exe permaneceram intactos. Esse escopo não demonstra substituição de
Qt Base, Multimedia, PDF ou FFmpeg, nem o build reproduzível do produtor.
Ligação dinâmica e SPDX não concedem liberação automática.

Registro local completo: `.cache/sprints/2026-10-18-p01b/VALIDACAO.md` e instruções
adjacentes; continuação em `continuation-pdf/VALIDACAO.md` e `RECOMPILACAO-SVG.md`.
O rascunho não foi incorporado ao candidato P0.1a, que permanece imutável
e publishable=false. Este documento de trabalho também não o torna publicável.
O roteiro de ambiente independente está em
[VALIDACAO-WINDOWS-INDEPENDENTE.md](VALIDACAO-WINDOWS-INDEPENDENTE.md); não foi executado.

## Conferencia antes de oferecer download

TeraCopy, Everything e 7-Zip são ferramentas externas instaladas pelo usuário. Os
adaptadores do DirOpen usam interfaces públicas; não incluem seus executáveis,
bibliotecas, chaves ou configurações no pacote. O uso de cada ferramenta segue
sua própria licença. A integração TeraCopy 0.16 delega cópia ao programa instalado;
compactados 0.17 supervisionam a CLI 7z.exe. O DirOpen não inclui 7z.exe/7z.dll.

- Identificar a licenca aplicavel aos arquivos reais de Qt, plugins e componentes
  incorporados, incluindo suas atribuicoes e direitos autorais. Incluir os textos
  completos necessarios; o SPDX e este documento nao os substituem.
- Para a opcao LGPL, preparar as fontes correspondentes e instrucoes de obtencao/
  recompilacao/substituicao, observando os requisitos da versao aplicavel. Nao
  afirmar cumprimento somente porque o aplicativo liga DLLs dinamicamente.
- Identificar a versao e a configuracao reais do FFmpeg incluido pelo kit e revisar
  suas licencas/fontes/componentes. Nao generalizar a partir de uma outra versao.
- A partir da 0.13, o pacote inclui Qt PDF e seu PDFium. Conferir as atribuicoes,
  textos e fontes correspondentes do modulo e dos componentes incorporados.
- Conferir qualquer origem external/, inclusive compiladores de shaders/D3D,
  conforme os termos de redistribuicao do fornecedor. A origem de deployment nao
  estabelece por si so uma permissao.
- O script usa --no-compiler-runtime: nao copia DLLs avulsas do Visual Studio nem
  inclui/executa instalador Microsoft. Documentar o Redistributable x64 adequado
  como requisito separado e testar sua ausencia/presenca em outro Windows.

## Referencias oficiais conferidas em 15/10/2026

- [Deployment no Windows](https://doc.qt.io/qt-6/windows-deployment.html)
- [Licenciamento Qt](https://doc.qt.io/qt-6/licensing.html)
- [Requisitos GPL/LGPL do Qt](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt Multimedia e componentes](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [Qt PDF e licencas de seus componentes](https://doc.qt.io/qt-6/qtpdf-licensing.html)
- [FFmpeg fornecido com Qt 6.12.0](https://doc.qt.io/qt-6.12/qtmultimedia-attribution-ffmpeg.html)
- [Redistributable Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)

A instalacao local Qt 6.12.0 fornece um SPDX que identifica FFmpeg 9.0.1. Essa
observacao nao se aplica automaticamente ao Qt 6.8.3 da CI ou a outro kit.
