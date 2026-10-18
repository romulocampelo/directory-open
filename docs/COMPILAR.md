# Compilação e testes no Windows

## Ambiente

Use Qt 6.8+ MSVC 2022 x64, Visual Studio 2022 ou posterior com desenvolvimento
C++ para desktop e Windows SDK, CMake 3.21+ e Ninja. Qt Widgets/Test, Multimedia/MultimediaWidgets, PDF e PrintSupport
são necessários para desenvolvimento. MinGW, Linux e macOS não são suportados:
operações usam APIs nativas do Windows. Qt PDF deve corresponder à versão e ao kit MSVC do restante do Qt.

A partir da 0.13, adicione **Qt PDF** pelo MaintenanceTool da instalação Qt se o
módulo estiver ausente. Ele é distribuído como extensão Qt PDF; não é necessário
instalar Qt WebEngine. build.cmd somente verifica/compila, sem instalar dependências.

Ambiente local validado: Qt 6.12.0, MSVC v143 14.44.35207, Windows SDK instalado,
CMake e Ninja. O workflow usa Qt 6.8.3/MSVC no Windows 2022.
Consulte os [resultados de cada commit](https://github.com/romulocampelo/directory-open/actions/workflows/windows.yml);
um resultado anterior não valida automaticamente alterações posteriores.

build.cmd descobre Visual Studio por vswhere, aceita Qt de QT_ROOT_DIR e procura
Ninja instalado. Prefere MSVC 14.44 quando presente, ou o padrão da instalação
selecionada. Não instala ferramentas.

## Configurar caminhos

Variáveis NAVEGA_* são mantidas por compatibilidade com o nome anterior.
Escolhas explícitas têm prioridade sobre a descoberta:

~~~powershell
$env:NAVEGA_QT_DIR = 'C:\Qt\6.12.0\msvc2022_64'
$env:NAVEGA_VS_DIR = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$env:NAVEGA_NINJA = 'C:\Qt\Tools\Ninja\ninja.exe'
# Opcional, se esta versão estiver instalada:
$env:NAVEGA_MSVC_VERSION = '14.44'
& .\build.cmd
~~~

Não é necessário definir todas: o Qt é a escolha mais comum a ajustar. As variáveis
valem para o terminal atual. MSVC/SDK/CMake/Ninja/Qt ausentes precisam ser instalados
pelo desenvolvedor; não são instalados pelo script.

## Compilar

~~~powershell
& .\build.cmd
# Reconfigurar o cache CMake e reconstruir o necessário:
& .\build.cmd --fresh
# Novo build separado, preservando builds e janelas existentes:
& .\build.cmd --build-dir '.\build-p01a024'
~~~

--fresh requer CMake 3.24 ou superior e remove o cache de configuração do CMake; não promete recompilar objetos
inalterados. O script configura Release, compila, executa testes e distribui Qt
junto de build/DirOpen.exe. Uma fase que falha impede as seguintes. UTF-8 mantém
consistente a leitura de dependências do MSVC pelo Ninja.
A opção `--build-dir` aceita um caminho dentro deste repositório, inclusive com
espaços/acentos e pais aninhados; caminhos relativos são relativos ao terminal.
O destino/pais não podem ser links/junções nem a raiz do repositório. Um build
com imagem observada em execução é recusado. Escolha uma pasta nova para outro
candidato; reutilização incremental continua compilando e testando. A descoberta
do kit permanece em build.cmd, e a instrumentação de apresentação é fixada em OFF.
`--fresh` e `--build-dir` podem ser combinados. Códigos de falha positivos e
negativos impedem avançar às próximas fases; a presença de EXE antigo não é aceite.
A disponibilidade de --fresh é descrita no [manual do CMake](https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-fresh).

Para configuração manual, em terminal de desenvolvimento x64 do Visual Studio:

~~~powershell
$env:PATH = "$env:NAVEGA_QT_DIR\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$env:NAVEGA_QT_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
~~~

## Verificação

CTest contém vinte e quatro suítes C++ e uma verificação PowerShell dos contratos de pacote:
navegação, operações nativas, sessão, clipboard/arraste,
busca/favoritos, exibição por pasta, miniaturas, visualizador, comparação de pastas,
renomeação em lote, mídia, histórico/reversão, miniaturas PDF/vídeo, texto do PDF
e menu nativo do Windows. everything_search verifica contratos de busca compartilhados,
protocolo nativo controlado, paginação, indisponibilidade, respostas inválidas,
escopo, arquivos alterados, links, cancelamento e timeout. As simulações não
comprovam o comportamento do Everything real. A suíte teracopy cobre o adaptador
de cópia; a verificação opcional da instalação real está descrita abaixo.

`interface_customization` verifica limites/codec, legado, perfis versionados,
backup/idempotência/concorrência, conflitos e prefixos de atalhos, recuperação,
Aplicar/Cancelar, persistência dos controles e compartilhamento das ações reais
entre barra/menu/central. Capturas opcionais de todas as páginas usam
`DIROPEN_INTERFACE_CAPTURES` com diretório próprio, plugin `windows` e fixtures.
Execute o teste GUI com `Start-Process -Wait` e confira o código de saída/log;
não presuma que a invocação direta de um executável GUI aguardou seu término.

Everything é opcional em execução; não é dependência do build. Para verificar a
instância padrão já aberta, com dados próprios e sem alterar sua configuração:

~~~powershell
$env:PATH = 'C:\Qt\6.12.0\msvc2022_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'C:\Qt\6.12.0\msvc2022_64\plugins\platforms'
$env:DIROPEN_TEST_EVERYTHING = '1'
& .\build\everything_search_tests.exe realEverythingAndNativeSearchDialog -o build\everything-native.xml,junitxml
~~~

A verificação real compara nomes/padrões com a busca direta, inclui ocultos e
paginação, e testa a janela/abertura solicitada. Requer que a pasta temporária de
demonstração esteja indexada. Abertura é capturada por sinal, sem executar leitores
externos. Sem a variável, esse caso é omitido explicitamente na suíte padrão.
Na 0.22, o mesmo caso opcional também compara caminho completo/padrões e filtros
de tamanho/data/tipo contra a busca direta. Os casos padrão novos conferem filtros
compartilhados, limites separados de candidatos/resultados, páginas recusadas,
cancelamento e seleção de instância por protocolo controlado. Isso não certifica
uma instância nomeada real. `DIROPEN_SEARCH_FILTER_CAPTURE` permite capturar a
janela de busca filtrada no caso `searchDialogEditsSharedFiltersAndSelectsNamedInstance`;
use a plataforma Windows e aguarde o teste GUI com `Start-Process -Wait`.
A medição de tarefa completa é outro caso opcional, com fixture e saída fornecidas
por DIROPEN_SEARCH_BENCH_*; não se presume resultado de desempenho em outra máquina.
Fixtures e sessões
alternativas preservam dados pessoais. Os XMLs de resultado ficam em build.
Testes de clipboard preservam/restauram formatos em memória. Testes de Lixeira
usam itens próprios e restauram somente esses itens.

A suíte shell_context usa a plataforma Windows real, com HWND/HMENU, fixtures,
abertura/cancelamento de menus, submenus dinâmicos e Propriedades. As entradas de
extensões variam por computador; a suíte não executa comandos de terceiros.
Propriedades é fechada somente pelo nome único do arquivo de demonstração, pois
o Windows pode hospedá-la em outro processo. Os testes conferem a destruição dos
menus possuídos pelo adaptador; recursos globais das extensões não provam vazamento.
O fluxo da árvore também verifica a confirmação da Lixeira sem excluir o alvo.

A maioria usa offscreen, sem desktop. Capturas opcionais e bloqueio real da área
de transferência têm omissões explícitas. Para complementar a GUI, configure
Qt/bin no PATH e rode as suítes numa sessão interativa Windows:

~~~powershell
$env:PATH = "$env:NAVEGA_QT_DIR\bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:NAVEGA_QT_DIR\plugins\platforms"
& .\build\thumbnail_tests.exe
& .\build\preview_tests.exe
& .\build\transfer_tests.exe
& .\build\comparison_tests.exe
& .\build\batch_rename_tests.exe
& .\build\media_tests.exe
& .\build\rename_history_tests.exe
& .\build\document_thumbnails_tests.exe
& .\build\pdf_text_tests.exe
& .\build\shell_context_tests.exe
~~~

A suíte de texto PDF registra uma fonte Arial do próprio Windows somente no processo
de teste, para produzir fixtures com texto também em offscreen. Não instala nem
redistribui fontes. DIROPEN_PDF_TEXT_CAPTURE recebe um caminho PNG opcional para
capturar a nova busca com dados de demonstração ao executar com o plugin windows.

Para uma captura opcional, defina DIROPEN_THUMBNAIL_CAPTURE ou
DIROPEN_PREVIEW_CAPTURE com um destino local antes da suíte correspondente.
DIROPEN_COMPARISON_CAPTURE captura a comparação com endereços ilustrativos.
DIROPEN_BATCH_CAPTURE e DIROPEN_MEDIA_CAPTURE capturam
as novas interfaces usando apenas dados demonstrativos.
DIROPEN_CAPTURE_DIR recebe a pasta das capturas de histórico e miniaturas PDF/vídeo
nas novas suítes; são opcionais e executadas apenas com o plugin windows.

A suíte de mídia gera WAV PCM e AVI/MJPEG próprios e exercita o backend real do
Qt, incluindo quadros decodificados. Não usa vídeo baixado ou ffmpeg.exe. O
workflow instala os módulos qtmultimedia e qtpdf no runner; localmente, se faltar o módulo,
use o MaintenanceTool da instalação Qt e adicione Qt Multimedia da mesma versão
e kit MSVC x64. build.cmd não o instala. windeployqt distribui as bibliotecas e
plugins de mídia correspondentes. Não há garantia de todo codec ou dispositivo
de áudio/GPU a partir de um único ambiente.
CI não substitui testes interativos nem pacote numa máquina sem ferramentas de
desenvolvimento. Um aviso OLE foi observado na limpeza do clipboard em testes
nativos anteriores, sem falha; não considerar sua causa ou correção estabelecidas.

### Verificar o adaptador de compactados

A suíte `archive` verifica o parser limitado e os nomes do Windows. Com 7-Zip
instalado, exercita criação, listagem, teste e extração ZIP/7z reais, corrupção,
nomes perigosos dentro de ZIPs, link simbólico real, junção substituindo o destino,
cancelamento parcial, identidade alterada, senha recusada, Zone.Identifier e
os comandos da janela principal. Sem instalação, os casos de provedor real são
omitidos explicitamente; testes do parser não comprovam o provedor.

Execute `archive_tests.exe` com o mesmo kit/ambiente dos demais testes, usando
`Start-Process -Wait` para aguardar sua saída no PowerShell. A variável
`DIROPEN_CAPTURE_ARCHIVE` salva uma captura demonstrativa apenas com o plugin
`windows`. `benchmarkWorkflow` é opcional e exige uma fixture com marcador,
motor e destino exclusivos; compara a aplicação com os comandos diretos apenas
sobre dados gerados. A extração direta usada pelo benchmark não é um fluxo do produto.

## Executar sem usar a sessão pessoal

A suíte `media_tools` usa os executáveis opcionais ExifTool, ImageMagick e
FFmpeg/ffprobe encontrados em `tools`, PATH ou caminhos configurados. Faz testes
reais sobre JPEGs e um vídeo gerados: Unicode, metadados, rotação/dimensões, quatro
perfis de mídia, identidade desde a prévia, colisões, marca de origem, cancelamento
parcial/limpeza, menus, sessão e confirmações das três telas. Ausência de motor
gera omissão explícita. `DIROPEN_CAPTURE_MEDIA` aponta a uma pasta para capturas
demonstrativas; use plugin `windows` e `Start-Process -Wait`. `benchmarkWorkflow`
é opcional, exige fixture marcada e compara o fluxo com comandos diretos. Os testes
nunca usam arquivos pessoais ou instalam ferramentas.

A suíte `specialized_tools` cobre contratos, sessão, respostas inválidas,
cancelamento durante execução e encerramento de filhos próprios. Se encontrar os
executáveis opcionais em `tools`, Program Files ou PATH, também executa restic,
rclone e ripgrep reais sobre fixtures: duas versões/restauração, senha errada,
prévia/cópia/conflito/recebimento por backend local e busca com exclusões/limites.
Ausência de ferramenta gera omissão explícita, sem provar o provedor real.
Não acessa contas ou configurações pessoais. `DIROPEN_CAPTURE_TOOLS` aponta para
uma pasta existente para capturas demonstrativas; use o plugin `windows` e aguarde
o executável com `Start-Process -Wait`. `benchmarkWorkflow` é opcional e exige uma
fixture marcada, com diretório e destinos exclusivos. Nenhum binário opcional é
baixado pelo build ou incluído automaticamente no pacote.

A suíte `teracopy` testa o adaptador, início de processo, Unicode, falhas,
cancelamento da preparação, junções, confirmação e sessão sem copiar dados reais.
O processo de prova pode terminar com código zero sem copiar nada: o teste exige
que o resultado continue `Delegated`, sem registrar conclusão.

Para testar a instalação real com fixtures, defina
`DIROPEN_TEST_TERACOPY_REAL=1` e execute `teracopy_tests.exe` com o plugin
`windows`, aguardando a saída (por exemplo, `Start-Process -Wait`). Isso inicia
o TeraCopy externo e usa suas opções atuais, sem alterá-las. Os casos
`realInstalledCopy` e `realUiCopy` conferem seleção múltipla, nomes Unicode,
conflito, pasta vazia, caminho maior que 260 caracteres e a cópia pela interface.
`DIROPEN_TEST_TERACOPY_ROOT` permite reter fixtures numa pasta existente de testes;
`DIROPEN_CAPTURE_TERACOPY` salva a confirmação com dados demonstrativos, somente
com o plugin nativo. O benchmark é opcional e exige fixture com marcador próprio.
Mocks/processos de prova não comprovam o comportamento do TeraCopy real.

~~~powershell
& .\build\DirOpen.exe --no-session
# Ou um registro separado em caminho completo de sua escolha:
& .\build\DirOpen.exe --session-file 'C:\Exemplos\sessao-de-teste.json'
~~~

A sessão normal continua em %LOCALAPPDATA%\Navega\session.json para preservar
usuários do nome anterior. Não copiar esse registro para o Git.

O histórico é desativado por --no-session. Uma sessão isolada habilita um histórico
próprio em CAMINHO-DA-SESSÃO.rename-history. Os testes usam somente diretórios
temporários; não inclua esses mapas de caminhos pessoais no repositório.

As suítes `office_text`, `ocr` e `specialized_launch` cobrem a prévia XML com
7-Zip real, o OCR WinRT disponível e as delegações explícitas. OCR sem idioma
instalado é omitido; as imagens de teste carregam Arial do Windows também no
plugin offscreen. Os testes de rclone usam apenas configuração temporária de
backend local, sem OAuth ou conta real. O teste de encaminhamento TeraCopy usa
um executável de prova copiado, com sua pasta de DLLs no PATH; isso não comprova
cópia pelo motor real.

RAR pode ser conferido com fixtures públicas opcionais, sem incluir compactados
no repositório. O script próprio abaixo baixa cinco arquivos de libarchive
v3.8.2, confere SHA-256 e guarda a procedência em cache ignorado. Requer rede
somente nessa preparação; os testes e o aplicativo não baixam nada.

~~~powershell
python tests/fetch_rar_fixtures.py
$env:DIROPEN_RAR_FIXTURES = (Resolve-Path '.cache/rar-fixtures').Path
ctest --test-dir build -R '^archive$' --output-on-failure
~~~

A suíte verifica RAR antigo, RAR5 armazenado/sólido, integridade, extração e
conteúdo exato, originais preservados e recusa de links/volumes. Sem a variável,
somente esse caso público opcional é omitido; os testes sintéticos de limites e
as verificações de ZIP/7z continuam. `batch_rename` também verifica receitas,
Unicode, conflitos, persistência, backup e invalidação da confirmação ao editar.


As suítes `operation_queue`, `synchronization`, `user_script` e
`windows_integration` verificam os quatro últimos blocos do inventário.
Fila/sincronização fazem cópias nativas somente em pastas temporárias e testam
mudanças desde a aprovação, pausa/cancelamento, parciais e rotas da janela.
O processo de prova TeraCopy continua sem certificar cópia pelo motor real.

Scripts usam Python real opcional (`DIROPEN_TEST_PYTHON` ou instalação detectada),
stdin Unicode, cópia imutável, consentimento fresco, cancelamento e preservação
de saídas. O exemplo é conferido com saídas novas, conflito e nomes de dispositivos
recusados. Não executam scripts pessoais nem persistem entradas/consentimento.

Associações usam uma chave própria UUID em `HKCU\Software\DirectoryOpen.Tests`,
sem redirecionar hives globais. Testam tipos/bytes/ausências, idempotência,
mudanças externas, diário persistente e recuperação de falha de permissão
provocada somente na fixture. Uma chamada real `ShellExecuteEx` com `hkeyClass`
isolado verifica o encaminhamento, sem mudar os padrões reais. TxR não é usado;
este Windows retornou 6801 durante a investigação da primeira abordagem.

Para capturas nativas, defina `DIROPEN_QUEUE_CAPTURES`, `DIROPEN_SYNC_CAPTURES`,
`DIROPEN_SCRIPT_CAPTURES` ou `DIROPEN_WINDOWS_CAPTURES` com a pasta de saída,
use o plugin `windows` e aguarde com `Start-Process -Wait`.

## Isolamento e medições dos relatórios (0.24 local)

`folder_report_tests` e `folder_size_tests` criam uma chave UUID exclusiva antes
de QApplication. `RegOverridePredefKey` redireciona HKCU somente nesse processo;
filhos não herdam o redirecionamento. O probe usa exatamente o construtor
NativeFormat/UserScope/QtProject do seletor Qt. Falha de isolamento impede
executar os casos. O produto e os demais processos mantêm suas configurações.

QApplication e os objetos QSettings/seletores terminam antes de desfazer o
redirecionamento. A limpeza confirma o marcador exclusivo no handle retido e
no caminho exato da chave, remove o conteúdo próprio e a chave exata. Falha conserva
responsabilidade/recibo para diagnóstico; não pesquisar e excluir por prefixo.
Recibos JSON em `<diretório de trabalho>/.cache/test-settings/` contêm apenas
caminho de chave de teste e nonce. Encerramento abrupto pode deixar uma chave
e seu recibo: analisar esse recibo individual, validar marcador e identidade
antes de qualquer recuperação. Chave sem prova de propriedade fica preservada.
Os testes exercitam retorno antecipado, falha após abertura/criação/redirecionamento/
probe, saída após inicialização Qt, falha simulada ao desfazer redirecionamento e antes de excluir a chave,
término abrupto em filho e recuperação explícita com recusa de nonce incorreto.
Isso não comprova recuperação automática depois de qualquer pane do sistema.

O atributo de seletor Qt usado na automação é restaurado por escopo. CSV/TXT/PDF
incluem cancelamento do seletor, criação exclusiva e resultado estável com a
origem removida. Não acionar impressão física nos testes. Usar plugin `windows`
para conferir fontes, layout, exposição e PDF; `offscreen` não valida pintura nativa.

A opção CMake `DIROPEN_PRESENTATION_PROBE=ON`, desativada por padrão, mede consulta,
formatação e aplicação nos controles do diálogo. O QApplication do benchmark mede
eventos LayoutRequest/Paint. Os tempos dos eventos são inclusivos e podem se
sobrepor; não somá-los ao motor concorrente como uma decomposição aditiva.
O runtime entregue deve ser compilado com a opção OFF. Os benchmarks opcionais
continuam em `representativeMeasurements`, selecionados por
`DIROPEN_SIZE_BENCHMARK`/`DIROPEN_SIZE_BENCHMARK_SHAPE`; dados são gerados no teste.
Registrar processo novo, primeira/segunda janela, exposição, tempo/CPU/pico e
variação entre processos. Arquivos recém-criados são dados aquecidos, não frio real.

Antes/depois das execuções nativas, comparar hashes de preferências reais e da
sessão sem registrar valores pessoais. Se houver mudança concorrente, relatá-la;
não restaurar automaticamente um snapshot. A alteração histórica dos primeiros
seletores 0.24 não tem snapshot anterior: o backup posterior não reconstrói esse
estado e não deve ser restaurado automaticamente.
