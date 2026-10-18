# Histórico do Directory Open

## 0.24.0 — relatórios e listagens sob demanda (fontes)

- Roteiro para diagnóstico/reinstalação de Qt PDF e separação entre publicação
  de fontes e requisitos ainda pendentes dos executáveis.

- P0.1a: build em pasta explícita, sem reutilizar imagens observadas em uso;
  candidato local com fingerprint antes/depois, kit do build e QA separado.
  Leitura JSON UTF-8 compartilhada no PowerShell 5.1/7, rejeição de ADS/junções
  e falhas positivas/negativas do pipeline. Sem liberação de redistribuição.

- Estabilização transversal: apresentação publica o resumo completo uma vez e
  preserva seleção/rolagem quando os detalhes não mudam. Sem cache de resultados.
- Seletores dos testes isolados antes de QApplication até depois de sua destruição,
  com limpeza estruturada, falhas/cancelamento e recibo de término abrupto.

- CSV/TXT do resultado concluído, com seleção, raízes efetivas, horários,
  cobertura, contadores, omissões, erros e limites; sem releitura do disco.
- Listagem opt-in no mesmo cálculo, limitada a 10.000 registros / 8 MiB
  contabilizados; retenção esgotada não interrompe os totais e possui estado próprio.
- Prévia paginada, PDF novo e impressão explicitamente confirmada; cobertura
  incompleta visível por página e nenhuma impressão física usada nos testes.
- UTF-8, escaping, proteção de fórmulas e criação exclusiva de CSV/TXT/PDF.
  Pastas sem subtotal inventado; sem cálculo automático. TeraCopy/fila preservados.

## 0.23.0 — tamanho de pastas sob demanda (local)

- Calcular tamanho das pastas selecionadas, em segundo plano, com progresso e
  cancelamento aguardado; resultados completo, parcial, cancelado e limitado.
- Tamanho lógico por caminho, hard links por nome e união das raízes sobrepostas;
  reparse points omitidos, erros e alterações detectadas explícitos, sem snapshot.
- Limites de entradas, profundidade, prazo e aritmética; nenhum cálculo automático,
  exportação ou impressão nesta etapa. TeraCopy e pausa da fila preservados.

## 0.22.0 — busca ampliada (em desenvolvimento local)

- Busca por nome ou caminho completo, com interpretação compartilhada entre
  Everything e busca direta, sem executar sintaxe digitada como comandos.
- Filtros de padrões de arquivo, bytes, datas de modificação, arquivos/pastas e
  ocultos reutilizam as regras da navegação; intervalos inclusivos, datas locais.
- Seleção explícita de instância nomeada do Everything já aberta, sem iniciar ou
  reconfigurar o programa; o resultado identifica a instância consultada.
- Paginação separa candidatos examinados de resultados aceitos. Filtros consultam
  metadados atuais, continuam nas páginas seguintes e preservam limites,
  cancelamento e erros parciais. O total do índice representa candidatos.

## 0.21.0 — cobertura funcional do inventário local

- Prévia textual de documentos Office/OpenDocument pelo 7-Zip, sem executar macros;
  OCR da imagem/página exibida com idiomas instalados no Windows.
- Acesso explícito ao duplicador Python e à configuração interativa de contas rclone,
  com backup do arquivo existente antes da delegação.
- Receitas de renomeação ordenadas, exemplo, importação/exportação com backup;
  a aplicação conserva prévia, conflitos, revalidação e histórico existentes.
- RAR em arquivo único, incluindo RAR5 sólido, e abertura do gerenciador externo
  do 7-Zip para opções avançadas. Delegação não comprova conclusão.
- Cabeçalhos de grupos tratados como fundo também no contexto e no arraste;
  codificação inicial respeitada ao abrir outro texto e indicadores de seleção legíveis.

- Árvores independentes por painel, opção com custo explícito e liberação ao
  desativar; menus próprios/Shell seguem o painel correspondente.
- Bloquear, duplicar e ordenar abas; filtros combinados com padrões/tamanho/
  datas/ocultos, e agrupamento em detalhes/nomes. Seleção permanece ligada aos
  arquivos reais, com modelos somente leitura.
- Catálogos de layouts nomeados e grupos de abas, restauração nesta janela ou
  em nova janela com sessão própria. Aparência e fontes pertencem à janela.
