# Arquitetura

Aplicativo próprio em C++20/Qt Widgets para Windows, sem dependência de REA ou
Directory Opus. CMake produz DirOpen.exe e uma biblioteca interna de UI usada
pelo aplicativo e pelos testes; não representa um plugin ou SDK público.

## Responsabilidades

| Área | Proprietário |
|---|---|
| Composição, ações e janela ampliada | mainwindow |
| Modelo somente leitura, navegação e seleção compartilhada | filepanel |
| Política de arraste nas duas apresentações | filelistview / FileTransferView |
| Identidade e formatos da área de transferência | filetransfer |
| Operação nativa e resultado por item | fileoperation |
| Confirmação, destino e conflitos | operationdialog |
| Registro, backups, recuperação e concorrência da sessão | sessionstore |
| Fatos, codec e catálogo das exibições por pasta | folderview |
| Trabalhador, codec de texto e cache de PDF | previewloader |
| Controles e pintura limitada ao viewport | previewwidget |
| Dimensões, escala e orientação EXIF compartilhadas | imagepreview |
| Trabalhador, frescor e orçamento de miniaturas | thumbnailcache |
| Demanda do viewport e pintura/seleção das células | thumbnailview |
| Busca por nome e sua interface | filesearch / searchdialog |
| Organizadores de favoritos e exibições | favoritesdialog / folderviewsdialog |

## Contratos essenciais

- Modelos, seleção, filtros e arraste não alteram arquivos. Operações confirmadas
  usam o motor do Windows. Lixeira usa ITransferSource::RecycleItem; não acrescentar
  exclusão definitiva como alternativa automática.
- Cancelar preserva trabalhos já concluídos e distingue itens pendentes/parciais.
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

## Testes e evidência

Teste pelo fluxo público da UI e pelos limites produtores quando o fluxo público
não reproduz uma semântica distinta. Use arquivos temporários e sessões isoladas.
Distinguir testes offscreen de clipboard/atalhos nativos e instalação em outro PC.
Aguardar exposição/ativação e usar o filho focado ao simular teclas nativas.
Veja [COMPILAR.md](COMPILAR.md) para execução e omissões.
