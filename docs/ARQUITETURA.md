# Arquitetura

Aplicativo próprio em C++20/Qt Widgets para Windows, sem dependência de REA ou
Directory Opus. CMake produz DirOpen.exe e uma biblioteca interna de UI usada
pelo aplicativo e pelos testes; não representa um plugin ou SDK público.

## Direção de evolução: orquestração de ferramentas especializadas

Direção aprovada pelo usuário em 15/10/2026: o DirOpen é a interface central de
gerenciamento e coordena os motores especializados mais adequados para cada
capacidade. Everything foi entregue na 0.15; cópia delegada ao TeraCopy na 0.16;
compactados com 7-Zip na 0.17; restic/rclone/ripgrep na 0.18;
ExifTool/ImageMagick/FFmpeg na 0.19. O duplicador Python
é posterior. A tabela abaixo descreve os proprietários atualmente implementados.

Organizar a evolução em três responsabilidades: interface (navegação e comandos),
coordenação (tarefas, confirmação, cancelamento e resultados) e adaptadores
(contratos de cada ferramenta). A composição lembra ambientes de desktop com
utilitários especializados: a experiência do usuário permanece consistente mesmo
quando o motor de uma função muda. A supervisão de processos, acesso a discos e
permissões continua pertencendo ao Windows.

Cada integração deve ter um adaptador responsável pela interface externa,
compatibilidade, disponibilidade, identidade dos recursos e interpretação dos
resultados. Os fluxos do aplicativo consomem fatos normalizados, preservando
origem, limites e resultados parciais; não reinterpretam saídas de terceiros.
Contratos e abstrações novos devem nascer das integrações concretas, sem construir
antecipadamente uma plataforma genérica de plugins.

Manter seleção, destino, confirmação e acompanhamento no DirOpen quando o contrato
externo permitir. Informar quando o controle estiver numa janela externa e quais
resultados puderam ser observados. Encerramento de processo não estabelece êxito.
Uma falha após possível alteração não permite repetir automaticamente a operação
em outro motor: reconciliar o que foi concluído antes de propor nova tentativa.
Configurações globais de terceiros não pertencem implicitamente ao DirOpen.

Escolher motores por qualidade, integridade, desempenho medido e compatibilidade;
preservar escolhas explícitas do usuário. Medir também o custo da integração e da
apresentação dos resultados, não apenas o tempo interno da ferramenta.

## Responsabilidades

| Área | Proprietário |
|---|---|
| Composição, ações e janela ampliada | mainwindow |
| Menu do Shell, seleção PIDL, COM e encaminhamento de mensagens | shellcontextmenu |
| Modelo somente leitura, navegação e seleção compartilhada | filepanel |
| Política de arraste nas duas apresentações | filelistview / FileTransferView |
| Identidade e formatos da área de transferência | filetransfer |
| Operações nativas e resultado comum por item | fileoperation |
| Descoberta e envio de cópia pela CLI pública, listas/recibos externos | teracopy |
| Catálogo ZIP/7z, processos 7-Zip e publicação de compactados/extração | archive |
| Confirmação, listagem, progresso, cancelamento e destino de compactados | archivedialog |
| Contratos CLI/JSON de restic/rclone/ripgrep | specializedtools |
| Configuração não secreta, prévia, confirmação e resultados das três ferramentas | toolsdialog |
| Transporte limitado, stdin UTF-8 e supervisão da família das ferramentas | externalprocess |
| Planejamento/identidade, formatos, perfis, metadados e publicação de conversões | mediatools |
| Preferências, seleção/prévia visual, confirmação e resultados de conversões | mediatoolsdialog |
| Leitura/escrita opaca de Zone.Identifier para compactados e derivados | fileorigin |
| Planejamento/identidade e motor nativo do lote | batchrename |
| Prévia, confirmação e trabalhador exclusivo do lote | batchrenamedialog |
| Registro, codec, reconciliação e reversão dos lotes | renamehistory |
| Seleção/confirmação do desfazer e trabalhador | renamehistorydialog |
| Reprodução local e propriedade do player/dispositivo | mediapreview |
| Classificação de extensões de áudio/vídeo | mediatypes |
| Confirmação, destino e conflitos | operationdialog |
| Registro, backups, recuperação e concorrência da sessão | sessionstore |
| Fatos, codec e catálogo das exibições por pasta | folderview |
| Trabalhador e codec de texto | previewloader |
| Documento PDF, texto/geometria, busca e cache por conteúdo | pdfrenderer (Qt PDF) |
| Controles e pintura limitada ao viewport | previewwidget |
| Dimensões, escala e orientação EXIF compartilhadas | imagepreview |
| Trabalhador, frescor e orçamento de miniaturas | thumbnailcache |
| Demanda do viewport e pintura/seleção das células | thumbnailview |
| Preparação, escopo e interpretação compartilhada do nome | searchquery |
| Seleção de motor, busca direta e trabalhador | filesearch |
| Protocolo Query2W, limites e parsing de páginas | everythingipc |
| Canal Win32, versão/prontidão, consulta e revalidação indexada | everythingsearch |
| Controles e apresentação dos resultados da busca | searchdialog |
| Enumeração nativa sem seguir links, união de raízes, contadores e resultado de tamanho | foldersize |
| Seleção explícita, snapshots limitados do trabalhador e apresentação sem total fictício | foldersizedialog |
| Projeção única do resultado estável, escaping e saídas exclusivas CSV/TXT/PDF; paginação sem reenumeração | folderreport |
| Prévia modal sobre valor concluído, escolha explícita de PDF/impressora, dispositivo com vida útil própria | folderreportpreview |
| Inventário, fatos de comparação e trabalhador | foldercomparison |
| Controles e resultados da comparação | comparisondialog |
| Representação de caminhos nas APIs Win32 e na interface | windowspath |
| Organizadores de favoritos e exibições | favoritesdialog / folderviewsdialog |
| Kit do build, candidato imutável, inventário e verificação de distribuição | deployment-info CMake / scripts/package-* / verify-package |

