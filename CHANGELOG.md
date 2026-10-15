# Histórico do Directory Open

## 0.12.0 — histórico de renomeações e miniaturas de documentos/mídia

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
