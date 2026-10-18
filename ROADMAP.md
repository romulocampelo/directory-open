# Roadmap do Directory Open

Revisado em **18/10/2026**, considerando as entregas, o planejamento anterior e
os recursos do Directory Opus apresentados pelo usuário. Não há prazos assumidos
nem promessa de paridade. As funcionalidades terão implementação própria ou
integrações documentadas com ferramentas de terceiros.

## Direção estratégica: DirOpen como orquestrador

O DirOpen concentra navegação, seleção, comandos e acompanhamento em uma interface
única. Para cada capacidade, priorizar a integração da ferramenta especializada
mais adequada disponível, considerando qualidade, integridade, desempenho medido,
licença e compatibilidade. Implementação própria continua apropriada quando
oferecer uma solução melhor para o fluxo ou uma alternativa necessária.

Everything tem o primeiro incremento integrado na 0.15; TeraCopy tem cópia
delegada na 0.16 e segue prioritário para evoluir transferências; 7-Zip integra
ZIP/7z na 0.17; restic, rclone e ripgrep na 0.18, por solicitação do usuário;
o duplicador Python existente tem acesso explícito pela interface na 0.21;
a migração do motor permanece posterior.
Novas capacidades devem começar
pela avaliação das ferramentas existentes antes de desenvolver outro motor.

Diretiva reafirmada pelo usuário: seguir a composição de interfaces e ferramentas
especializadas, como nos ambientes de desktop das distribuições Linux. A interface
do DirOpen deve permanecer coerente quando um motor for acrescentado ou trocado.
O núcleo coordena tarefas, seleção, destinos e resultados; adaptadores conectam
as ferramentas. Evoluir a estrutura a partir de cada integração real.

7-Zip e a fila própria de transferências têm incrementos entregues. **ExifTool**,
**ImageMagick** e **FFmpeg/ffprobe** tiveram o primeiro
incremento integrado na 0.19. **restic**, **rclone** e **ripgrep** entraram na 0.18
(tabela abaixo). As próximas avaliações acompanham as etapas
funcionais correspondentes; não antecipam a implementação nem alteram a baixa
prioridade de duplicatas. **A solicitação mais recente do usuário promoveu
interfaces e personalização para a etapa atual**, com primeiro incremento na 0.20.
O visualizador existente
continua a ser aproveitado.

A experiência deve ser transparente: quando a interface permitir, comandos,
progresso e resultados ficam no DirOpen, sem exigir alternância manual entre
programas. O motor utilizado e os detalhes técnicos podem aparecer sob demanda.
Limitações, erros e delegações que exigem janela externa devem ficar claros;
não simular progresso, cancelamento ou conclusão que a ferramenta não confirme.
Detectar disponibilidade e compatibilidade; respeitar a escolha do usuário e
oferecer uma alternativa adequada quando necessário. Instalação/reconfiguração
de terceiros constitui uma ação separada, não um efeito implícito da busca/cópia.

**Legenda:** ✅ entregue no código local; ◐ parte entregue; ☐ pendente.
Entrega local não significa pacote público validado. A base local é **0.24.0 estabilizada localmente**;
a versão publicada registrada no contexto é **0.11.0**. As validações anteriores
estão registradas no contexto e no histórico; 0.16/0.17/0.18 passaram por testes locais,
incluindo TeraCopy, 7-Zip, restic, rclone e ripgrep reais com dados de demonstração.

## Base já entregue

