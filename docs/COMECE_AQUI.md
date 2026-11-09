# Comece aqui

Guia rápido pra usar o OmniPad no PS5 desbloqueado (FW 7.00 a 13.60). Com ele dá pra jogar com controles USB, com dongle 2.4G e com Bluetooth por um adaptador USB.

## 1. Baixe o arquivo

Na página de [Releases](https://github.com/jpemendonca/OmniPad-CE/releases), baixe o `OmniPad-CE.elf` da versão mais recente.

## 2. Mande pro PS5 e inicie

Use o loader de payloads que você já usa, como o PLK Payload Manager.

1. Mande o `OmniPad-CE.elf` pro PS5.
2. **Inicie o payload.** No PLK, depois de mandar o arquivo, aperte **Launch**. Só mandar não basta: se você não iniciar, o OmniPad não roda e nada acontece.
3. Confira o aviso no canto da tela: **"OmniPad ... ativo"**. Ele também mostra o endereço do painel. Sem esse aviso, o OmniPad não está rodando.

## 3. Conecte o controle

- **Com fio ou dongle 2.4G:** plugue numa porta USB do PS5. Aparece o aviso "OmniPad: <controle> conectado".
- **Bluetooth (DualShock 4 ou Xbox Series):**
  1. Plugue um adaptador Bluetooth USB no PS5. Foram testados o TP-Link UB500 e um genérico "CSR 4.0" comum. Aparece o aviso "adaptador Bluetooth pronto".
  2. Coloque o controle em pareamento. No DS4, segure **Share + PS** até a luz piscar rápido. No Xbox, segure o botão de parear, em cima do controle, até o logo piscar rápido.
  3. Em alguns segundos ele conecta. Da próxima vez, é só ligar o controle: **PS** no DS4, o botão **Xbox** no Xbox.

- **Dois Joy-Cons como um controle só:** conecte os dois por Bluetooth. Em até 1 minuto depois de conectar o segundo, segure **L** no esquerdo e **R** no direito por 1 segundo. Aparece o aviso "Joy-Cons unidos". Pra separar, desligue um deles: o outro volta a funcionar sozinho, de lado.

O DualSense oficial continua sendo o jogador 1, sem fio, como sempre. O OmniPad não mexe no rádio Bluetooth do PS5.

## 4. Escolha o perfil

Aperte **PS** no controle novo. O PS5 mostra a tela de escolher o perfil: escolha quem vai jogar com ele. Os controles do OmniPad são os jogadores 2, 3 e 4.

## 5. Painel

- **No próprio PS5:** abra a aba **Mídia** e o ícone **OmniPad**. Ele aparece depois da primeira vez que o OmniPad rodar, e só abre o painel enquanto o OmniPad estiver rodando.
- **No celular ou no PC:** abra `http://<IP do PS5>:8105`. O IP aparece no aviso de "ativo".

No painel você testa os botões do controle, vê se a conexão está boa, pareia ou esquece controles Bluetooth e acompanha o log.

## Mapear botões (opcional)

Ninguém precisa fazer isso: o controle já funciona como está. Se o seu não tem botão de touchpad, ou você quer trocar algum botão, aperte **Mapear botões** ao lado do desenho do controle no painel. Clique no botão do desenho e, em até 10 segundos, aperte no controle o que vai virar ele. Apertar dois juntos cria um atalho, como L3 + R3 para o touchpad. Vale para aquele modelo de controle e fica salvo. **Restaurar tudo ao padrão** desfaz tudo.

## Não funcionou?

| O que acontece | O que fazer |
|---|---|
| Nenhum aviso do OmniPad apareceu | O payload não foi iniciado. Mande de novo e aperte **Launch** (ou o equivalente no seu loader). |
| O painel não abre no celular | O celular e o PS5 precisam estar na mesma rede. Confira o IP no aviso de "ativo". |
| O ícone da aba Mídia abre uma página de erro | O OmniPad não está rodando. Inicie o payload e abra o ícone de novo. |
| Controle com fio não responde | Alguns cabos só carregam, sem passar dados. Teste outro cabo. |
| Adaptador Bluetooth não aparece no painel | Tire e plugue de novo. Adaptadores Realtek que não sejam RTL8761B ainda não funcionam. |
| Segundo controle não conecta num adaptador genérico | Com um controle já conectado, os genéricos "CSR 4.0" não procuram sozinhos. Toque em **Parear controle** no painel e coloque o controle em pareamento. Isso pode travar esse chip e derrubar os controles: se o aviso pedir, tire e plugue o adaptador. Pra usar dois controles Bluetooth, prefira um adaptador com chip RTL8761B, como o TP-Link UB500. |
| O DS4 não conecta por Bluetooth | Coloque em pareamento (**Share + PS**, luz piscando rápido) com o adaptador já plugado. |
| O DS4 pareado não volta só com PS | Coloque em pareamento de novo. Ele conecta e fica salvo outra vez. |
| Depois do repouso, o controle pede o perfil de novo | É o esperado. No repouso o OmniPad solta os controles, como o PS5 faz com o DualSense. Uns 10 segundos depois de acordar, os controles USB voltam pedindo o perfil. Os controles Bluetooth voltam quando você aperta PS. |

Se ainda assim não der certo, abra o painel e toque em **Algum problema? Envie um relatório**, no canto de cima. Conte o que está acontecendo, se quiser, e toque em **Enviar**. O log e o diagnóstico vão direto pro desenvolvedor, e aparece um código curto. Cite esse código no grupo, no Reddit ou numa [issue](https://github.com/jpemendonca/OmniPad-CE/issues). Nada sai do console sem você apertar Enviar, os endereços MAC são cortados e o relatório é apagado em 90 dias.

Se preferir, toque em **Copiar diagnóstico**: ele copia a versão, os controles, o Bluetooth e o fim do log num texto só, pra colar onde você for pedir ajuda.

## Iniciar sozinho

O OmniPad precisa ser iniciado de novo toda vez que o PS5 desliga ou reinicia. Se o seu loader tiver uma opção de iniciar payloads automaticamente (autoload), coloque o `OmniPad-CE.elf` nela. Aí ele já sobe junto com o PS5.
