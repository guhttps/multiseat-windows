# Roadmap

Marcos sequenciais; estimativas de prazo só após M1. Completar um marco exige evidência, não apenas código.

Por solicitação do responsável pelo projeto, o painel e a distribuição de um protótipo foram antecipados. Essa entrega permite baixar, instalar e organizar um plano de duas estações; não encerra M1–M6 nem substitui a prova do motor e do isolamento de aplicativos externos.

| Marco | Entrega | Gate |
|---|---|---|
| M0 | Laboratório e base | build e diagnóstico repetíveis |
| M1 | Viabilidade | decisão arquitetural documentada com provas |
| M2 | Inventário | dispositivos identificáveis e persistência |
| M3 | Protótipo simultâneo | duas estações com input e display independentes |
| M4 | Orquestração | lifecycle, IPC e recuperação |
| M5 | Driver se necessário | necessidade provada, assinatura e rollback |
| M6 | Painel e instalador | configuração e instalação reversíveis |
| M7 | Beta | matriz de compatibilidade e testes prolongados |
| M8 | Release | distribuição, atualização e suporte |

## Backlog em ordem

| ID | Marco | Tarefa | Dependência |
|---|---|---|---|
| [T001](docs/backlog/T001.md) | M0 | Preparar laboratório | Nenhuma |
| [T002](docs/backlog/T002.md) | M0 | Executar diagnóstico Win32 | T001 |
| [T003](docs/backlog/T003.md) | M0 | Publicar base e CI | T002 |
| [T004](docs/backlog/T004.md) | M1 | Especificar produto | T003 |
| [T005](docs/backlog/T005.md) | M1 | Experimentar desktops | T004 |
| [T006](docs/backlog/T006.md) | M1 | Comparar estratégias de isolamento | T005 |
| [T007](docs/backlog/T007.md) | M1 | Aprovar arquitetura | T006 |
| [T008](docs/backlog/T008.md) | M2 | Inventariar entrada | T007 |
| [T009](docs/backlog/T009.md) | M2 | Inventariar vídeo | T008 |
| [T010](docs/backlog/T010.md) | M2 | Inventariar áudio | T009 |
| [T011](docs/backlog/T011.md) | M2 | Persistir configuração | T010 |
| [T012](docs/backlog/T012.md) | M3 | Demonstrar display simultâneo | T011 |
| [T013](docs/backlog/T013.md) | M3 | Demonstrar isolamento de entrada | T012 |
| [T014](docs/backlog/T014.md) | M3 | Demonstrar contas e áudio | T013 |
| [T015](docs/backlog/T015.md) | M3 | Validar protótipo integrado | T014 |
| [T016](docs/backlog/T016.md) | M4 | Projetar IPC e agentes | T015 |
| [T017](docs/backlog/T017.md) | M4 | Implementar orquestrador | T016 |
| [T018](docs/backlog/T018.md) | M4 | Tratar lifecycle | T017 |
| [T019](docs/backlog/T019.md) | M5 | Decidir necessidade de driver | T018 |
| [T020](docs/backlog/T020.md) | M5 | Prototipar driver em laboratório | T019 |
| [T021](docs/backlog/T021.md) | M5 | Preparar driver para distribuição | T020 |
| [T022](docs/backlog/T022.md) | M6 | Criar painel de estações | T021 |
| [T023](docs/backlog/T023.md) | M6 | Criar instalador | T022 |
| [T024](docs/backlog/T024.md) | M7 | Testar compatibilidade | T023 |
| [T025](docs/backlog/T025.md) | M7 | Testar estabilidade e desempenho | T024 |
| [T026](docs/backlog/T026.md) | M8 | Preparar release comercial | T025 |