- Codecs únicos de painel e exibição compartilhados entre sessão, pastas e layouts.
  Sessão v1 aditiva, backup/concorrência e limites conservados.
- Personalização avançada: barras nomeadas, menus principais/contextuais próprios,
  componentes, ícones raster limitados, lançadores com confirmação e delegação.
- Conjuntos visuais isolados nos layouts, sem substituir pastas abertas. Pacotes de
  idioma Qt na próxima execução; português em falha/omissão, sem tradução completa incluída.
- Atalhos globais opt-in por execução, com registro/conflito/liberação no Windows;
  importação e restauração não ativam comandos nem registros globais.
- Preferências do visualizador com fonte/quebra/codificação e qualidade de imagem,
  compartilhadas na prévia/ampliado e persistidas com a interface.
- Classificações e triagem em catálogo próprio por identidade: etiquetas/cores,
  estrelas/status/tags/descrições, edição preparada e gravação explícita. Marcar
  Descartar não remove arquivos; originais/EXIF intactos, backup e revalidação.
- Fila própria com ordem/remoção, pausa das próximas, cancelamento e revalidação;
  avulsas e fila não concorrem, encaminhar ao TeraCopy pausa sem concluir falsamente.
- Sincronização por cópia em direção explícita, prévia atual, conflitos e
  revalidação antes de aplicar; não propaga exclusões nem repete planos antigos.
- Formulários JSON tipados e scripts Python aprovados por execução, cópia imutável
  do código e cancelamento de família; permissões completas, sem sandbox/autorun.
- Associações por extensão na tela oficial do Windows e Abrir pastas físicas
  opt-in por usuário, com backup, recuperação de interrupção e reversão conferida.
  Validação em registro isolado; sem ativação pessoal, Win+E ou pastas virtuais.
- Codec de caminhos dos motores possui uma tabela única; campos futuros da sessão
  são preservados. Indicadores de seleção legíveis também em árvores/listas.
- As 25 capacidades de docs/COBERTURA-INTERFACES.md possuem implementação no
  escopo documentado. Isso não significa paridade integral com o Directory Opus.

## 0.20.0 — Preferências e central de comandos

- Configurações → Preferências (Ctrl+,): seis páginas para aparência, painéis,
  barras/menus, atalhos, integrações e perfis. Fonte, tamanho, espaçamento, cores,
  ícones, estilo dos botões, posição da prévia e arranjos iniciais.
- Barra de comandos editável e menu pessoal Meus comandos, reutilizando ações
  existentes com as mesmas condições e confirmações. Opção de mover/destacar barra.
- Central de comandos pesquisável (Ctrl+Shift+P), com acesso às telas existentes
  e às exibições da pasta ativa; ações indisponíveis permanecem desabilitadas.
- Atalhos locais editáveis com detecção de colisão/prefixos, combinações de edição
  reservadas e acesso fixo à recuperação. Aplicar/OK/Cancelar têm efeitos explícitos.
- Sessão v1 aditiva, legado preservado, perfis de interface versionados até 64 KiB,
  importação para revisão e exportação atômica com backup/idempotência.
- Inventário em docs/INTERFACES.md distingue o que funciona das famílias ainda
  pendentes. Não há paridade completa com o Opus: múltiplas barras nomeadas,
  edição dos menus principais/contextuais, atalhos globais, layouts nomeados,
  centenas de cores por componente e scripting continuam futuros.

## 0.19.0 — ExifTool, ImageMagick e FFmpeg/ffprobe

- Ferramentas → Metadados: propriedades e prévia antes/depois de campos EXIF,
  IPTC e XMP, com datas, autor, título, descrição e palavras-chave. Lotes de até
  200 arquivos; grava cópias em pasta nova e relê os campos para confirmar valores.
  Transporte UTF-8 próprio para nomes/valores no Windows, sem carregar config Perl.
- Ferramentas → Converter imagens: JPEG/PNG/WebP/TIFF, dimensões sem ampliar,
  orientação EXIF, rotações de 90/180/270°, qualidade e prévia visual do primeiro
  arquivo. Apenas primeira página/quadro; JPEG usa branco para transparência.
- Ferramentas → Áudio e vídeo: ffprobe consulta formato, duração, taxas e fluxos;
  FFmpeg converte para MP4 H.264/AAC ou extrai primeiro áudio para MP3/WAV/FLAC.
  Verifica catálogo e decodificação completa antes de publicar. HDR não suportado.
