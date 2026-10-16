# Experimento 002 — origem de entrada

## Método

Programa ms-keyboard-probe com janela em primeiro plano, Raw Input para teclado e mouse. Exibe somente F6 pressionado e clique direito; associa IDs temporários aos handles de dispositivo. Não suprime ou redireciona eventos de outros aplicativos.

## Evidência enviada pelo usuário

- Teclados: pressionar e soltar F6 alternadamente produziu Keyboard 1 e Keyboard 2, alternados nos eventos 1–12.
- Mouses: cliques direitos alternados produziram Mouse 1 e Mouse 2, alternados nos eventos 1–9.
- Referência anterior: no Bloco de Notas, 111 e 222 digitados pelos dois teclados apareceram na mesma janela (aaa111222); a outra manteve bbb.
- Build Release x64 passou com /W4 /WX; smoke local verificou criação da janela e registro das duas classes de dispositivos.

## Conclusão e limites

Origem dos eventos distinguível neste laboratório. O teste não demonstra dois focos Windows, cursores independentes, persistência da identidade, contas separadas ou jogos simultâneos. IDs iguais entre teclado e mouse não associam dispositivos a uma estação. Não conclui os marcos M2 ou M3.

## Próxima pergunta arquitetural

Como oferecer dois ambientes visíveis e controláveis simultaneamente para aplicativos Windows comuns? Comparar opções e limites em M1, conforme ADR 0001. Uma aplicação própria com dois painéis pode demonstrar distribuição interna de eventos, mas não prova isolamento para Bloco de Notas ou League of Legends. O gate arquitetural exige evidências de display, foco e entrada independentes antes de construir o motor do produto.
