# Laboratório 008 — duas sessões reais por RDS em VM separada

Estado em 2026-10-18: duas sessões autenticadas e transporte sintético até dois Notepads externos validados. A interação física ainda não foi validada neste transporte. Este laboratório é auxiliar e não aprova o motor nativo nem duas instâncias de LoL. A decisão está no [ADR 0008](../architecture/0008-backend-code-review.md).

## Escopo e ambiente

Uma VM nova executa Windows Server 2025 Evaluation com Desktop Experience. Ela contém somente dados e contas sintéticas do laboratório. A VM de comparação anterior e as contas do host não são reutilizadas.

A mídia vem do [Microsoft Evaluation Center](https://www.microsoft.com/en-us/evalcenter/download-windows-server-2025). A avaliação tem prazo e condições próprios; o período de carência do RDS não dispensa o cumprimento dos termos aplicáveis. Não usar este laboratório como configuração de produção nem alterar períodos de avaliação. Referências: [avaliação oficial](https://www.microsoft.com/en-us/evalcenter/evaluate-windows-server-2025) e [licenciamento RDS](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-client-access-license).

A preparação usa disco virtual novo, Secure Boot, switch Hyper-V Internal dedicado e sub-rede RFC1918 sem colisão com as rotas existentes. O switch não recebe NAT, bridge ou gateway; a regra RDP do convidado permite somente o endereço do host nessa ligação. Nenhuma regra RDP de entrada é aberta no host.

Administração do convidado por [PowerShell Direct](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/powershell-direct), sem habilitar WinRM ou alterar TrustedHosts no host. As senhas são aleatórias, exclusivas do laboratório, guardadas com DPAPI para o mesmo usuário local e nunca passadas pela linha de comando. Unattend não é criptografia: seus arquivos e caches precisam de acesso restrito e remoção após o primeiro boot.

## Sequência de execução

1. Verificar procedência, tamanho e hash da mídia; conferir a assinatura Microsoft do instalador e a edição antes de aplicar a imagem ao disco virtual novo.
2. Inicializar a VM, confirmar marcador exclusivo, edição Server e identidade do convidado; remover os arquivos de bootstrap com credenciais e criar checkpoint anterior ao RDS.
3. Executar [Initialize-RdsLabGuest.ps1](../../scripts/Initialize-RdsLabGuest.ps1) somente dentro dessa VM. O script exige marcador e ambiente válidos, instala a role RDS e prepara duas contas de teste sem privilégios administrativos. Se houver reinicialização pendente, retomá-lo após o boot.
4. Obter por PowerShell Direct o certificado efetivamente associado ao listener RDP. O cliente usa SHA256 desse certificado, NLA e duas conexões distintas. Não aceitar qualquer certificado nem instalar uma raiz de confiança no host.
5. Abrir aplicativos Windows externos nas duas contas. Observar o processo que realmente possui cada janela e comparar identidades/sessões, sem presumir que o PID inicial é o dono final da janela.
6. Encaminhar teclado e mouse de cada par para sua conexão. A captura inicial só atua com o próprio frontend em primeiro plano; perda de foco, remoção de dispositivo ou encerramento devem liberar as teclas e botões mantidos.
7. Fazer o desafio físico: interagir simultaneamente, trocar aplicativos em um posto enquanto o outro mantém digitação ou movimento, e inverter os papéis. Verificar as duas telas físicas atualizando e ausência de entrada cruzada. Entrada sintética automatizada não substitui esse passo.
8. Registrar falhas e retorno ao estado anterior; encerrar apenas conexões, contas e processos pertencentes a esse laboratório.

## Evidência necessária

| Prova | Resultado observado |
|---|---|
| VM separada, marcador e checkpoint verificados | Passou; assinatura Microsoft da mídia e identidade do convidado conferidas |
| Duas contas com identidades distintas | Passou; contas locais de teste não administrativas, tokens distintos |
| Duas sessões RDP simultaneamente conectadas | Passou; duas sessões distintas ativas na coleta WTS |
| Aplicativos externos com processo e conta corretos | Passou; dois Notepads com PID, token SID e SessionID revalidados |
| Duas imagens remotas apresentadas nos monitores locais | Dois framebuffers recebidos e janelas criadas para monitores distintos; conferência visual física pendente |
| Encaminhamento sintético por conexão | Passou; cada Notepad observou sua fixture exata, sem a fixture cruzada nas amostras |
| Encerramento do cliente após a prova sintética | Passou na repetição; código de saída zero e stderr vazio |
| Interação física simultânea e troca de foco sem contaminação | Não observado |
| Recuperação de falha, hotplug, bloqueio e UAC | Não observado |

Relatórios brutos, endereços, SIDs, certificados, identificadores e credenciais ficam somente em `docs/lab/results/` e `build/`, ignorados pelo Git. A evidência pública deve limitar-se a procedimento, resultado, limitações e booleanos revisados, sem anexar os arquivos brutos ou capturas do desktop pessoal.

O preparo do convidado passou após corrigir duas verificações: os métodos CIM podem devolver `ReturnValue` nulo, portanto o script exige também a leitura da configuração aplicada; os resolvers IPv6 padrão inativos não devem ser confundidos com DNS IPv4 configurado. A NIC exclusiva da VM usa somente IPv4, sem alterar IPv6 no host. As fixtures inertes cobrem falha das pós-condições e fechamento da entrada de rede.

O cliente precisou de inicialização explícita do Winsock, agora coberta por resolução numérica offline. Uma tentativa inicial apresentou erro genérico de TLS; a causa não foi isolada. As conexões posteriores passaram com o mesmo certificado fixado, NLA obrigatória e TLS mínimo 1.2, sem relaxar esses controles. O diagnóstico registra apenas etapas e códigos numéricos.

Na prova executada, o modo explícito `--transport-check --unattended-fixtures` manteve Raw Input desativado. Após os dois framebuffers, o controlador conferiu relatórios recentes, proprietários dos processos, Notepad em primeiro plano dentro de cada sessão e documentos vazios. O cliente enviou somente `rdp-seat-a`/`rdp-seat-b`, sem Enter. A coleta posterior exigiu `exact_fixture_now`, sessões ativas distintas, tokens esperados, janela/controle válidos, aplicativos ainda abertos e ausência de erro; não aceitou apenas um sinal de envio ou resultado antigo. A repetição também confirmou encerramento do cliente com código zero e stderr vazio. A captura física continua restrita ao próprio frontend em primeiro plano.

A verificação de documento vazio usa `document_empty_now` com consulta bem-sucedida. Apenas `exact_fixture_now=false` não basta: texto parcial de uma tentativa anterior também produz esse resultado. Tentativas que enviaram entrada exigem preparação limpa antes de repetir.

Compilação Release e 14 testes CTest passaram localmente, além das fixtures inertes do preparo do convidado. Esses testes e a prova sintética têm escopos diferentes. Os relatórios brutos continuam privados.

## Critério de conclusão

O laboratório só demonstra as etapas efetivamente observadas do transporte RDS. Para aprovar o produto no Windows cliente ainda será necessário cumprir o gate do [ADR 0007](../architecture/0007-native-engine-gate.md) no mesmo backend alvo, incluindo GPU, segurança da interação, recuperação, áudio e jogos.
