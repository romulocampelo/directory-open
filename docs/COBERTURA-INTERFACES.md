# Controle de cobertura do inventário

Inventário funcional coberto na 0.21 local, em 17/10/2026: 25 capacidades.
Cada item exige interface funcional, persistência quando
aplicável e verificação; uma página vazia não conta como entrega. A cobertura
das famílias não equivale a compatibilidade irrestrita com todos os formatos,
serviços, extensões ou versões de outros produtos.

| ID | Capacidade do inventário | Estado atual |
|---|---|---|
| N1 | Árvores independentes | ✅ 0.21; 23 suítes e teste nativo |
| N2 | Bloquear, duplicar e ordenar abas | ✅ 0.21; testes nativos passaram |
| N3 | Filtros combinados e editor | ✅ 0.21: padrões, tamanho, datas, arquivos/pastas e ocultos; testes nativos passaram |
| N4 | Agrupamento por tipo, mês e tamanho | ✅ 0.21 em detalhes/nomes, com cabeçalhos e seleção real; testes nativos passaram |
| N5 | Grupos salvos de abas | ✅ 0.21; catálogo com codec comum, persistência e restauração validados |
| L1 | Layouts nomeados, restauração e várias janelas | ✅ 0.21; teste nativo salva, reabre e restaura; aparência pertence à janela |
| A1 | Paletas por componente e ícones próprios | ✅ 0.21; cores/tamanho por seis componentes e ícones PNG/ICO/JPEG limitados, com padrão em erro |
| A2 | Idioma e animações | ✅ 0.21; animações opcionais e pacote Qt .qm na próxima execução, com retorno ao português; sem pacote completo de outro idioma incluído |
| B1 | Várias barras nomeadas e conjuntos | ✅ 0.21; até 16 barras nomeadas, posição/movimento e conjuntos nos layouts; aplicação visual isolada |
| B2 | Lançadores e comandos de usuário | ✅ 0.21; até 32 lançadores com argumentos separados, prévia/confirmacão e delegação explícita; importação nunca executa |
| M1 | Editor dos menus principais | ✅ 0.21; sete menus principais editáveis e recuperação preservada em Configurações |
| M2 | Editor dos menus contextuais próprios | ✅ 0.21; menus próprios de arquivos/árvore/abas editáveis, vinculados à seleção/pasta/aba correspondente |
| T1 | Atalhos globais | ✅ 0.21; até 32 globais por janela, ativação explícita nesta execução, conflitos e liberação nativos verificados |
| S1 | Scripting, permissões, cancelamento e formulários | ✅ 0.21; formulários JSON tipados, Python com código aprovado imutável, consentimento por execução e cancelamento supervisionado; permissões completas de usuário, sem sandbox nem autorun |
| V1 | Preferências específicas do visualizador | ✅ 0.21; fonte/tamanho, quebra, codificação inicial, ajuste/suavização e três qualidades de prévia; sessão/perfil e decodificador verificados |
| V2 | Triagem/marcação de imagens | ✅ 0.21; prévia, navegação, marcas Manter/Revisar/Descartar, avanço e gravação explícita do catálogo; nenhuma exclusão |
| V3 | Documentos Office e OCR por integração apropriada | ✅ 0.21; texto de DOCX/XLSX/PPTX/ODT/ODS/ODP via 7-Zip/XML e OCR local da prévia via Windows; sem macros, paginação Office ou formatos antigos |
| O1 | Fila própria de operações | ✅ 0.21; até 32 tarefas, ordem/remoção, execução sequencial, pausa das próximas, cancelamento da atual e revalidação; TeraCopy delegado pausa a fila sem presumir conclusão |
| O2 | Sincronização com prévia, conflitos e revalidação | ✅ 0.21; comparação tipada, direção explícita, prévia atual, conflitos manter ambos/perguntar e revalidação antes de copiar; nenhuma exclusão propagada |
| O3 | Opções avançadas de renomeação/macros | ✅ 0.21; receitas nomeadas de até 16 etapas, ordem/edição, exemplo, importação/exportação com backup e prévia/revalidação pelo motor atual |
| E1 | Etiquetas, estrelas, status, tags e descrições persistentes | ✅ 0.21; catálogo por identidade, etiquetas/cores/estrelas/status/tags/descrições, edição em seleção e persistência com revalidação/backup |
| R1 | Assistente de configuração/autorização de contas | ✅ 0.21; assistente interativo delegado ao rclone, backup antes de abrir configuração existente e retorno à listagem; teste com conta local fictícia, sem certificação de OAuth por serviço |
| Z1 | RAR e opções avançadas de compactados | ✅ 0.21; leitura/teste/extração de RAR em arquivo único, inclusive RAR5 sólido, e abertura explícita do gerenciador 7-Zip para opções avançadas; criar RAR não é suportado |
| F1 | Associações e substituição opcional do Explorer | ✅ 0.21 no escopo de pastas físicas: consulta de associações e acesso à escolha oficial do Windows; comando Abrir opt-in por usuário, backup e recuperação de interrupções. Shell e reversão verificados em registro isolado; Win+E e pastas virtuais fora do escopo |
| D1 | Acesso ao duplicador Python especializado | ✅ 0.21; abertura explícita da instalação Python configurada e cópia de locais sugeridos; GUI 0.4.1 verificada com catálogo isolado; migração do motor continua posterior |

Os limites anteriores de integridade continuam: sem exclusão definitiva como
alternativa à Lixeira; nenhuma configuração do sistema é alterada implicitamente.
Integrações delegadas precisam identificar que a execução e a conclusão estão
sob controle da ferramenta externa. Credenciais reais e mudanças de associações
são escolhas explícitas na interface, não parte dos testes com fixtures.
