# P0.1b - roteiro para Windows independente

Este roteiro ainda não foi executado. A verificação local sem PATH de
desenvolvimento não prova outro computador. Não iniciar/reconfigurar VM ou
instalar software como consequência deste documento. O ambiente independente
foi escolhido em 18/10/2026: outro computador Windows disponível. A execução
nesse computador e os resultados da matriz continuam pendentes; a escolha não
constitui teste aprovado.

## Preparação e registro

1. Resolver os avisos/textos/fontes/instruções aplicáveis ao payload exato antes
   de autorizar distribuição. O candidato P0.1a mantém publishable=false.
2. Registrar versão/arquitetura/build do Windows e origem do ambiente. Usar
   uma conta/pasta de teste, sem sessões/preferências pessoais do desenvolvedor.
3. Registrar hash do ZIP e do manifesto de uma origem confiável, CRC e extração
   integral em pasta nova com espaços/acentos. Não executar dentro do ZIP.
4. Registrar existência/versão do Visual C++ Redistributable x64. A ausência e
   presença do requisito são cenários distintos; qualquer instalação exige
   decisão própria, origem Microsoft e observância de seus termos.
5. Registrar somente dados necessários do ambiente: Windows, requisito Microsoft,
   argumentos e PATH restrito escolhido. Não exportar o ambiente herdado completo.

## Matriz com fixtures próprias

| Cenário | Conferência exigida |
|---|---|
| Inicialização | Janela exposta, versão, argumentos Unicode/espaços e duas pastas; --no-session. Módulos Qt/plugins/FFmpeg dentro do pacote. |
| Sessão própria | --session-file exclusivo; reinício preserva abas, preferências e argumentos; sessão pessoal não é usada. |
| Navegação/imagens | Duas pastas, seleção/filtros, miniaturas de imagem, Unicode, erro/missing e origem preservada. |
| Tamanho/relatório | Invocação explícita, cancelar/fechar aguardados; completo/parcial/cancelado/limitado; união de raízes e reparse points; sem cálculo automático. |
| CSV/TXT/PDF | Resultado terminal estável, inclusive origem removida; fórmulas/Unicode/controles; arquivo existente recusado; cobertura e paginação em todas as páginas; prévia visível. Não imprimir fisicamente. |
| PDF/prévia | PDF de fixture com mais de uma página, navegação/zoom, conteúdo não vazio, erro em corrompido e arquivo liberado ao fechar. |
| Áudio/vídeo | WAV e AVI próprios, carregar sem autoplay, reproduzir/pausar/parar/seek, decodificar frame real e liberar arquivo; erro em corrompido. Não inferir cobertura de todos os codecs. |
| Comparação/lote | Prévia e confirmação com fixtures; conflitos, cancelamento, histórico/revalidação e destinos novos. |
| Copiar/mover/Lixeira | Somente fixtures; confirmar bytes/hashes, conflito, cancelamento e resíduos. Sem exclusão definitiva como alternativa à Lixeira. |
| Foco/F11/encerramento | Retorno do visualizador, fullscreen, fechar com tarefa ativa e liberação dos recursos. |
| Integrações externas | Ausência deve ter motivo útil. TeraCopy permanece Delegated e a fila pausa; sem remoto/OAuth real nem instalação/configuração implícita. |

Seletores automatizados só podem ser usados com isolamento antes de QApplication
até depois da destruição, com UUID/nonce/handles/recibo próprios. Comparar hashes
de preferências e sessões antes/depois; relatar mudanças sem restaurar alterações
concorrentes. Não remover chaves por prefixo nem controlar PIDs históricos.

## Critério de registro

Para cada cenário: versão/hash do payload, fixture, resultado esperado/observado,
artefatos e falhas/resíduos. Medir primeira execução e ao menos três processos
por cenário representativo; distinguir aquecido de frio real, tempo/CPU/pico e
família não medida. Não comparar o candidato antigo 0.12 como baseline da 0.24.

Ausência do ambiente independente não é aprovação. Nenhum resultado deste roteiro
autoriza publicação automática. P0.1 continua parcial até resolver as pendências
do pacote real e concluir esta matriz; commit/push/publicação exigem instrução própria.
