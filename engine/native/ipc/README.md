# IPC local autenticado do núcleo nativo

Esta camada é um primitivo de **transporte de controle** reutilizável pelo núcleo
nativo. Ela abre um named pipe **local** e identifica o processo do outro lado a
partir de fatos atestados pelo kernel, produzindo uma `VerifiedIdentity` do peer
real. Segue a "Fronteira de segurança" do [ADR 0009](../../../docs/architecture/0009-native-core-and-session-research.md).

**O que ela não é.** Não cria conta, sessão ou terminal, não conecta RDS, não
toca `termsrv`, licenciamento, serviço ou driver, e não encaminha entrada nem
apresenta tela. Uma `VerifiedIdentity` devolvida é um retrato de token, **não**
prova de que existe um segundo terminal. Isto é apenas o canal de controle; a
lacuna da segunda sessão permanece em aberto no ADR 0009 e no gate do
[ADR 0007](../../../docs/architecture/0007-native-engine-gate.md).

## Contrato de autenticação

`AuthenticatedPipeServer::AcceptAndVerify` deriva a identidade **somente** do
kernel, nesta ordem:

1. `GetNamedPipeClientProcessId` fornece o PID atestado do cliente desta exata
   instância do pipe. Um PID de sistema (≤ 4) é recusado.
2. A thread do servidor **não pode já estar impersonando** (um token de thread
   pré-existente nunca é descartado em silêncio). O servidor impersona o cliente
   só para ler SID de usuário, LUID de logon, ID de sessão e SIDs de logon do
   contexto conectado (um cliente em nível anônimo é recusado aqui) e então
   **reverte com resultado verificado** (`RevertToSelf`); uma rede RAII reverte
   em qualquer exceção. Se a reversão continuar falhando na limpeza, o processo
   encerra por fail-fast para não continuar com o contexto do cliente, conforme
   a orientação de [`RevertToSelf`](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-reverttoself).
3. O servidor abre o processo peer com direitos mínimos
   (`PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`), recusa um processo já
   encerrado ou **criado depois** do instante em que a conexão foi observada
   (esse processo não pode ser o cliente; o PID foi reutilizado) e lê o **token
   primário real** do processo. Essa comparação de instante usa o relógio do
   sistema e é uma heurística, não uma prova: um ajuste do relógio entre a
   observação e a criação de um processo com PID reutilizado pode tanto recusar
   um peer legítimo quanto deixar de detectar o reuso (um ajuste para trás faz
   um processo posterior parecer anterior). Quem protege de fato é o vínculo do
   item 4, não esse check.
4. O vínculo exige que o contexto que abriu o pipe e o token primário do
   processo concordem em **todos os fatos que a identidade carrega**: SID de
   usuário, LUID de logon, ID de sessão e SIDs marcados pelo kernel como de
   logon (`PeerBindingMismatch`). É a mesma noção de `SameLogon`. Com o item 3,
   isso **reduz** a janela de reuso de PID; não é uma eliminação comprovada
   dessa corrida.
5. A identidade é produzida por `VerifiedIdentity::FromPrimaryToken`, que mantém
   seu próprio contrato: token primário, usuário interativo, sessão ≠ 0,
   não-serviço, exatamente um SID de logon habilitado. Um token que não seja de
   assento gera `IdentityError` exata, sem ser enfraquecido aqui.
6. Com `allowed_client_sid` configurado, a DACL avalia o token **efetivo** de
   quem abre o pipe. Por isso, depois da identidade, o SID de logon do **token
   primário** de onde ela veio também precisa ser o da política
   (`PeerPolicyMismatch`).

Os itens 4 e 6 fecham uma inconsistência de contrato: antes, sessão e SID de
logon da identidade não eram conferidos com o contexto conectado, e a política
não era conferida no token primário. Não foi demonstrado um caminho sem
privilégio que produza essa divergência com o mesmo usuário e o mesmo LUID de
logon (mudar a sessão de um token exige `SeTcbPrivilege`; acrescentar grupos
exige criar token). As verificações não dependem dessa suposição.

**Autoridade.** Usuário, SID ou sessão **declarados em qualquer mensagem** nunca
são autoridade. O conteúdo das mensagens é opaco para a autenticação. Observar um
token também não prova que a sessão continua conectada ou que há um terminal.

## Estado one-shot e fail-closed

`AcceptAndVerify` é chamável uma vez. `ReadMessage`/`WriteMessage` só funcionam
**após** uma verificação bem-sucedida. Qualquer falha na aceitação (inclusive a
rejeição de identidade não-assento) ou qualquer erro de transporte posterior
**fecha o endpoint**: a instância é desconectada e o handle é liberado, e novas
chamadas são recusadas com `NotVerified`. Assim, bytes de um quadro parcial ou
ambíguo nunca são reaproveitados como troca verificada. O cliente é igualmente
fail-closed em erro de I/O.

## Limites e segurança do transporte

- **Tamanho:** cada mensagem tem prefixo de 4 bytes e é limitada por
  `PipeLimits::max_message_bytes` (padrão 4096, teto absoluto de 1 MiB). Um
  quadro maior que o limite é recusado na leitura **sem** alocar o tamanho
  declarado (propriedade do código; o teste confere a recusa e o fechamento,
  não mede alocação); `WriteMessage` recusa pelo tamanho e por ponteiro nulo com tamanho
  positivo **antes** de qualquer I/O.
