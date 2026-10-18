# Preparar um lançamento

A base atual é a versão 0.24.0 estabilizada localmente. O P0.1a prepara um
candidato verificável local, com QA em cópia separada; continua `publishable=false`.
Preparar/publicar o repositório de fontes
não equivale a oferecer um pacote de executáveis validado para outros computadores.

## Repositório de fontes

Incluir fontes, testes, CMake, scripts, documentação e a licença do código próprio.
Conferir arquivos e histórico que serão enviados. Não incluir builds, sessões,
relatórios locais ou instaladores de outros produtos. O repositório deve ter
histórico próprio, sem herdar um projeto diferente.

O repositório público de fontes é [romulocampelo/directory-open](https://github.com/romulocampelo/directory-open),
com main como branch principal. Ele tem histórico independente. Para novas
versões, enviar apenas revisões verificadas desse projeto.

O workflow compila/testa em Windows 2022 com Qt 6.8.3 e ações fixadas por commit.
Conferir o resultado da revisão exata nos [Actions](https://github.com/romulocampelo/directory-open/actions). Não usar o
workflow para publicar Releases ou instalar software no computador do usuário.

## Pacote para download

### Candidato local automatizado

Na raiz do repositório, execute no PowerShell:

~~~powershell
& .\scripts\package-windows.ps1
# Preferir uma pasta de build nova para preservar builds existentes:
& .\scripts\package-windows.ps1 -BuildDirectory '.\build-candidato-novo'
# Use a pasta exata indicada ao terminar:
& .\scripts\verify-package.ps1 -PackageDirectory 'CAMINHO-DO-CANDIDATO'
~~~

O primeiro script sempre executa build.cmd (compilação e testes), lê o kit real
registrado pelo CMake e usa seu windeployqt. Não instala ferramentas ou runtimes.
Cria uma pasta nova e ZIP em .cache/packages, sem sobrescrever candidatos anteriores;
falhas conservam a saída parcial com estado incomplete. Copia somente DirOpen.exe,
dependências selecionadas pelo Qt e documentação própria, sem varrer o build inteiro.
O manifesto registra hashes, origens, revisão, alterações locais e fingerprint dos
fontes; não considera o commit sozinho prova do build quando há alterações locais.
Alterações nos fontes durante a compilação impedem criar o candidato.
O fingerprint também é reconferido após o deploy/cópia dos documentos, antes de
congelar o manifesto. O registro inclui os estados antes/depois, a pasta de build,
o kit efetivo e o hash de deployment-info.json.
O inventário Git usa separação NUL explícita e ordenação ordinal, produzindo o
mesmo fingerprint no PowerShell 5.1 e 7. A descrição fingerprintFormat distingue
essa representação dos candidatos históricos com ordenação implícita; não
comparar fingerprints de formatos diferentes como prova de alteração de fontes.
Falhas preservam diagnóstico
`incomplete`, inclusive antes de existir payload. Chamadas sem argumentos mantêm
o build/ incremental; `-BuildDirectory` aceita pasta nova ou reutilização sem
imagens observadas em uso, sempre com build e gate completos.

Os SPDX do kit são conservados para revisão conforme
[AVISOS-TERCEIROS.md](AVISOS-TERCEIROS.md). O script **não produz um lançamento
liberado para redistribuição**: os avisos/textos/fontes completos e a validação em
outro Windows continuam obrigatórios. Não anexar o candidato local a uma Release.
O runtime Microsoft é requisito externo; não são copiadas DLLs avulsas do compilador.

O segundo script confere todos os hashes/tamanhos e rejeita arquivos adicionais,
links e caminhos fora do pacote. Abre o executável com --no-session e dados próprios,
PATH limitado ao Windows e sem variáveis Qt herdadas; captura a janela e encerra.
Também confere se os módulos Qt/plataforma observados vieram da pasta do candidato.
Guarda evidências em uma nova pasta temporária, fora do pacote. Isso comprova
integridade e abertura local nesse ambiente, sem testar PDF/mídia/operações nem
estabelecer compatibilidade com um segundo computador. O manifesto fica imutável;
o resultado referencia seu SHA-256. A verificação não autentica a origem do ZIP:
para transporte, compare também o hash adjacente obtido de uma origem confiável.
Os scripts são compatíveis com Windows PowerShell 5.1 e PowerShell 7. A suíte CTest
usa RemoteSigned apenas no processo de teste; não altera a política do usuário ou
da máquina. Ambientes com política administrada devem respeitar essa política.

### QA funcional e imutabilidade

Conferir o SHA-256 adjacente, CRC/nomes/caminhos/membros do ZIP e os bytes extraídos
contra o manifesto antes de executar. Testar a extração em caminho com espaços e
acentos. Repetir startup em processos novos sem PATH de desenvolvimento; as
alterações de ambiente pertencem somente ao filho. O manifesto não recebe um
carimbo de sucesso posterior: relatórios externos referenciam seu SHA-256.

Para relatórios/tamanho, prévia/PDF e mídia, copiar o candidato para uma pasta QA
nova. Acrescentar exclusivamente os executáveis de teste do mesmo build e Qt6Test
do kit descrito por deployment-info.json; inventariar origem/hash dessas adições
fora do pacote. Não acrescentar DLLs para ocultar dependências faltantes, não
incluir testes/QtTest no ZIP e não executar seletores sem o isolamento anterior
a QApplication. Conferir módulos carregados, criação exclusiva, fórmulas,
cobertura/paginação, cancelamento e resultado estável após remoção da origem.

O inventário técnico por arquivo deve ligar o manifesto às origens/hashes/versões
e às evidências SPDX. A lista completa de pacotes SPDX pode incluir ferramentas
de build, fontes e código não embarcado: manter presença/obrigações desconhecidas
quando não houver prova. O roteiro P0.1b está em
[VALIDACAO-WINDOWS-INDEPENDENTE.md](VALIDACAO-WINDOWS-INDEPENDENTE.md).

### Requisitos para publicar

1. Compilar/testar a revisão exata e registrar o commit.
2. Preparar uma pasta própria de distribuição com DirOpen.exe, dependências Qt,
   plugins necessários (inclusive mídia), instruções, requisitos e avisos/licenças.
3. Identificar a licença e a origem da versão real de Qt e de cada componente
   redistribuído. A licença MIT cobre apenas o código próprio.
   A partir da 0.13, incluir Qt PDF/PDFium e seus componentes na conferência.
4. Cumprir a licença escolhida. Se Qt for distribuído sob LGPL, incluir seus avisos
   e textos de licença, fornecer o código-fonte correspondente ou oferta adequada
   e permitir a substituição/religação exigida. A ligação dinâmica não dispensa as
   demais obrigações. Conferir também plugins/componentes de terceiros.
5. Documentar/fornecer o runtime Microsoft Visual C++ x64 compatível com o compilador,
   segundo os termos de redistribuição Microsoft. O SDK Windows não é um requisito
   de uso normal do aplicativo; é usado na compilação.
6. Testar o pacote em outro Windows, sem Qt/Visual Studio no PATH: abertura, sessão,
   miniaturas, PDF, comparação de pastas, lote de renomeação, áudio/vídeo, foco/F11, copiar/mover, cancelamento, conflitos e Lixeira usando
   somente dados de demonstração.
7. Gerar hash SHA-256, escrever notas/limitações e anexar o pacote à Release.
   Marcar o primeiro download como pré-lançamento. Não versionar o pacote no Git.

Avisos de terceiros e validação fora da máquina de desenvolvimento ainda precisam
ser concluídos para o pacote real. Este documento não declara um binário pronto
para distribuição nem determina que qualquer instalação local tem licença específica.

## Componentes de mídia

O Qt Multimedia pode distribuir backend FFmpeg e suas DLLs/plugins. Inventariar
as versões/licenças reais geradas por windeployqt, incluindo FFmpeg e codecs, seus
avisos e requisitos de código-fonte/redistribuição, antes de oferecer binários.
O código MIT do aplicativo não substitui essas licenças. A disponibilidade de
um codec e eventuais requisitos de sua distribuição precisam ser conferidos
para o pacote/plataforma real; o repositório de fontes não inclui tais binários.

## Referências oficiais

- [Licenciamento Qt](https://doc.qt.io/qt-6/licensing.html)
- [Obrigações GPL/LGPL no Qt](https://www.qt.io/development/open-source-lgpl-obligations)
- [Runtime Visual C++](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)
- [Releases no GitHub](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)

- [Qt Multimedia e FFmpeg](https://doc.qt.io/qt-6/qtmultimedia-index.html)
