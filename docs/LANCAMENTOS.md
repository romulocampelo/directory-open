# Preparar um lançamento

A versão 0.11 está em desenvolvimento. Preparar/publicar o repositório de fontes
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

1. Compilar/testar a revisão exata e registrar o commit.
2. Preparar uma pasta própria de distribuição com DirOpen.exe, dependências Qt,
   plugins necessários (inclusive mídia), instruções, requisitos e avisos/licenças.
3. Identificar a licença e a origem da versão real de Qt e de cada componente
   redistribuído. A licença MIT cobre apenas o código próprio.
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