- Originais preservados, origem revalidada sob bloqueio, nomes conflitantes
  recusados, temporários próprios e publicação sem substituição. Marca de origem
  preservada; cancelamento mantém concluídos e relata limpeza pendente se ocorrer.
- Supervisão compartilhada de famílias de processos em externalprocess; parsers
  continuam nos adaptadores. Sessão v1 guarda quatro caminhos adicionais, sem
  alterar configurações globais, baixar pelo build ou redistribuir ferramentas.
- Edição direta dos originais, remoção/limpeza de tags, fusos/listas avançadas,
  RAW/SVG/PDF, animações/múltiplas páginas, perfis avançados, legendas/múltiplas
  faixas e preservação integral de metadados/ACLs permanecem pendentes.

## 0.18.0 — restic, rclone e ripgrep

- Ferramentas → Buscar conteúdo (Ctrl+Alt+F): ripgrep com resultados por arquivo,
  linha e trecho, expressão regular opcional, opções de ocultos/ignorados e abertura
  pelo programa padrão. PDF/Office sem extração; binários e arquivos >64 MiB têm
  cobertura limitada. Limites e erros não são apresentados como ausência.
- Ferramentas → Backups com versões: inicializar repositório local criptografado,
  fazer backup da seleção, listar versões e restaurar uma versão em pasta nova,
  com verificação restic. Origens de uma mesma pasta, gravadas com raízes relativas;
  senha em memória, sem gravação na sessão, argumentos ou registros do DirOpen.
- Ferramentas → Armazenamento remoto: rclone.conf existente, conexões, navegação,
  prévia de envio/recebimento e cópia confirmada do conteúdo de pastas. Arquivos
  diferentes existentes causam erro; não propaga exclusões. Autorização das contas
  continua no rclone; o arquivo de configuração pode ter seus tokens renovados.
- Processos em segundo plano com supervisão da família desde a criação, resultados
  limitados e cancelamento aguardado. Preferências não secretas aditivas na sessão.
  Sem instalação global/redistribuição automática nem mudanças no build.cmd.
- Remotos reais, OAuth dentro do DirOpen, agendamento, retenção/prune, sincronização
  com exclusões, seleção de arquivos internos de versões e metadados completos
  continuam pendentes. Testes de transferência usam o backend local real do rclone.

## 0.17.0 — compactados com 7-Zip

- Criar ZIP/7z, listar, testar integridade e extrair no DirOpen, usando o 7-Zip
  instalado. Comandos em Ferramentas e no menu de contexto; duplo clique mantém
  a abertura pelo programa padrão. Nenhum componente 7-Zip é redistribuído.
- Criação em área temporária, teste de integridade e publicação com nome novo.
  Extração para pasta nova, sem atualizar compactados nem mesclar destinos.
- Extração por fluxos de cada arquivo: o 7-Zip fornece dados e o DirOpen possui
  os caminhos gravados. Recusa travessia, nomes absolutos/reservados, ADS, links,
  colisões e listagens ambíguas; mantém handles de diretórios normais durante a
  escrita e preserva a marca de origem do compactado nos arquivos extraídos.
- Processo supervisionado e cancelável; CRC/código/tamanho conferidos antes de
  publicar. Cancelar mantém arquivos concluídos e distingue resultado parcial.
  Identidade do compactado revalidada desde a listagem, com bloqueio de leitura.
- Primeira matriz verificada com 7-Zip 26.04 no Windows: ZIP e 7z normais.
  Senhas, volumes divididos, RAR, edição interna e metadados completos pendentes.
  Muitos arquivos e blocos sólidos podem aumentar o custo da extração por arquivo.

## 0.16.0 — cópia com TeraCopy

- **Copiar com: Windows / TeraCopy** na confirmação de cópia, incluindo colar
  e arrastar. A escolha é lembrada na sessão e pode ser alterada em Configurações.
- Adaptador da interface pública do TeraCopy instalado, sem redistribuir seus
  componentes ou modificar suas preferências. Seleção múltipla em lista Unicode;
  conflitos usam `/RenameCopied` para preservar arquivos existentes.
- Estado próprio **encaminhado**, sem contabilizar arquivos como concluídos.
  Progresso, cancelamento após envio e verificação pertencem à janela do TeraCopy.
  Listas e registros locais sobrevivem ao encerramento do DirOpen/encaminhamento
  para outra instância. Não há repetição automática com Windows após uma falha.
