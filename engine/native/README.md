# Núcleo inicial do motor nativo

Esta biblioteca começa o controle de identidade e recursos do motor Windows. Ela ainda não cria sessões ou terminais, não apresenta desktops e não entrega entrada. A base de [leitura de texturas D3D11](display/README.md) oferece um módulo gráfico separado e testável. Não há serviço, driver, transporte RDS ou backend nativo simulado nesta pasta. O gate do [ADR 0007](../../docs/architecture/0007-native-engine-gate.md) permanece aberto.

`VerifiedIdentity` captura fatos de um token primário real, com uma duplicação privada e somente consulta. Exige usuário interativo, sessão diferente de zero, SID de usuário válido, logon SID habilitado e AuthenticationId. Rejeita impersonação e identidades de serviço. SID, identificador de logon e handle não são exportados nem registrados. `SameLogon` compara a encarnação de logon; copiar um token não cria outra identidade. A captura não autentica IPC nem comprova que a sessão continua conectada ou tem um terminal.

`SeatLifecycle` admite duas reservas não interativas, usando `VerifiedIdentity`. Rejeita usuários ou sessões compartilhados e colisões de monitor, teclado ou mouse. Cada reserva tem controlador e geração próprios. Recursos usam identificadores opacos locais e geração de chegada; não se passam caminhos de dispositivos, nomes de contas ou dados recebidos de IPC como autoridade.

O controlador deve serializar todas as chamadas e chamar `Poll` em seu temporizador. Reservas duram no máximo cinco minutos. Expiração, bloqueio, desktop seguro, desconexão ou remoção suspendem a elegibilidade e preservam a exclusividade até cancelamento ou reassociação explícita. `Reassociate` exige a mesma encarnação de logon; outra conta/sessão exige cancelar e reservar novamente. O handle antigo não cancela nem suspende a nova geração. Uma retirada fica registrada mesmo antes da primeira reserva ou depois do cancelamento; um aviso atrasado não afeta uma chegada mais nova.

Se faltar memória ao registrar chegadas ou retiradas, o controlador suspende todas as reservas, inclusive a do outro posto, e recusa novas admissões com `TrackingFailure`. A recuperação da memória não reabre esse controlador. `Cancel` continua disponível; um novo controlador exige observações novas do adaptador. O `memory_resource` opcional deve permanecer vivo durante toda a vida do controlador.

`Reservation` é móvel e não executa callbacks no destrutor. O dono chama `Cancel`; se perder o handle, o temporizador ainda suspende a reserva ao vencer o prazo. Destruir o controlador invalida seus handles. `IsCurrent` consulta somente elegibilidade da reserva: nunca autoriza encaminhar eventos, anexar terminal, autenticar usuário ou apresentar uma tela.

Os testes de política usam identidades e chaves fictícias em `BasicSeatLifecycle<FixtureIdentity>`. O alias de produção usa fatos reais do Windows. Os testes de identidade usam apenas o próprio token e cópias temporárias: nenhuma conta, processo ou sessão é criado. Em runners não interativos, o teste exige a rejeição exata justificada pelos fatos do token. No ambiente interativo, confere equivalência, validade após fechar o handle original e rejeição de cópia com INTERACTIVE deny-only. O teste também compila o cabeçalho público com as macros `min`/`max` padrão do Windows habilitadas. Os resultados não comprovam dois usuários nativos funcionando.

Para compilar e testar a base, na Developer PowerShell x64, na raiz do repositório:

```powershell
cmake -S . -B build/native-core -A x64 -DBUILD_TESTING=ON
cmake --build build/native-core --config Release
ctest --test-dir build/native-core -C Release -R native-engine-boundaries --output-on-failure
```

Essa configuração não baixa dependências, instala componentes ou altera dispositivos. A integração necessária para a segunda sessão e seu laboratório estão no [ADR 0009](../../docs/architecture/0009-native-core-and-session-research.md).
