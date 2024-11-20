## Structuri si protocoale:

`udp_packet`: structura de 1552 bytes pentru a primi 1551 bytes de la clientul udp + terminator de sir (pentru cazul STRING - 1500 bytes).

`tcp_pachet`: Protocol client -> server. `type` = tipul comenzii.

`tcp_header`: Protocol server -> client. `type` = tipul de data; `len` = bytes aflati sub header; structura continut: \<topic\>//\<content\>.

`client`: `status` = `CONNECTED`/`DISCONNECTED`; `index` = index element in vectorul de client


## Implementare client:

Se face conexiunea cu serverul si se trimite id-ul. Se pun in epoll evenimente pentru `STDIN` si socket. Daca id-ul este deja folosit sau se primeste comanda `EXIT` la `STDIN` server, se primeste un pachet `tcp_header` de la server si se inchide clientul. Cand se primeste un pachet de la udp, se formateaza continutul in interiorul clientului. La citirea comenzii, se sterge `\n` din buffer.

## Implementare server:

Se pun in epoll evenimente pentru `tcp_socket`, `udp_socket` si `STDIN`. Cand se primeste o conexiune noua, se face conexiunea, se verifica daca id-ul este deja conectat, se adauga clientul in cache-ul local `clients` si se adauga evenimentul `socket_client` in epoll. Cand se primeste un pachet de la tcp, se aplica comanda trimisa de client. Cand se primeste un pachet de la udp, se redirecteaza pachetul la clientii abonati.
