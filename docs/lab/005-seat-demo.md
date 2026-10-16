# Encaminhamento para dois painéis próprios

Experimento auxiliar de T006, sem aprovação da arquitetura de estações. Objetivo: testar uma associação explícita de dois teclados e dois mouses a dois painéis do nosso programa, mantendo apenas uma janela em primeiro plano.

## Executar

Na pasta do projeto, com dois monitores no modo Estender:

```powershell
.\build\seat-demo\Release\ms-seat-demo.exe
```

O executável desta pasta foi compilado localmente. Para reconstruir no Developer PowerShell:

```powershell
cmake -S . -B build/seat-demo -A x64
cmake --build build/seat-demo --config Release
```

1. Clique em um dos painéis e mantenha nosso programa em primeiro plano.
2. Pressione e solte F6 no teclado destinado ao painel 1. Depois faça o mesmo no teclado destinado ao painel 2.
3. Clique com o botão direito do mouse destinado ao painel 1. Depois faça o mesmo com o mouse destinado ao painel 2. O ponteiro pode ficar sobre qualquer painel; a associação depende da ordem dos dispositivos, não da posição do clique.
4. Aguarde ambos os painéis mostrarem PRONTO. As quatro ações de associação não incrementam contadores.
5. Sem clicar para alternar o foco, pressione e solte F6 cinco vezes em cada teclado. Cada painel deve mostrar F6: 5.
6. Clique cinco vezes com o botão direito de cada mouse, mantendo o ponteiro sobre um dos painéis. Cada painel deve mostrar Cliques direitos: 5.
7. Repita com as duas pessoas usando seus pares simultaneamente. Anote diferenças, eventos faltantes ou contadores alterados pelo dispositivo errado.

F8 reinicia as associações e contadores. Desconectar um dispositivo associado também reinicia tudo. Fechar qualquer painel encerra o programa. Ao mudar para outro aplicativo, o teste para de receber entrada; retorne a um painel para continuar. Solte F6 antes de sair e retornar ao aplicativo.

## O que esta prova mede

O processo tem um receptor Raw Input por classe de dispositivo e encaminha os eventos aos próprios contadores. Teclados e mouses são associados separadamente pelo usuário, com handles temporários. Dispositivos extras são ignorados após a associação. Repetição automática de F6 enquanto pressionado não aumenta o contador.

Os painéis compartilham processo, sessão Windows e desktop. O ponteiro do Windows continua compartilhado. Não há cursor independente, redirecionamento para outros programas, contas separadas, áudio separado nem teste de LoL. Mesmo um resultado físico perfeito não fecha P01–P08 do escopo nem aprova T007.

Fundamento: [Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input) permite distinguir dispositivos. [RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices) permite um receptor por classe em cada processo. Registro sem INPUTSINK limita o recebimento ao primeiro plano; não bloqueia a entrada dos outros aplicativos.

## Evidência disponível

- Build Release x64 local concluído com /W4 /WX.
- `--self-test`: passou com dispositivos sintéticos; verifica contadores separados, ignorar dispositivos extras/indisponíveis e reiniciar ao remover dispositivo associado.
- `--smoke`: passou; cria dois painéis ocultos e registra entrada. Não requer dois monitores.
- Teste físico de associação e contadores: **confirmado pelo usuário** em 2026-10-07 após seguir o roteiro de cinco eventos por dispositivo. Captura enviada na conversa mostra ambos os painéis associados, cada um com F6: 5 e Cliques direitos: 5; painel 1 em primeiro plano. Usuário informou: "funcionou certinho". A imagem mostra o resultado final; a ausência de cruzamento durante o teste é relatada pelo usuário, sem log por evento.
- Uso simultâneo pelos dois pares: **confirmado pelo usuário** após o roteiro de dez F6 adicionais por teclado e dez cliques direitos adicionais por mouse, com resultado esperado de 15/15 em cada painel. Usuário informou "funcionou certinho"; nesta etapa não foi enviada captura nem log por evento. Evidência de funcionamento simultâneo relatado dentro do próprio programa, sem medição de latência ou teste prolongado.
- Recuperação após desconexão: **confirmada pelo usuário** após o roteiro abaixo. Usuário informou "funcinou", confirmando reinício das associações/contadores, reconexão e novo teste de um evento por dispositivo, com resultado esperado de 1/1 em cada painel. Sem captura ou log por evento nesta etapa. O relato não comprova recuperação de sessões nem isolamento de aplicativos externos.

## Roteiro de recuperação validado

1. Com os painéis abertos, desconecte apenas um teclado associado. Se houver receptor USB compartilhado com outros dispositivos, use um dispositivo com conexão própria para este teste.
2. Ambos os painéis devem zerar os contadores e voltar a aguardar associação, sem encerrar o programa.
3. Reconecte o teclado. Associe novamente os dois teclados com F6, na ordem 1 e 2, e os dois mouses com clique direito, na ordem 1 e 2.
4. Após PRONTO, pressione e solte F6 uma vez em cada teclado e clique direito uma vez com cada mouse. Cada painel deve mostrar 1/1.

Se a desconexão não for detectada, registre o comportamento e o tipo de conexão; não substitua o resultado por um reset manual com F8.

## Encerramento do experimento básico

Associação, contagem por dispositivo, uso simultâneo e recuperação após desconexão foram confirmados pelo usuário no programa cooperativo. Não solicitar novas repetições desse roteiro sem falha ou mudança de implementação. Próxima etapa de T006: identificar e validar um mecanismo de isolamento de aplicativos externos/sessões no alvo Windows 11 Pro. Ainda não existe mecanismo aprovado; ampliar os contadores não resolve essa lacuna.

Fontes consultadas em 2026-10-07.