| Recurso | Estado | Escopo e limites atuais |
|---|---|---|
| Um ou dois painéis, múltiplas abas, histórico e favoritos | ✅ | Navegação independente; árvore compartilhada ou por painel, abas bloqueadas/duplicadas/ordenáveis e grupos salvos na 0.21. |
| Abertura pelo programa padrão do Windows | ✅ | Duplo clique em arquivos, botão Abrir e Ctrl+O compartilham a abertura externa; pastas navegam. Funciona nas listas e miniaturas dos dois painéis. |
| Filtro e ordenação de pastas/arquivos | ✅ | Filtro por nome e combinado por padrões/bytes/datas/tipos/ocultos; agrupamento por extensão, mês e tamanho em detalhes/nomes. |
| Copiar, mover, renomear, criar pasta e Lixeira | ✅ | Motor nativo do Windows, confirmação, conflitos, progresso, cancelamento e resultados por item. Clipboard e arraste integrados. Cópia também pode ser delegada ao TeraCopy (0.16), com acompanhamento na janela externa. Fila própria sequencial desde a 0.21. |
| Busca por nome/caminho, direta e com Everything | ✅ incremento | 0.22: filtros compartilhados de tamanho/data/tipo/ocultos, caminho completo e instância nomeada já aberta. O índice fornece candidatos; metadados atuais decidem os filtros. Windows Search ainda futuro. |
| ZIP/7z e leitura RAR com 7-Zip | ✅ | Criar ZIP/7z; listar, testar e extrair em pasta nova, incluindo RAR em arquivo único. Opções avançadas por abertura explícita do gerenciador 7-Zip; motor externo controla o resultado. |
| Busca de conteúdo com ripgrep | ✅ | 0.18: texto/regex, ocultos e ignorados opcionais, arquivo/linha/trecho e abertura. Limites/cobertura explícitos; sem extrair PDF/Office. |
| Backups versionados com restic | ✅ | 0.18: repositório local criptografado, backup da seleção, catálogo de versões e restauração completa verificada em pasta nova. Senha não persistida; sem retenção/agendamento/repos remotos. |
| Armazenamento remoto com rclone | ✅ | Navegação e cópia com prévia; assistente interativo delegado para configurar/autorizar contas, com backup prévio. Backend local testado; OAuth/serviços reais dependem de verificação específica. |
| Metadados com ExifTool | ✅ | 0.19: consulta de propriedades e edição em lote de 12 campos EXIF/IPTC/XMP, com prévia e gravação verificada em cópias novas. Formatos/campos têm suporte próprio; sem edição direta ou remoção de tags. |
| Conversão de imagens com ImageMagick | ✅ | 0.19: JPEG/PNG/WebP/TIFF, redimensionamento sem ampliar, orientação/rotação, qualidade, prévia visual do primeiro arquivo e saídas novas. Primeira página/quadro; sem RAW/SVG/PDF/animações. |
| Conversão de áudio/vídeo com FFmpeg e ffprobe | ✅ | 0.19: consulta dos fluxos, MP4 H.264/AAC e extração MP3/WAV/FLAC, saída nova e verificação por decodificação. Primeiro vídeo/áudio; HDR e perfis avançados pendentes. |
| Comparação de pastas | ✅ | Desde a 0.10, por tamanho/data ou conteúdo, com subpastas opcionais e cobertura parcial explícita. Sincronização por cópia com prévia desde a 0.21. |
| Renomeação em lote e desfazer | ✅ | Prévia, identidade, conflitos e histórico/desfazer. Receitas de até 16 transformações ordenadas, exemplo e importação/exportação nomeada com backup na 0.21. |
| Visualizador e miniaturas | ✅ | Imagem, texto, PDF, áudio/vídeo; painel, ampliado/tela cheia, preferências e triagem. Texto Office/OpenDocument via 7-Zip/XML e OCR local da prévia pelo Windows na 0.21; sem aparência Office completa. |
| Menu clássico do Windows e extensões instaladas | ✅ | Desde a 0.14, com opção de desativar e retorno ao menu próprio em caso de falha. Comandos de terceiros são externos; menus exclusivos do Explorer não são garantidos. |
| Sessão e preferências básicas | ✅ | Abas, pastas, filtros, layout e exibições por pasta; backups e gravação atômica. Temas claro/escuro existentes. |
| Interfaces e personalização | ✅ incremento | 0.21: inventário de 25 capacidades concluído, incluindo fila, sincronização, scripts e associações. [Controle por item e limites](docs/COBERTURA-INTERFACES.md). |
| C++ nativo, build x64 e trabalho em segundo plano | ✅ | Busca, operações, comparação e decodificação fora da UI; filas/cache limitados onde aplicável. Não estabelece superioridade de desempenho sobre outros produtos. |

## Desenvolvimento pendente, em ordem de prioridade

P0 fecha a distribuição já iniciada. P1 é a próxima sequência funcional. P2 amplia
o gerenciamento diário. P3 reúne recursos posteriores. **A prioridade anterior
P4 para toda customização foi substituída pelo pedido atual de aperfeiçoar as
interfaces.** A etapa UI abaixo avança agora; automação e extensões avançadas
permanecem posteriores. Correções de segurança, integridade e regressões são
transversais e não aguardam recursos novos. Por solicitação do usuário, a
**integração com o Everything passa a ser P1.1, primeira prioridade funcional**.

