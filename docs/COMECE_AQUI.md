# Comece aqui

Guia rápido pra usar o OmniPad no PS5 desbloqueado (FW 7.00 a 13.60). Com ele dá pra jogar com controles USB, com dongle 2.4G e com Bluetooth por um adaptador USB.

## 1. Baixe o arquivo

Na página de [Releases](https://github.com/jpemendonca/OmniPad-PS5/releases), baixe o `OmniPad-PS5.elf` da versão mais recente.

## 2. Mande pro PS5 e inicie

Use o loader de payloads que você já usa, como o PLK Payload Manager.

1. Mande o `OmniPad-PS5.elf` pro PS5.
2. **Inicie o payload.** No PLK, depois de mandar o arquivo, aperte **Launch**. Só mandar não basta: se você não iniciar, o OmniPad não roda e nada acontece.
3. Confira o aviso no canto da tela: **"OmniPad ... ativo"**. Ele também mostra o endereço do painel. Sem esse aviso, o OmniPad não está rodando.

## 3. Conecte o controle

- **Com fio ou dongle 2.4G:** plugue numa porta USB do PS5. Aparece o aviso "OmniPad: <controle> conectado".
- **Bluetooth (DualShock 4):**
  1. Plugue um adaptador Bluetooth USB no PS5. Foram testados o TP-Link UB500 e um genérico "CSR 4.0" comum. Aparece o aviso "adaptador Bluetooth pronto".
  2. Coloque o controle em pareamento: segure **Share + PS** até a luz piscar rápido.
  3. Em alguns segundos ele conecta. Da próxima vez, é só apertar **PS**.

O DualSense oficial continua sendo o jogador 1, sem fio, como sempre. O OmniPad não mexe no rádio Bluetooth do PS5.

## 4. Escolha o perfil

Aperte **PS** no controle novo. O PS5 mostra a tela de escolher o perfil: escolha quem vai jogar com ele. Os controles do OmniPad são os jogadores 2, 3 e 4.

## 5. Painel

- **No próprio PS5:** abra a aba **Mídia** e o ícone **OmniPad**. Ele aparece depois da primeira vez que o OmniPad rodar, e só abre o painel enquanto o OmniPad estiver rodando.
- **No celular ou no PC:** abra `http://<IP do PS5>:8095`. O IP aparece no aviso de "ativo".

No painel você testa os botões do controle, vê se a conexão está boa, pareia ou esquece controles Bluetooth e acompanha o log.

## Não funcionou?

| O que acontece | O que fazer |
|---|---|
| Nenhum aviso do OmniPad apareceu | O payload não foi iniciado. Mande de novo e aperte **Launch** (ou o equivalente no seu loader). |
| O painel não abre no celular | O celular e o PS5 precisam estar na mesma rede. Confira o IP no aviso de "ativo". |
| O ícone da aba Mídia abre uma página de erro | O OmniPad não está rodando. Inicie o payload e abra o ícone de novo. |
| Controle com fio não responde | Alguns cabos só carregam, sem passar dados. Teste outro cabo. |
| Adaptador Bluetooth não aparece no painel | Tire e plugue de novo. Adaptadores Realtek que não sejam RTL8761B ainda não funcionam. |
| Segundo controle não conecta num adaptador genérico | Com um controle já conectado, os genéricos "CSR 4.0" não procuram sozinhos. Toque em **Parear controle** no painel e coloque o controle em pareamento. |
| O DS4 não conecta por Bluetooth | Coloque em pareamento (**Share + PS**, luz piscando rápido) com o adaptador já plugado. |
| O DS4 pareado não volta só com PS | Coloque em pareamento de novo. Ele conecta e fica salvo outra vez. |

Se ainda assim não der certo, abra o painel e toque em **Copiar diagnóstico**. Ele copia a versão, os controles, o Bluetooth e o fim do log num texto só. Cole no grupo pra pedir ajuda.

## Iniciar sozinho

O OmniPad precisa ser iniciado de novo toda vez que o PS5 desliga ou reinicia. Se o seu loader tiver uma opção de iniciar payloads automaticamente (autoload), coloque o `OmniPad-PS5.elf` nela. Aí ele já sobe junto com o PS5.