- Movimentação continua no motor Windows; fila própria e confirmação integrada
  do resultado externo permanecem no roadmap.

## 0.15.0 — busca integrada ao Everything

- A busca do DirOpen prefere o Everything em execução e apresenta os resultados
  na mesma janela. Métodos Everything e busca direta também podem ser escolhidos.
  O modo automático recorre à busca direta apenas na indisponibilidade, índice
  ainda não pronto ou versão incompatível, com o motivo visível.
- Integração local Query2W sem DLL do SDK, serviço instalado ou mudança de
  configurações do Everything. Confere versão/prontidão e limita consulta,
  paginação, prazo, memória e encerramento do trabalhador.
- Nome literal e padrões continuam compartilhando a interpretação do DirOpen.
  Escopo, identidade da pasta, links/junções e disponibilidade dos resultados são
  conferidos; falhas e mudanças durante a paginação conservam os fatos recebidos.
- Resultados do índice têm cobertura explícita: zero correspondências não prova
  ausência na pasta. Duplo clique/Abrir abre arquivos no programa padrão;
  Abrir pasta em nova aba mantém a localização nos painéis.
- Primeira verificação real com Everything 1.5.0.1423 no Windows; protocolo 1.4.1
  previsto pelo contrato, ainda sem validação real dessa versão ou de outro Windows.

## 0.14.0 — menu de contexto nativo do Windows

- Duplo clique em arquivos abre o programa padrão do Windows nas listas e
  miniaturas, nos dois painéis. Pastas continuam navegando; seleção simples e
  F7/F3/F11 conservam o visualizador integrado. Botão Abrir/Ctrl+O compartilham
  o mesmo fluxo de abertura, liberando a reprodução interna antes de delegar.
- Seleção por mouse nas miniaturas passa a selecionar a linha completa do modelo
  compartilhado, preservando a seleção para prévia, abertura e operações.
- Configurações → Menu de contexto do Windows (Consome mais CPU) permite alternar
  imediatamente entre o menu nativo e o simples, nos painéis e árvore. Escolha
  persistida na sessão, com backup e compatibilidade com sessões anteriores.
- Botão direito em arquivos, seleção múltipla, fundo da pasta e árvore lateral
  usa o menu clássico do Shell, com extensões registradas no computador.
- Submenus dinâmicos/itens desenhados pelas extensões recebem as mensagens nativas;
  COM, PIDLs, menus e conexão com a janela têm um único proprietário com limpeza.
- Comandos próprios no submenu Directory Open. Verbos padrão de abrir, clipboard,
  renomear, Lixeira e criar pasta usam as ações existentes e suas confirmações.
  Comandos de terceiros são explicitamente delegados e não entram no histórico.
- Falha ao obter o menu preserva o menu próprio. Sem alterações do registro ou de
  Win+E. Extensões exclusivas do menu compacto/serviços do Explorer não têm garantia.

## 0.13.0 — pesquisa e seleção de texto em PDF

- Busca anterior/próxima entre páginas, sem diferenciar maiúsculas, com destaque
  e navegação até a ocorrência. Ctrl+F procura no visualizador quando ele tem foco;
  fora dele, conserva o filtro do painel.
- Arraste sobre o texto para selecionar; Ctrl+A seleciona o texto da página e
  Ctrl+C ou Copiar seleção copia como texto simples. Funciona no painel e ampliado.
- Qt PDF substitui Windows.Data.Pdf no proprietário compartilhado de renderização,
  texto e coordenadas. Miniaturas e prévia usam o mesmo componente; decodificação,
  busca e seleção continuam no trabalhador, em uma cópia imutável do documento.
- Busca limitada por solicitação e páginas densas/limites mostram cobertura parcial.
  Sem OCR, formulários, links interativos ou entrada de senha. Qt PDF da mesma
  versão/kit passa a ser requisito explícito de build e distribuição.

## 0.12.0 — histórico de renomeações e miniaturas de documentos/mídia

- Preparação local de pacote Windows em pasta isolada e ZIP, com hashes, origem
  das dependências, revisão/fingerprint dos fontes e SPDX do kit Qt quando disponível.
  Verificação de integridade e abertura sem caminhos de desenvolvimento, usando
  dados próprios e sem sessão pessoal. Candidato ainda pendente de revisão completa
  das licenças/fontes e teste em outro Windows; sem publicação automática.