| Prioridade / ordem | Recurso | Estado | Próxima entrega e critério de conclusão |
|---|---|---|---|
| UI.1 — atual | Preferências e acesso às telas | ✅ incremento | 0.20: seis páginas, central pesquisável, ações existentes compartilhadas e perfis. Cancelar não aplica mudanças pendentes; Aplicar preserva mudanças já confirmadas. |
| UI.2 — atual | Personalização ampliada | ✅ incremento | 0.21: várias barras, menus principais/contextuais, estilos/layouts e componentes, ícones raster, idiomas Qt e preferências do visualizador. |
| UI.3 — inventário | Famílias de telas inventariadas | ✅ 0.21 local | 25 capacidades cobertas no escopo de docs/COBERTURA-INTERFACES.md, incluindo fila, sincronização, scripts e associações. Ampliar formatos/serviços/escopos segue nas prioridades funcionais abaixo. |
| P0.1 | Distribuição inicial | ◐ | Automação/candidato local existem. Completar avisos, licenças e fontes correspondentes do pacote real; atualizar o candidato para a versão escolhida; validar em outro Windows sem Qt/Visual Studio. Só então preparar pré-lançamento, notas e hashes. |
| P0.1a — local | Candidato local verificável da 0.24 | ✅ local | Build separado, fingerprint/kit/manifesto e ZIP rastreáveis; QA nativo em cópia separada sem PATH de desenvolvimento. Inventário técnico e roteiro P0.1b; evidências/limites no relatório local do sprint. Continua local-candidate e publishable=false; não fecha P0.1. |
| P0.1b — posterior | Fechar requisitos para distribuição | ☐ | Resolver avisos/fontes aplicáveis ao payload exato e validar em Windows independente sem Qt/Visual Studio. Publicação dos fontes autorizada em 18/10/2026; binários continuam condicionados aos avisos/fontes e Windows independente. P0.1 permanece parcial até cumprir seus critérios. |
| P1.1 — alta | Busca integrada ao Everything | ✅ incremento 0.22 local | Nome/caminho completo, filtros compartilhados de bytes/datas/padrões/tipos/ocultos e escolha de instância já aberta. Candidatos e resultados têm limites separados; filtros conferem metadados atuais. Busca direta e Everything 1.5.0.1423 reais comparados com fixtures; instância nomeada verificada por protocolo controlado. Outra instância real e Windows Search posteriores. |
| P1.2 | Cópia/movimentação com TeraCopy | ◐ | 0.16: cópia pela CLI oficial, seleção múltipla Unicode, escolha lembrada, manter ambos e estado encaminhado. Windows continua disponível. Progresso/cancelamento/verificação na janela externa; verificação depende das opções dela. Resultado integrado bloqueado pela ausência de contrato terminal confiável; não reabrir sem evidência nova. UNC/outros discos, erros de mídia e movimentação comprovada continuam posteriores. |
| P1.3 | Fila de transferências | ✅ incremento | 0.21: até 32 tarefas confirmadas, ordem/remoção, execução sequencial, pausa das próximas e cancelamento da atual, com revalidação. Erro/parcial/delegação pausa. TeraCopy permanece externo; sem retomada automática após reinício. |
| P1.4 | Duas árvores de pastas e navegação funcional | ✅ | 0.21: árvore por painel/aba, navegação e contexto correspondentes, opção de configuração com custo e sessão preservada. |
| P1.5 | Agrupamento e filtros avançados | ✅ incremento | 0.21: tipo/mês/tamanho e filtros combinados, seleção real, contexto e drop nos cabeçalhos tratados como fundo. |
| P1.6a | Tamanho de pastas sob demanda | ✅ | 0.23 local: segundo plano, cancelamento aguardado, estados explícitos, raízes em união, hard links por caminho, reparse points omitidos e limites. |
| P1.6b | Exportar listagens e imprimir | ✅ | 0.24 local: CSV/TXT e prévia paginada/PDF sobre resultado estável; coleta opt-in limitada, cobertura de cálculo/listagem separada, criação exclusiva e proteção de células. |
| P2.1 | Arquivos compactados | ◐ | ZIP/7z e leitura RAR em arquivo único disponíveis, com extração por fluxos e revalidação. Senhas/volumes/edição por gerenciador 7-Zip explicitamente delegado. Metadados completos e otimização adicional permanecem posteriores. |
| P2.2 | Sincronização e backups | ◐ | 0.21: sincronização por cópia em direção explícita, prévia, conflitos e revalidação, sem exclusões. Backup restic e rclone copy preservados. Agendamento, retenção, seleção interna de versões, repositórios remotos e resolução bidirecional permanecem posteriores. |
| P2.3 | Renomeação avançada | ◐ | 0.21: receitas de 16 etapas com transformações ordenadas, exemplo, importação/exportação e motor atual. Expressões regulares, metadados e gravação de teclas, se necessárias, ficam para incrementos próprios. |
| P2.4 | Etiquetas, estrelas, status, tags e descrições | ✅ incremento | 0.21: catálogo por identidade, editor de seleção e triagem de imagens, sessão/backup/revalidação. Não grava EXIF nem acrescenta colunas à lista; portabilidade entre volumes/equipamentos permanece posterior. |
| P2.5 | Painel de propriedades e edição de metadados | ◐ | ExifTool entregue na 0.19 com propriedades, prévia e edições verificadas em cópias. Ampliar formatos/campos, fusos, listas e integração com propriedades do painel. Edição de originais/remoção só com política de backup e reversão comprovada. |
| P2.6 | Conversão e seleção de imagens | ◐ | Conversão ImageMagick desde a 0.19; triagem/marcação com catálogo na 0.21. Animações, múltiplas páginas, perfis/metadados adicionais e outros formatos permanecem posteriores. |
| P2.7 | Visualizador e painel de utilidades ampliados | ◐ | 0.21: preferências, triagem, texto Office/OpenDocument e OCR da prévia. Mais formatos e apresentação Office completa dependem de integração apropriada. |
| P2.8 | Processamento de áudio e vídeo | ◐ | FFmpeg/ffprobe na 0.19: consulta e perfis MP4/MP3/WAV/FLAC. Ampliar perfis, seleção de faixas, legendas, HDR/tone mapping, aceleração e formatos após validação; preservar visualizador atual. |
| P3.1 | Substituição opcional do Explorer | ✅ incremento | 0.21: Abrir pastas físicas opt-in por usuário, backup, diário de recuperação e reversão; consulta/alteração de aplicativos padrão pela tela oficial do Windows. Shell e restauração testados em registro isolado. Win+E, pastas virtuais e cobertura global adicional continuam fora do escopo. |
| P3.2 | FTP, SFTP e serviços remotos | ◐ | rclone com navegação/cópia e assistente interativo para configuração/autorização. Validar cada serviço real, certificados/chaves e falhas de rede; testes usam somente backend local. |
| P3.3 | Duplicatas por integração | ◐ | 0.21 abre o duplicador Python e copia locais sugeridos. GUI 0.4.1 verificada em catálogo isolado. Troca de resultados e migração do núcleo ficam em baixa prioridade, preservando revisão manual e Lixeira. |
| UI.2 — promovida | Customização de comandos | ✅ incremento | Menus principais/contextuais, barras, globais por execução, lançadores, receitas e scripts com formulários. Incrementos estéticos posteriores continuam por último. |
| P4.2 — promovida no inventário | Scripting e diálogos programáveis | ✅ incremento | 0.21: manifesto JSON tipado, Python instalado, código aprovado imutável, consentimento por execução e cancelamento supervisionado. Permissões completas de usuário, sem sandbox/autorun. APIs adicionais de automação e novos tipos de diálogo ficam posteriores. |
| Transversal | Desempenho, estabilidade e preservação de dados | ◐ contínuo | Manter C++/x64 e trabalho fora da UI; medir tarefas equivalentes antes/depois, tempo, CPU e pico de memória, primeira execução e cache aquecido. Limitar concorrência e testar cancelamento/encerramento; não prometer velocidade pela linguagem ou reputação do motor. |