- **Prazo finito de espera:** cada aceitação e cada quadro inteiro usam
  `PipeLimits::deadline_ms` (padrão 10 s) e um evento de cancelamento opcional.
  Um quadro truncado ou parado expira como `IoTimedOut` (`ConnectTimedOut` na
  aceitação). O evento sinalizado interrompe uma espera pendente e é relatado
  como `Cancelled`, distinto do prazo; ele não desfaz uma operação já concluída
  de forma síncrona pelo kernel. O prazo tem **teto finito de 120 s**;
  `INFINITE`/`MAXDWORD` é recusado em `Create`. No estouro ou cancelamento, a
  operação é cancelada com `CancelIoEx` e drenada até a conclusão real antes de
  retornar, preservando `OVERLAPPED`/buffer. A conclusão do cancelamento pode
  exceder o prazo: não há garantia de tempo real nem de recuperação de driver
  de kernel defeituoso. Desconexão no meio de um quadro é `Disconnected`.
- **Instância única:** criado com `FILE_FLAG_FIRST_PIPE_INSTANCE`; um segundo
  `Create` do mesmo nome é recusado.
- **Sem acesso remoto:** `PIPE_REJECT_REMOTE_CLIENTS` barra clientes remotos no
  kernel. Isso é estrutural (uma flag do pipe), **não** exercitado pelo teste
  automatizado — o teste é local.
- **DACL por direitos explícitos:** o descritor concede dono e grupo ao usuário
  criador e uma única ACE com a máscara explícita
  `0x00100083` (`FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES |
  SYNCHRONIZE`). Não usa máscara genérica, portanto **não concede**
  `FILE_CREATE_PIPE_INSTANCE`. O cliente conecta pedindo exatamente esses
  direitos (sem `GENERIC_WRITE`) e com `SECURITY_IDENTIFICATION`: o servidor lê
  quem ele é, mas não age como ele. `Create` aceita um `allowed_client_sid`
  opcional para **política por logon de configuração confiável**. Somente SID
  de logon é aceito nessa opção; grupos amplos e outros tipos são recusados.
  Esse valor nunca deve vir de uma mensagem não confiável. Vazio concede o
  usuário criador, abrangendo os logons desse usuário. O dono pode alterar a
  DACL: a política padrão não constitui isolamento contra o próprio usuário.

Lifetime por RAII (`UniqueHandle`); o descritor é liberado após a criação.
As operações e a destruição de cada endpoint devem ser serializadas pelo
chamador; o evento de cancelamento precisa permanecer válido até o retorno.
O cliente limita a impersonação, mas não autentica a identidade do servidor;
o nome do endpoint deve vir de uma fonte confiável. Identidade de conexão
também não é autorização para qualquer comando: cada consumidor deve definir
as permissões das suas operações.

## Compilar e testar

Na Developer PowerShell x64, na raiz do repositório:

```powershell
cmake -S . -B build/native-core -A x64 -DBUILD_TESTING=ON
cmake --build build/native-core --config Release --target ms-native-ipc-tests
ctest --test-dir build/native-core -C Release -R native-ipc-authentication --output-on-failure
```

O teste cria pipes próprios e **processos filhos próprios** como clientes (e os
encerra e recolhe mesmo se uma verificação falhar antes do `Wait`). Cada filho
permanece vivo até observar a resposta ou o fail-close do servidor, e o código
de saída esperado é exigido; um filho que não termina no prazo é falha. O teste
confere: porta de estado antes da verificação (leitura/escrita recusadas com
`NotVerified`); limites de argumento (tamanho e ponteiro nulo); recusa de prazo
infinito, política de grupo amplo e nome com terminador embutido; preservação
de token de thread pré-existente, conferindo que `TokenId` e `ModifiedId` são
os mesmos antes e depois da recusa (a mesma instância de token, não modificada);
cancelamento real de aceitação pendente, relatado como `Cancelled`; filho
próprio conectado com `SECURITY_ANONYMOUS`, recusado em `ImpersonationFailed`
com a causa documentada `ERROR_CANT_OPEN_ANONYMOUS`, sem impersonação residual
(só `ERROR_NO_TOKEN` conta como ausência de token), endpoint inutilizável e
saída limpa do filho; filho do próprio usuário negado pelo kernel
(`ERROR_ACCESS_DENIED`) ao abrir um pipe cuja política é um SID de logon que
ele não tem; peer real autenticado pelo token do processo, com o payload
declarado lido só depois da identidade; reversão de impersonação; recusa de
quadro acima do limite; quadro truncado e parado expirando no servidor; estouro
de prazo na aceitação; e instância única. Contra um pipe
bruto do próprio teste, o lado cliente do enquadramento compartilhado também
falha fechado em quadro truncado parado (`IoTimedOut`), quadro truncado seguido
de desconexão (`Disconnected`, sem esperar o prazo) e leitura pendente
cancelada (`Cancelled`); em cada caso a outra ponta confirma que o handle do
cliente foi liberado.

Em runner sem identidade de assento interativo, o teste exige a **categoria
exata** rejeitada pelo módulo de identidade (observada de forma independente),
o fail-close do endpoint e que cada filho observe esse fechamento. Nesse caso o
plano de dados do **servidor não é exercitado**; apenas o enquadramento do lado
cliente é. As recusas por nível anônimo e por política de logon estranha
acontecem antes da etapa de identidade e são exigidas em qualquer runner.
Nenhuma conta, sessão, serviço ou driver é criado.

**Não coberto ao vivo.** Os ramos de recusa `PeerBindingMismatch` (sessão, SID
de logon, usuário ou LUID divergentes) e `PeerPolicyMismatch` exigiriam um token
construído com privilégio ou outra conta, o que este teste não faz. Em runner de
assento, o caminho positivo passa por essas verificações com a política do
próprio logon, mostrando que elas não recusam um peer legítimo. Também não são
testados: negação a token de baixa integridade ou restrito, ausência de
alocação no quadro grande e espera ociosa do peer além do prazo, que fecha o
endpoint como `IoTimedOut`.
