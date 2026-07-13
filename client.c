#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //chiamate di sistema posix
#include <signal.h> //gestione segnali
#include <sys/socket.h> //socket
//gestione ip
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h> //gestione errori

#define PORT 8080 //porta a cui connettersi
#define BUF_SIZE 4096 //dimensione del buffer per i comandi


static ssize_t ricevi_tutto(int fd, char *buf, size_t n){ //per evitare la perdita di byte

    size_t ricevuti=0;
    while(ricevuti<n){ //tiene traccia dei byte ricevuti
        ssize_t r=recv(fd, buf + ricevuti, n - ricevuti, 0);
        if (r<=0) return r; //per r=0 sessione chiusa, per r<0 errore
        ricevuti+=r;}
    return (ssize_t)ricevuti;
}

static ssize_t ricevi_riga(int fd, char *buf, size_t lunghezza_max){ //per leggere l'header mandato dall'esecutore

    size_t i=0;
    while(i<lunghezza_max-1){ //legge un byte alla volta dal socket
        char c;
        ssize_t r=recv(fd, &c, 1, 0);
        if(r<=0){return r;}
        buf[i++]=c; //aggiunta del carattere al buffer
        if(c=='\n'){break;}} //se \n esce dal loop
    buf[i] = '\0';
    return (ssize_t)i;
}

int main(void){

    const char *ip_server = "127.0.0.1"; //ip localhost di default

    //ignora SIGINT e SIGQUIT
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    //crea un socket tcp
    int fd_socket=socket(AF_INET, SOCK_STREAM, 0);
    if (fd_socket<0) {perror("socket"); exit(1);}

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT)};

    //verifica se l'ip di default sia valido
    if(inet_pton(AF_INET, ip_server, &addr.sin_addr)<=0){ 
        fprintf(stderr, "Indirizzo non valido: %s\n", ip_server);
        exit(1);}

    if(connect(fd_socket, (struct sockaddr *)&addr, sizeof(addr))<0) {
        perror("connessione");
        fprintf(stderr, "Questo server gira su %s:%d?\n", ip_server, PORT);
        exit(1);
    }

    printf("Connesso al server su %s:%d\n", ip_server, PORT);
    printf("Digita un comando, altrimenti digita exit per uscire.\n");

    char comando[BUF_SIZE];

    while(1){

        //chiede al client un comando
        printf("\nDigita il tuo comando -> ");
        fflush(stdout); //per evitare che rimanga nel buffer lo stampa subito

        if (fgets(comando, sizeof(comando), stdin)==NULL) { //ritorna null se l'utente preme ctrl+d
            printf("\nEOF, disconnessione.\n");
            send(fd_socket, "exit", 4, 0);
            break;
        }

        //toglie la newline
        size_t lunghezza=strlen(comando);

        if(lunghezza>0 && comando[lunghezza-1]=='\n'){comando[--lunghezza] = '\0';}

        if(lunghezza==0){continue;} //ignora se vuoto

        //invia il comando all'esecutore
        if(send(fd_socket, comando, lunghezza, 0)<=0){
            perror("Invio");
            break;}

        //se digitato exit
        if(strcmp(comando, "exit")==0){
            printf("Disconnessione del client.\n");
            break;}

        //riceve l'header
        char header[64];
        ssize_t ri=ricevi_riga(fd_socket, header, sizeof(header));
        if(ri<=0){
            printf("L'esecutore ha chiuso la connessione.\n");
            break;}

        //controlla se l'esecutore sta chiudendo
        if(strncmp(header, "__EXIT__", 8)==0){
            printf("Esecutore chiuso.\n");
            break;}

        long lunghezza_risultato=0;

        if(sscanf(header, "LEN:%ld", &lunghezza_risultato) != 1 || lunghezza_risultato <= 0){ //se l'header non è nel giusto formato
            fprintf(stderr, "Errore nell'header dall'esecutore: %s\n", header);
            break;}

        //riceve la lunghezza in byte del risultato e lo colloca dinamicamente
        char *risultato = malloc(lunghezza_risultato+1);
        if(!risultato){perror("malloc"); break;}

        ssize_t ricevuti = ricevi_tutto(fd_socket, risultato, (size_t)lunghezza_risultato);
        if(ricevuti<=0) {
            free(risultato);
            printf("L'esecutore ha chiuso la connessione.\n");
            break;}

        risultato[ricevuti] = '\0';

        //il client riceve il risultato
        printf("%s", risultato);
        if (risultato[ricevuti-1] != '\n')
            printf("\n");
        fflush(stdout);

        free(risultato);
    }

    close(fd_socket);
    return 0;
}