## Contratos essenciais

- SearchRequest preserva método, instância, pasta, nome/caminho e filtros. searchquery
  valida e prepara uma única interpretação literal/padrão; ambos os motores usam
  esses fatos. `FolderRuleMatcher` possui o predicado único de filtragem, consumindo
  `FolderRuleFacts` normalizados de QFileInfo na navegação ou de metadados nativos
  atuais na busca. Padrões/bytes/datas pertencem a arquivos; pastas seguem sua opção.
  Resultados e falhas esperadas são tipados. Automatic só muda para Direct em
  Unavailable/NotReady/Unsupported antes de receber resultados; não muda em zero
  correspondências, Timeout, Access ou Protocol. A UI mostra fonte e motivo.
- Na 0.22, Everything seleciona candidatos por nome/caminho e os filtros usam
  GetFileAttributesExW; não confunde tamanho de pasta com bytes de arquivo nem
  metadados inacessíveis com ausência. Candidatos (até um milhão) e resultados
  (até dez mil) possuem contadores/limites separados; uma página sem aceitos
  continua a paginação. Prazo, memória e cache de ancestrais permanecem limitados.
  O total do índice é de candidatos, anterior aos filtros; não é snapshot atômico.
- Everything usa o Query2W público por WM_COPYDATA, no trabalhador. Um canal é dono
  da janela de resposta e do handle retido do processo consultado, sem autoridade
  para encerrar/reconfigurar o Everything. Prazo/cancelamento delimitam a espera;
  chamadas nativas de arquivos podem precisar terminar. IDs distinguem respostas
  atrasadas; mensagens irrelevantes não são interpretadas como resultados.
- everythingipc confere tamanho, contagens, offsets, flags, UTF-16 e terminadores
  antes de alocar dados derivados. Capacidade de página é derivada de 64 MiB e do
  caminho Win32 máximo (1.023 itens); retenção total de caminhos limitada a 16 MiB.
  Cache de ancestrais: até 1.024 entradas/1.048.576 unidades UTF-16 por página,
  profundidade até 256. Os objetos da UI e o índice externo têm custos adicionais.
- O adaptador valida toda a página antes de contabilizar resultados. Prefixo de
  caminho sem distinção de caixa não prova escopo: a identidade nativa da pasta
  também é conferida, inclusive em diretórios com distinção de caixa. Confere
  ancestrais sem atravessar links, tipo e existência dos itens. Um total que muda
  entre páginas interrompe a consulta sem descartar páginas já entregues.
  Índice concluído não prova cobertura/atualização de toda a árvore; isso permanece
  explícito na apresentação. Mudanças de conteúdo sem mudança do total não formam
  um snapshot garantido do índice.
