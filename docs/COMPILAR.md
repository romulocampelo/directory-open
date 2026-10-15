# Compilação e testes no Windows

## Ambiente

Use Qt 6.8+ MSVC 2022 x64, Visual Studio 2022 ou posterior com desenvolvimento
C++ para desktop e Windows SDK (C++/WinRT), CMake 3.21+ e Ninja. Qt Widgets/Test e Multimedia/MultimediaWidgets
são necessários para desenvolvimento. MinGW, Linux e macOS não são suportados:
operações e PDF usam APIs nativas do Windows.

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
~~~

--fresh requer CMake 3.24 ou superior e remove o cache de configuração do CMake; não promete recompilar objetos
inalterados. O script configura Release, compila, executa testes e distribui Qt
junto de build/DirOpen.exe. Uma fase que falha impede as seguintes. UTF-8 mantém
consistente a leitura de dependências do MSVC pelo Ninja.
A disponibilidade de --fresh é descrita no [manual do CMake](https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-fresh).

Para configuração manual, em terminal de desenvolvimento x64 do Visual Studio:

~~~powershell
$env:PATH = "$env:NAVEGA_QT_DIR\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$env:NAVEGA_QT_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
~~~

## Verificação

CTest contém onze suítes: navegação, operações nativas, sessão, clipboard/arraste,
busca/favoritos, exibição por pasta, miniaturas, visualizador, comparação de pastas,
renomeação em lote e mídia.
Fixtures e sessões
alternativas preservam dados pessoais. Os XMLs de resultado ficam em build.
Testes de clipboard preservam/restauram formatos em memória. Testes de Lixeira
usam itens próprios e restauram somente esses itens.

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
~~~

Para uma captura opcional, defina DIROPEN_THUMBNAIL_CAPTURE ou
DIROPEN_PREVIEW_CAPTURE com um destino local antes da suíte correspondente.
DIROPEN_COMPARISON_CAPTURE captura a comparação com endereços ilustrativos.
DIROPEN_BATCH_CAPTURE e DIROPEN_MEDIA_CAPTURE capturam
as novas interfaces usando apenas dados demonstrativos.

A suíte de mídia gera WAV PCM e AVI/MJPEG próprios e exercita o backend real do
Qt, incluindo quadros decodificados. Não usa vídeo baixado ou ffmpeg.exe. O
workflow instala o módulo qtmultimedia no runner; localmente, se faltar o módulo,
use o MaintenanceTool da instalação Qt e adicione Qt Multimedia da mesma versão
e kit MSVC x64. build.cmd não o instala. windeployqt distribui as bibliotecas e
plugins de mídia correspondentes. Não há garantia de todo codec ou dispositivo
de áudio/GPU a partir de um único ambiente.
CI não substitui testes interativos nem pacote numa máquina sem ferramentas de
desenvolvimento. Um aviso OLE foi observado na limpeza do clipboard em testes
nativos anteriores, sem falha; não considerar sua causa ou correção estabelecidas.

## Executar sem usar a sessão pessoal

~~~powershell
& .\build\DirOpen.exe --no-session
# Ou um registro separado em caminho completo de sua escolha:
& .\build\DirOpen.exe --session-file 'C:\Exemplos\sessao-de-teste.json'
~~~

A sessão normal continua em %LOCALAPPDATA%\Navega\session.json para preservar
usuários do nome anterior. Não copiar esse registro para o Git.