- Histórico persistente dos novos lotes, com mapa gravado antes de alterar nomes,
  gravação atômica, backup e proteção contra duas janelas escrevendo ao mesmo tempo.
- Ferramentas → Histórico de renomeações (Ctrl+Shift+R): prévia, escolha de itens e
  confirmação para desfazer. Revalidação de identidade, tamanho/datas e nomes livres;
  conflitos, alterações e incertezas ficam bloqueados, sem substituir destinos.
- Cancelamentos e interrupções conservam o mapa; uma nova prévia identifica o que
  já voltou ao nome original e o que ainda pode ser desfeito. Modo sem sessão não grava histórico.
- Miniaturas da primeira página de PDF e do primeiro quadro válido de vídeo,
  sem áudio, na fila/cache compartilhados. Formato/codec inválido mostra o motivo.
- Renderizador PDF compartilhado entre visualizador e miniaturas, com cache por
  conteúdo, limites e escala do Windows tratados no mesmo componente.

## 0.11.0 — renomeação em lote e áudio/vídeo

- Prévia de renomeação (Ctrl+F2) com modelo, numeração, substituição literal e datas
  de modificação/criação/escolhidas; preservação opcional da última extensão.
- Ordem do lote acompanha a apresentação; exclusão de linhas, confirmação explícita,
  conflitos/nomes inválidos bloqueantes e revalidação de identidade antes de aplicar.
- Motor nativo sem substituir destinos, trabalho em segundo plano, cancelamento
  parcial e resultado por item; sem desfazer automático ou resolução de ciclos.
- Áudio/vídeo no mesmo visualizador F7/F3/F11: reproduzir/pausar, parar, posição,
  volume e silêncio. Sem autoplay; Anterior/Próximo conserva a navegação do painel.
- Backend/dispositivo liberados ao trocar/ocultar/fechar e antes de mudar a origem.
  Qt Multimedia é requisito de build; formatos/codecs dependem do runtime distribuído.


## 0.10.0 — comparação de pastas

- Comparar as pastas dos dois painéis por tamanho/data ou por bytes, com inclusão
  opcional de subpastas, itens exclusivos, tipos diferentes e resultados por caminho.
- Distinguir coincidência de metadados de conteúdo conferido; pastas correspondentes
  não estabelecem igualdade automática de toda a árvore.
- Filtrar diferenças e pendências; abrir cada resultado em nova aba do painel escolhido.
- Trabalhador com cancelamento, limites explícitos e encerramento aguardado. Erros,
  arquivos em uso, links/junções e cobertura parcial não confirmam igualdade ou ausência.
- Corrigir caminhos longos da busca no limite Win32, sem depender da política global
  do Windows; representação compartilhada com a comparação.


As versões abaixo registram entregas anteriores, inicialmente validadas localmente.
O código foi publicado posteriormente no GitHub; o pacote para outro computador continua pendente.

## 0.9.0 — 14/10/2026

- Grade de miniaturas de imagens, com tamanhos de 96, 144, 192 e 256 px.
- Modo e tamanho guardados por pasta, compartilhados entre os dois painéis.
- Seleção, filtro, ordenação, visualizador e operações existentes disponíveis na grade.
- Carregamento apenas de imagens visíveis, em um trabalhador, com fila substituível
  e cache em memória compartilhado e limitado; F5 permite invalidar o cache.
- Leitura de imagens e orientação EXIF compartilhadas com o visualizador.
- Pintura explícita da seleção para manter o destaque nos temas nativos do Windows.

## 0.8.0 — 14/10/2026

- Nome Directory Open e executável DirOpen.exe, preservando atalhos e sessão legados.
- Visualizador de imagens, texto e PDF em segundo plano, no painel ou janela ampliada.
- Zoom/rotação de imagens, pesquisa/codificação de texto e navegação de páginas PDF.
- F3 para ampliar, F7 para o painel e F11 para tela cheia.

## 0.7.0 — 14/10/2026

- Preferências de exibição por pasta: colunas, larguras, ordem e ordenação.
- Catálogo compartilhado entre painéis, com organizador e persistência na sessão.

## Recursos anteriores

Dois painéis, árvore, abas, histórico, filtro, temas, operações nativas de arquivos,
Lixeira, área de transferência, arraste interno, sessão, busca por nome e favoritos.