- Fonte do protocolo: [SDK/IPC oficial](https://www.voidtools.com/support/everything/sdk/ipc/).
  A implementação é própria; não incorpora DLL, lib ou fonte do SDK baixado como
  referência local. Nome literal usa tradução textual explícita na 1.5; padrões
  usam a expressão preparada. Nenhuma preferência global do Everything é alterada.

- O menu clássico usa IShellFolder/GetUIObjectOf para seleção na mesma pasta e
  CreateViewObject para o fundo. O proprietário mantém PIDLs, COM, HMENU e
  IContextMenu2/3 até encerrar a interação/invocação, antes de abrir diálogos próprios.
  IDs de extensões e ações próprias usam faixas separadas; rótulos traduzidos não
  decidem operações. GetCommandString identifica os verbos padrão encaminhados
  às ações existentes. Seleção inválida/mista não é reduzida a um subconjunto.
  Falhas preservam o menu Qt. O gancho da janela só existe durante TrackPopupMenuEx;
  submenus próprios não são encaminhados às extensões. Coordenadas lógicas são
  convertidas pelo DPI da janela antes do posicionamento nativo.
- A opção nativeContextMenu é um booleano aditivo da sessão v1, ativado quando
  omitido em sessões anteriores. SessionStore valida o tipo e usa sua política de
  backup/gravação atômica existente. MainWindow escolhe o menu simples antes de
  criar ShellContextMenu quando a opção está desativada, também para a árvore.
- Ações explicitamente delegadas a extensões são uma fronteira externa: não usam
  o motor/histórico próprio e não retornam comprovação de conclusão. O menu só é
  aberto fora de operações/confirmacões/visualizador modal; fechar a janela durante
  a interação é recusado. Não se modifica registro nem se instalam extensões.
  Referências: [GetUIObjectOf](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-getuiobjectof),
  [CreateViewObject](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-createviewobject),
  [HandleMenuMsg2](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu3-handlemenumsg2).

- Modelos, seleção, filtros e arraste não alteram arquivos. Operações confirmadas
  usam Windows ou, para cópia explicitamente escolhida, TeraCopy. Lixeira usa ITransferSource::RecycleItem; não acrescentar
  exclusão definitiva como alternativa automática.
- Lista e miniaturas selecionam linhas completas do mesmo modelo multicoluna;
  selectedPath/selectedPaths consomem essa seleção compartilhada. Duplo clique
  resolve o caminho pelo índice clicado: pastas navegam, arquivos emitem
  openRequested. MainWindow é dono da abertura externa, compartilhada com
  Abrir/Ctrl+O: libera mídia e delega o QUrl local ao programa padrão do Windows.
  Seleção simples continua alimentando somente a prévia.
- Compactados são uma capacidade própria com ArchiveRequest/ArchiveResult e um
  único ArchiveJob por diálogo. O adaptador descobre 7z.exe em Arquivos de Programas,
  recebe argumentos separados (sem shell), fecha stdin e supervisiona o filho
  direto até o fim. Cancelamento encerra somente esse filho e aguarda sua saída;
  o diálogo aguarda o trabalhador antes de fechar. Não se anexa a processos por PID.
  Saída 0 é necessária; aviso 1, erro, corrupção, sinal ou tamanho divergente não
  são sucesso. Catálogo/diagnóstico limitados a 8/1 MiB; até 10.000 entradas.
  Memória do provedor/dicionário continua dependente do 7-Zip, sem alegar teto rígido.
- archive é dono do formato técnico `l -slt -ba -sccUTF-8` e dos caminhos relativos.
  Não há reinterpretação de texto na interface. Campos ausentes/repetidos/
  desconhecidos, UTF-8 inválido, tamanhos inconsistentes, colisões e links são
  recusados. Formatos de entrada são derivados da extensão no adaptador; somente
  ZIP/7z aceitos. ReadLock impede escrita/exclusão enquanto se lista/testa/extrai;
  a identidade reutiliza inspectRenameItem/RenameIdentity e confere a prévia.
- Criação escreve em área própria temporária no volume final, testa com `7z t`
  e publica com rename sem substituição. Falha na limpeza mantém o caminho no
  diagnóstico. 7z criado com blocos independentes; ZIP/7z usam mx=5 e dois threads.
  Extração não usa `-o`: cada membro é selecionado literalmente com `-spd -ssc --`
  e recebido em stdout, em blocos de 64 KiB. O adaptador valida o tamanho e saída
  antes de publicar cada arquivo. OutputDirectories abre os ancestrais normais
  com OPEN_REPARSE_POINT e sem compartilhar escrita/exclusão enquanto grava;
  a proteção pertence a esses handles, não a rechecagens de texto após a escrita.
  Zone.Identifier é copiado como bytes opacos limitados a 64 KiB; falha não o
  descarta silenciosamente. Outros metadados não são restaurados. Pastas concluídas
  e arquivos anteriores são preservados em cancelamento; não se mescla destino.
- Cancelar preserva trabalhos já concluídos e distingue itens pendentes/parciais.
- TeraCopy recebe apenas Copy com `/RenameCopied /NoClose`; movimentação não
  é delegada. `validateOperationRequest` e `validateOperationTree` são a
  prechecagem compartilhada; listas UTF-16LE com BOM e recibos JSON são gravados
  antes de iniciar a ferramenta. `QProcess::startDetached` recebe argumentos
  separados, sem shell. Não acompanha PID como prova de trabalho/conclusão:
  a CLI pode encaminhar a solicitação para outra instância. O resultado é
  `Delegated`, nunca `Completed`; conta zero conclusões e não participa da
  confirmação de movimentação do clipboard. Nunca repete automaticamente com
  outro motor. As listas sobrevivem ao launcher e ao DirOpen; limpeza manual
  após conferir a ferramenta. A sessão v1 guarda `teraCopyForCopy`, booleano
  aditivo, falso quando ausente; motor ausente não troca a preferência em silêncio.
- Snapshot/resultado pertence ao trabalhador; a UI não interpreta novamente bytes
  de arquivo nem trata falha de leitura como sucesso ou ausência definitiva.
- Preview e miniaturas têm filas substituíveis, limites de recursos e validação de
  identidade. Decodificação pesada fica fora da UI. Não abandonar threads ao fechar.
- A janela ampliada reutiliza o widget; devolver propriedade e visibilidade ao dock
  antes de gravar a sessão. F11 pertence à janela.
- Preferências por pasta são opcionais em sessões antigas. Não escrever dentro das
  pastas navegadas nem perder referências indisponíveis. Nome antigo do diretório
  de sessão e identificadores de clipboard permanecem por compatibilidade.
- Não ler clipboard sincronamente dentro de dataChanged nem reintroduzir sondagens
  OpenClipboard(NULL). Sequência/token protegem mudanças de outros aplicativos;
  movimento concluído não dispara outra exclusão no aplicativo de origem.
- configureTransferInteraction configura a view e seu viewport: IconMode pode
  redefinir a aceitação de drop no viewport. Uma política atende tabela e grade.
- O delegado de miniaturas pinta explicitamente a seleção: alguns estilos nativos
  omitem o destaque quando o layout de texto/ícone padrão é substituído.

## Comparação de pastas

foldercomparison é dono do inventário e da prova: tamanho/data não estabelecem
igualdade de conteúdo. A UI consome classificações e descrições prontas. Nomes
são exatos para preservar diretórios Windows com sensibilidade a maiúsculas.
A cobertura de cada inventário conserva prefixos não examinados; limite global
invalida a prova de ausência naquele lado, sem descartar correspondências vistas.
Um link, erro ou limite de profundidade bloqueia a inferência de ausência abaixo
dele. Pastas correspondentes não recebem uma igualdade agregada indevida.

Um trabalhador mantém um relatório limitado, transferido depois de finished/wait.
A UI consulta dois contadores atômicos a cada 150 ms: não há fila de lotes de
resultados ou atualizações por bloco no loop de eventos. Blocos de conteúdo são
de 256 KiB por lado; a leitura para na primeira diferença ou no cancelamento.
Arquivos usam handles com FILE_SHARE_READ e OPEN_REPARSE_POINT, com metadados e
identidade do handle verificados antes/depois; alterações detectadas são desconhecidas.
A travessia revalida diretórios antes de entrar. Não representa snapshot atômico
nem isolamento contra alterações externas de toda a árvore entre as leituras.
Destrutores cancelam e aguardam o trabalhador, inclusive com resultado ainda não entregue.

windowspath normaliza caminhos absolutos na fronteira Win32: prefixos estendidos
permitem caminhos longos sem depender da política do Windows hospedeiro; apenas
esses prefixos são retirados para exposição ao usuário. Busca e comparação usam
o mesmo proprietário dessa representação.

## Renomeação e mídia

batchrename é dono dos nomes gerados e da identidade observada. O template é
analisado uma vez e nomes/substituições não são interpretados novamente. A expansão
é limitada a 255 caracteres antes de alocar representações maiores; até 1.000 itens
e 2 MiB de caminhos. O plano registra volume/índice/atributos/tamanho/mtime/criação do handle.
A execução revalida todo o plano antes de escrever e por item, mantém a pasta
aberta sem compartilhar exclusão, resolve sua representação Win32 por handle e usa
SetFileInformationByHandle/FileRenameInfo com ReplaceIfExists=false. RootDirectory
é nulo e o destino é absoluto; a forma relativa falhou com Windows 87 no teste nativo.
Não substituir por tentativas de overwrite ou nomes temporários para contornar conflitos.

O diálogo modal mantém a exclusão mútua de operações de MainWindow. Planejar e aplicar
acontecem no trabalhador, com resultado lido após wait, confirmação invalidada por
qualquer mudança e progresso por contador. Linhas são indexadas pela origem; a UI
não refaz geração/validação. selectedPaths ordena os índices pela linha no produtor,
de modo que consumidores recebam a ordem da apresentação, inclusive nas miniaturas.
Cancelamento conserva o que já terminou; sem transação global. Desfazer é um fluxo
explícito do histórico, com nova prévia/seleção/confirmação.

MediaPreview só é criado para uma extensão de mídia candidata. Cada seleção cria
um player próprio, ligado a QFile somente leitura, e destrói o anterior antes de
liberar seu dispositivo; sinais pertencem ao player de origem, evitando respostas
antigas na seleção nova. Decodificação e buffers pertencem ao backend Qt Multimedia;
a UI não lê o arquivo inteiro. Não há autoplay nem avanço automático. O mesmo widget
é reparentado na ampliação/F11, conservando o player. Mudança de arquivo, dock oculto,
fechamento ou operação que muda a origem liberam o player/arquivo.

O pipeline estático imagem/texto/PDF continua com PreviewWorker e serial; uma seleção
de mídia invalida esse trabalho e não aceita seus resultados. Não criar backend de
mídia ao selecionar texto/imagem/PDF ou ao abrir o aplicativo sem mídia. Formatos e
codecs dependem das bibliotecas reais; fixtures WAV e AVI/MJPEG não provam MP4/H.264,
HEVC ou todos os dispositivos. APIs Qt usadas permanecem compatíveis com Qt 6.8.

## Histórico de renomeações

renamehistory é o único dono do formato versionado, parsing, limites e reconciliação.
Antes de qualquer mutação grava um mapa prepared; depois recebe as identidades
finais do resultado nativo e grava finished, com cópia anterior .bak. Usa QSaveFile
e QLockFile durante toda a execução/reversão. Uma falha de gravação antes impede
o trabalho; depois distingue resultado incompleto e preserva o mapa anterior.
Não há escrita de JSON por item nem fila de resultados de renomeação.

Campos Win32 de 64 bits são strings decimais, nunca doubles JSON. Registros são
limitados a 8 MiB, 1.000 itens e 4 MiB UTF-16 de origem+destino; o catálogo para
em 1.001 nomes para detectar capacidade sem enumerar indefinidamente. Até
1.000 registros, sem descarte automático. Leituras do catálogo retêm um registro
por vez e resumos limitados. Formatos desconhecidos/malformados não são sobrescritos.

inspectRenameItem, no motor batchrename, produz identidade e nome real pelo mesmo
handle. A reconciliação distingue nome original restaurado, nome final disponível
para reversão, inalterado e bloqueado. Comparações conservadoras de caminhos evitam
colisões de maiúsculas; o nome exato do disco resolve o caso de renomeação apenas
de caixa. A prova usa volume/índice, atributos, tamanho, modificação e criação, sem
hash integral dos arquivos. Não trata metadados como prova criptográfica de conteúdo.

Ao aplicar a reversão, recarrega o registro sob lock, confere seu SHA-256 contra a
prévia, revalida os itens marcados e monta um BatchRenamePlan reverso. O mesmo
motor nativo faz a última conferência e renomeia sem substituir destinos. O mapa
fica intacto para reconsultar após cancelamento/crash; não é uma pilha cega de undo.
O histórico não pode renomear a própria pasta ou seus ancestrais, incluindo caminhos
resolvidos. Sem sessão, a composição passa root vazio e mantém o lote sem journal;
testes/capturas não abrem o histórico pessoal.

## Miniaturas de PDF e vídeo

ThumbnailSource carrega a classificação produzida por metadados (Image/Pdf/Video);
a chave inclui caminho/tamanho/mtime/tipo. mediatypes é o único catálogo de extensões
de áudio/vídeo usado pelo visualizador e miniaturas. Mantêm-se um trabalhador,
fila substituível 128/painel–256 total, um resultado e cache 64 MiB/256 entradas.

PdfRenderer vive no trabalhador que o usa; pimpl encapsula QPdfDocument/QBuffer,
criados somente ao carregar PDF, e cache de um documento por SHA-256 dos bytes
limitados a 64 MiB. Libera documento antes do dispositivo/bytes. Um único backend
é dono da imagem, dos pontos PDF e da interpretação textual; não há parser paralelo
na UI. O tamanho a 100% continua em 96 dpi; geometria preserva pontos PDF separados
de pixels/zoom/scroll. Visualizador pede 1800×2400; miniatura pede a primeira página
até 256×256, convertida para ARGB32. Arquivo não fica aberto no PDF.

PreviewWorker tem uma solicitação substituível e um resultado, incluindo interações
Find/Select/SelectAll. Cada interação traz digest da cópia exibida; proprietário
rejeita outro documento/página. Troca de consulta/arquivo invalida serial; fechar
cancela cooperativamente e aguarda a chamada Qt em curso. Interações não relêem
o arquivo e não acompanham edições externas; Atualizar cria uma nova cópia.

Busca usa QPdfSearchModel de uma página por vez no trabalhador sem event loop de
indexação; não acumula um modelo de todo o documento. A geometria de busca vem do
backend, sem converter índices de QString em índices internos do PDF. Limites:
10.000 páginas/documento, 128 páginas/solicitação, amostra de 65.537 caracteres para
identificar páginas densas e até 4.096 regiões no resultado. Páginas não examinadas
ou densas tornam uma resposta negativa parcial. Texto/copiar segue QPdfSelection;
sem OCR. As alocações internas de parsing/renderização do Qt/PDFium são adicionais
aos limites de bitmaps, texto e resultados mantidos pelo aplicativo.

PreviewWidget assume ShortcutOverride/KeyPress somente quando o evento pertence a
seus descendentes: Ctrl+F abre busca; Ctrl+A/C na página selecionam/copiam texto.
Isso evita ambiguidade com as ações de arquivos da janela e vale após reparenting
no visualizador ampliado. O aplicativo não escreve no clipboard ao selecionar:
somente uma ação explícita de copiar envia o texto simples.

Vídeo usa QMediaPlayer/QVideoSink locais no trabalhador, com loop de eventos e
sem QAudioOutput. Consome um quadro válido, converte até 16 megapixels, reduz a
256×256 e descarta o QVideoFrame. Leitura por QFile, sem carregar o vídeo inteiro.
Prazo de espera de 8 s e cancelamento cooperativo verificado a cada 25 ms; teardown
do backend nativo não é uma interrupção forçada. Player é destruído antes do arquivo.
Erros ficam no mesmo cache limitado; renderização/codec/GPU consomem memória além
do orçamento de bitmaps. Não instanciar esse backend ao abrir uma lista sem vídeo.

## Testes e evidência

Teste pelo fluxo público da UI e pelos limites produtores quando o fluxo público
não reproduz uma semântica distinta. Use arquivos temporários e sessões isoladas.
Distinguir testes offscreen de clipboard/atalhos nativos e instalação em outro PC.
Aguardar exposição/ativação e usar o filho focado ao simular teclas nativas.
Veja [COMPILAR.md](COMPILAR.md) para execução e omissões.

Referências das integrações: [QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html) e
[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info).

## Personalização da interface (0.20)

`uicustomization` possui o modelo limitado, codec, conflitos de atalhos, estilos
e arquivos de perfil versionados/atômicos/com backup. `SessionStore` incorpora
esse codec de forma aditiva no registro v1. `PreferencesDialog` possui controles
e edição pendente; `MainWindow` aplica somente valores validados e cataloga as
QActions por identidade. Barra, menu pessoal e `CommandsDialog` reutilizam os
mesmos objetos; disponibilidade, confirmação e operação continuam nos donos
existentes. Rotas de recuperação têm atalhos fixos. Não há cópia de contratos ou
configurações do Directory Opus. Inventário e limites em `INTERFACES.md`.

## Cobertura do inventário (0.21)

`panelsession` e `folderview` possuem codecs compartilhados por sessão/layouts;
`workspace` possui os catálogos nomeados. `advancedinterface`, `interfacecommands`,
`interfacestudio` e `mainwindowcustomization` separam contrato, edição e aplicação
das mesmas QActions. `globalhotkeys` possui registro nativo/rollback/liberação,
com ativação por execução. `fileannotations` possui identidade/catálogo; triagem
e editor não reinterpretam EXIF nem transformam marcas em exclusão.

`guardedtransfer` aprova/revalida IDs e nomes pelo produtor `inspectRenameItem`.
`operationqueuedialog` possui tarefas e histórico transitórios; o motor atual
continua dono da mutação. `MainWindow::operationBusy` coordena fila e avulsas.
`synchronization` consome entradas tipadas de comparação, captura alvos e aplica
somente cópias revalidadas; o diálogo invalida aprovação ao editar os parâmetros.

`userscript` possui manifesto/entradas tipados, código imutável, identidade da
cópia privada e limpeza exclusivamente própria. `UserScriptDialog` é dono do
consentimento fresco, nunca persistido. `externalprocess` supervisiona a família;
script não é sandbox. `SessionStore` tem uma tabela única de campos dos motores
e preserva propriedades futuras no documento que já possui.

`windowsintegration` possui valores nativos tipados, backups e diário de mudança
por usuário, limite 512 KiB e escritor único por identidade de usuário. Intenção
e fatos de reversão são persistidos antes dos comandos. Cada etapa confere
valores conhecidos e recibo de gravação; interrupção mantém estado explícito
recuperável. Reverter uma tentativa usa os valores anteriores à tentativa;
desativar uma integração completa usa o backup original. Não depende de TxR.
Chaves de estrutura são preservadas para não apagar gravações concorrentes.
O diálogo mostra esses fatos; alteração de aplicativos padrão pertence à tela
oficial do Windows. Testes injetam uma chave UUID própria, não redirecionam HKCR.

`documenttext` possui seleção/parser XML de partes Office; `ocr` possui a tarefa
WinRT e suas factories por apartamento. `specializedlaunch` possui comandos
delegados; GUI externa, console rclone e 7zFM não produzem conclusão fictícia.

## Propriedade dos relatórios e listagens

FolderSizeJob mantém o resultado com seleção, limites e metadados do pedido.
A enumeração de foldersize pode reter registros quando collectListing foi
solicitado; não muda políticas de links/sobreposição e não reenumera para obter
mais detalhes. Estado da retenção/listagem é distinto do estado do cálculo.
Um registro de pasta não possui tamanho recursivo. Snapshots Qt compartilham
dados por valor; cada retenção/serialização é limitada, sem fila de progresso.

folderreport projeta o valor terminal para CSV/TXT e paginação. A projeção única
preserva cobertura/erros/omissões; cada formato possui escaping próprio. Escrita
exclusiva reaproveita validateItemName e QFile::NewOnly; PDF usa QPdfWriter sobre
o arquivo já reservado. Prévia mantém resultado const e dispositivo PDF próprio;
QPrintDialog só cria outro dispositivo ao pedir impressão. Nenhum desses
consumidores consulta novamente as origens. Mais detalhes e números estão na
validação local .cache/reports/VALIDACAO.md (não distribuída).

Na estabilização transversal 0.24, cada consulta do diálogo ainda obtém o valor
do FolderSizeJob. A formatação prepara o resumo inteiro antes de publicá-lo;
detalhes idênticos usam o texto que já pertence ao documento, preservando seleção
e rolagem. Não há outro proprietário dos fatos, cache, nova enumeração ou cálculo
automático. A instrumentação de apresentação é opt-in de build, sem custo no
runtime normal. O redirecionamento de Registro existe somente nos executáveis de
teste de relatórios/tamanho e envolve todo o ciclo de vida do QApplication.