Na estabilização transversal local da 0.24, os testes de relatórios/tamanho
isolam QSettings antes de QApplication, com limpeza por propriedade e recibos
para término abrupto. A apresentação evita o resumo intermediário e conserva
seleção/rolagem dos detalhes idênticos. Comparação com 0.23/0.24: seis processos
por variante/cenário, 12 tarefas por fluxo, tempo/CPU/pico e etapas de apresentação.
Não foi demonstrado ganho material de velocidade nem isolada a causa da diferença
histórica do diálogo com 8.000 arquivos. Frio real, rede e impressão física seguem
sem medição. Não há cache novo, cálculo automático ou novo enumerador.

## Próxima etapa — P0.1b

O P0.1a prepara o candidato LOCAL verificável da 0.24 com build separado,
fingerprint estável, ZIP/manifesto e QA sobre cópia separada do payload. O relatório
local registra os artefatos exatos e as limitações; não confundir integridade e
QA local com liberação de redistribuição.

P0.1b deve reconciliar avisos/licenças/fontes/instruções do payload real e executar
o [roteiro de Windows independente](docs/VALIDACAO-WINDOWS-INDEPENDENTE.md).
Esse teste ainda não foi executado. P0.1 continua parcial, publishable=false;
a publicação dos fontes foi autorizada em 18/10/2026. Isso não libera os
executáveis com requisitos pendentes. O ambiente escolhido para o teste final é
outro computador Windows, ainda sem resultado. P1.2 continua condicionado
a contrato terminal confiável; TeraCopy permanece Delegated e a fila pausa.

