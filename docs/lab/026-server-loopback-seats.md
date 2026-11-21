# Laboratório 026: dois postos locais no próprio Windows Server

Atualizado em 2026-11-21. **Marco: dentro de uma VM com Windows Server 2025 e a role RDS, três sessões ficaram ativas ao mesmo tempo no mesmo computador.** A sessão do console rodava o nosso cliente de postos. As duas contas de posto não administrativas entravam pelo endereço local `127.0.0.1`. Cada posto recebeu imagem, e a entrada enviada a cada um chegou só ao aplicativo da própria conta, sem cruzamento. O teste usou um monitor virtual e entrada sintética. Teclados, mouses e monitores físicos, áudio, aceleração gráfica e jogos ainda não foram testados nesse arranjo. Todas as rodadas foram restauradas ao checkpoint anterior.

## Por que Windows Server

O [laboratório 025](025-native-session-arbitration.md) confirmou que o Windows 11 Pro mantém uma única sessão interativa ativa. O projeto não altera o serviço de Área de Trabalho Remota, o licenciamento nem os limites de sessão. Projetos que liberam várias sessões ativas no Windows cliente fazem isso alterando componentes do Windows, então esse caminho não foi seguido.

O responsável pelo projeto escolheu o Windows Server com a role RDS, uma edição licenciada para várias sessões. Nela, as sessões simultâneas são um recurso previsto, sujeito ao licenciamento de RDS. A avaliação do Windows Server e o período de carência do RDS servem só para testes.

## Arranjo testado

- A conta de administração do laboratório entra no console e roda o cliente de postos do [laboratório 008](008-rds-transport-comparison.md), baseado em FreeRDP.
- O cliente abre duas conexões para `127.0.0.1`, uma para cada conta de posto. Ele exige NLA, TLS e o pin do certificado do próprio servidor, e as senhas ficam protegidas por DPAPI do usuário do console.
- Cada conexão cria uma sessão própria. O observador de cada conta confere apenas o Notepad dela.
- Com um único monitor virtual, o modo de verificação automática divide o monitor entre os dois painéis. O modo normal continua exigindo um monitor por posto.

## Resumo das rodadas

| Rodada | Mudança | Resultado |
| --- | --- | --- |
| S1 | Primeira execução do cliente dentro do servidor | A senha das contas de laboratório tinha expirado pela política padrão de 42 dias; o posto 1 recusou a conexão |
| S1b | Senhas das contas de laboratório sem expiração, só dentro da rodada restaurada | **Console e os dois postos ativos ao mesmo tempo**; 87 e 85 frames; as fixtures saíram antes de o Notepad abrir |
| S1c | Miniaturas do frame de cada posto antes e depois do envio | As miniaturas mostraram a área de trabalho ainda sem o Notepad no momento do envio |
| S1d | Envio 150 segundos depois da conexão dos dois postos | **Cada Notepad recebeu exatamente o texto do próprio posto, sem o texto do outro**; 97 e 100 frames; o cliente terminou com código 0 |

Cada rodada criou um checkpoint antes de começar e o restaurou e removeu no fim. Os checkpoints originais do laboratório foram preservados.

## Mudanças no cliente

- A configuração aceita exatamente `127.0.0.1`, além dos endereços IPv4 privados do laboratório. Outros endereços de loopback continuam recusados.
- `--fixture-delay <30-240>` e `--frame-snapshots <pasta>` valem só na verificação automática sintética.
- Os 18 testes do cliente passam com o SDK do FreeRDP 3.33.0.

## O que ainda falta

- Testar num computador real com Windows Server: dois monitores físicos, dois pares de teclado e mouse roteados para os postos e conferência visual.
- Permitir que cada pessoa faça o próprio login no seu posto, em vez de credenciais configuradas.
- Áudio por posto.
- Aceleração gráfica nas sessões e codecs adequados para vídeo e jogos.
- Medir desempenho.
- Instalação e início automático do cliente de postos no console.

Os resultados brutos, as credenciais e as miniaturas continuam privados. Consulte o [trabalho restante](../REMAINING-WORK.md).