## Decisão de planejamento para busca

O usuário considera o Everything a ferramenta mais eficiente que encontrou para
busca e solicitou prioridade alta. O primeiro incremento foi entregue localmente
na **0.15**: usa **o motor e o índice do Everything em execução**, com resultados
dentro do Directory Open. O adaptador Query2W é próprio, baseado no contrato
público; não incorpora algoritmo interno nem DLL do SDK.

Executável confirmado em 15/10/2026:
`C:\Program Files\Everything\Everything.exe`, com ProductVersion **1.5.0.1423b**.
Em 16/10/2026, comunicação, versão 1.5.0.1423, prontidão e busca foram verificadas
na instância padrão em execução, com fixtures. Isso não comprova que todos os
volumes estejam indexados/atualizados, nem valida o comportamento de outra versão.

Busca fora da UI, cancelamento, limites/paginação, abertura de arquivo e localização
da pasta foram integrados. Automático usa a busca direta quando o motor está
indisponível, não pronto ou incompatível, com motivo explícito; uma consulta vazia
não muda de motor nem prova ausência. Para locais não cobertos pelo índice, a
busca direta pode ser escolhida. Não instalar, iniciar ou reconfigurar Everything
implicitamente. Desempenho deve continuar sendo comparado em tarefas equivalentes,
considerando índice pronto, escopo e quantidade de resultados; a busca direta
pode ser mais rápida em pastas pequenas. Custos da indexação inicial não medidos.

## Decisão de planejamento para cópia

A preferência é **reutilizar o motor do TeraCopy instalado**, inicialmente como
programa externo, usando sua interface pública. Isso permite avaliar a tecnologia
real de cópia e verificação sem pressupor acesso ao seu algoritmo interno. Se no
futuro houver um motor próprio, terá desenho e dependências com origem/licença
documentadas; integração e reimplementação são entregas diferentes.

Inspeção local em 15/10/2026: executável presente em
`C:\Program Files\TeraCopy\TeraCopy.exe`, com `Whatsnew.txt` começando em 4.0.5 e
instruções de integração com outros gerenciadores. A versão não foi confirmada
pela execução: os campos de versão do executável estão vazios e o manifesto do
Shell informa 4.0.0.1. Isso não comprova funcionamento, licença ativa ou ganho de
desempenho. Nenhuma transferência ou alteração de configuração foi feita para
esta revisão inicial. Na 0.16, a cópia foi integrada pela CLI pública, com lista
Unicode, `/RenameCopied /NoClose` e opção Windows. Transferências de demonstração
foram verificadas independentemente por SHA-256, sem alterar opções globais.

A documentação oficial descreve [comandos de cópia/movimentação e listas de
arquivos](https://support.codesector.com/en/articles/8789945-teracopy-command-line),
[verificação após a cópia](https://support.codesector.com/en/articles/8789942-copying-and-verifying-files)
e [opções de hash, buffers e concorrência](https://support.codesector.com/en/articles/9905589-options-tab).
Confirmar cada capacidade na versão instalada. Não tratar encerramento do processo
ou simples abertura da janela como prova de cópia concluída/verificada; quando o
contrato não permitir observar o resultado, mostrar a delegação e a incerteza.

Os testes usam somente fixtures. Comparar o fluxo Windows atual com o
TeraCopy nos mesmos arquivos pequenos/grandes e dispositivos, com verificação
equivalente e custos separados. Não alterar preferências globais do TeraCopy,
instalar serviços, elevar privilégios ou redistribuir seus componentes como parte
da integração inicial. O motor nativo continua disponível na ausência do TeraCopy,
mediante escolha explícita. O primeiro incremento não retorna conclusão por arquivo;
movimentação TeraCopy e seu resultado integrado continuam pendentes. A fila própria
da 0.21 executa tarefas sequenciais e pausa na delegação, sem simular acompanhamento.

Ver [CHANGELOG.md](CHANGELOG.md) para entregas por versão,
[USO.md](docs/USO.md) para limites atuais e
[LANCAMENTOS.md](docs/LANCAMENTOS.md) para distribuição.
