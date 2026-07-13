#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //chiamate di sistema posix
#include <signal.h> //gestione segnali
#include <sys/socket.h> //socket
#include <sys/wait.h> //attesa per i processi figli
//gestione ip
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h> //gestione errori

#define PORT 8080 //porta a cui connettersi
#define BACKLOG 10 //massimo numero di client prima di accept()
#define BUF_SIZE 4096 //dimensione del buffer per i comandi


//entra dopo il fork nel server e riceve comandi dal client tramite fd_client
//poi li esegue tramite fork e exec e rimanda indietro l'output tramite stdout e stderr
static void avvia_esecutore(int fd_client){

    char comando[BUF_SIZE];
    char risultato[BUF_SIZE * 4];

    signal(SIGTERM, SIG_DFL);

    while(1){
        //riceve il comando dal client
        memset(comando, 0, sizeof(comando));
        ssize_t n = recv(fd_client, comando, sizeof(comando)-1, 0);

        //se il client è disconnesso
        if (n<=0) {break;}

        comando[n] = '\0';

        //toglie un newline se presente
        size_t lunghezza = strlen(comando);
        if (lunghezza>0 && comando[lunghezza-1] == '\n'){
            comando[--lunghezza] = '\0';}

        //se il client immette il comando exit per uscire
        if (strcmp(comando, "exit") == 0){
            const char *addio = "__EXIT__\n";
            send(fd_client, addio, strlen(addio), 0);
            break;}

        //creo una pipe e il figlio scrive il suo stdout e stderr
        //il genitore legge l'output e lo invia al client
        int fd_pipe[2];
        if (pipe(fd_pipe) == -1){
            perror("esecutore: pipe");
            const char *errore = "ERRORE: pipe fallita\n";
            send(fd_client, errore, strlen(errore), 0);
            continue;
        }

        pid_t pid = fork();

        if(pid<0){
            perror("esecutore: fork");
            close(fd_pipe[0]);
            close(fd_pipe[1]);
            continue;}

        if(pid==0){

            //il figlio esegue il comando
            close(fd_pipe[0]);

            //indirizza stdout e stderr nella write end della pipe
            dup2(fd_pipe[1], STDOUT_FILENO);
            dup2(fd_pipe[1], STDERR_FILENO);
            close(fd_pipe[1]);

            //comando senza argomenti
            char *argv[] = {comando, NULL};
            execvp(comando, argv);

            fprintf(stderr, "Comando non trovato: %s\n", comando);
            exit(127);}


        //il genitore esecutore legge l'output e lo manda al client
        close(fd_pipe[1]);

        //prende tutti gli output dei figli
        memset(risultato, 0, sizeof(risultato));
        ssize_t totale = 0;
        ssize_t r;
        while ((r=read(fd_pipe[0], risultato+totale, sizeof(risultato)-1-totale))>0){
            totale+=r;
            if (totale>=(ssize_t)(sizeof(risultato)-1)){break;}
        }

        close(fd_pipe[0]);

        //aspetta i figli
        int stato;
        waitpid(pid, &stato, 0);

        //se il comando non restituisce nessun output
        if (totale==0){
            strcpy(risultato, "nessun output\n");
            totale=strlen(risultato);}

        //manda l'output al client
        char intestazione[32];
        snprintf(intestazione, sizeof(intestazione), "LEN:%zd\n", totale);
        send(fd_client, intestazione, strlen(intestazione), 0);
        send(fd_client, risultato, totale, 0);
    }

    close(fd_client);
    exit(0);
}


static volatile int continua=1;

static void gestore_sigterm(int sig)
{
    (void)sig;
    continua=0;
}

int main(void)
{
    //ignora SIGINT e SIGQUIT
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    //SIGTERM per chiudere il server
    struct sigaction sa_term = {0};
    sa_term.sa_handler = gestore_sigterm;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    sigaction(SIGTERM, &sa_term, NULL);

    //per evitare esecutori zombie vengono tolti in automatico dopo il comando exit
    struct sigaction sa_chld = {0};
    sa_chld.sa_handler = SIG_DFL;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa_chld, NULL);

    //per creare un socket tcp
    int fd_server = socket(AF_INET, SOCK_STREAM, 0);
    if(fd_server < 0){perror("socket"); exit(1);}

    //permettere il riutilizzo della porta 8080 per velocizzare un riaccesso
    int opzione = 1;
    setsockopt(fd_server, SOL_SOCKET, SO_REUSEADDR, &opzione, sizeof(opzione));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY };

    if(bind(fd_server, (struct sockaddr *)&addr, sizeof(addr))<0){
        perror("bind"); exit(1);}

    if(listen(fd_server, BACKLOG)<0){
        perror("listen"); exit(1);
    }

    printf("Server in ascolto nella porta: %d (PID=%d, PGID=%d).\n",
           PORT, getpid(), getpgrp());
    fflush(stdout);

    while(continua){
        struct sockaddr_in addr_client;
        socklen_t lunghezza_client = sizeof(addr_client);

        int fd_client = accept(fd_server,
                               (struct sockaddr *)&addr_client,
                               &lunghezza_client);
        if(fd_client < 0){
            if(errno == EINTR){break;} //SIGTERM
            perror("accept");
            continue;
        }

        printf("Nuovo client connesso %s (fd=%d).\n",
               inet_ntoa(addr_client.sin_addr), fd_client);
        fflush(stdout);

        //fork esecutore per il client
        pid_t pid = fork();
        if(pid<0){
            perror("fork");
            close(fd_client);
            continue;}

        if(pid==0){
            //il figlio diventa l'esecutore
            close(fd_server);
            avvia_esecutore(fd_client);
        }

        close(fd_client);
        printf("Esecutore PID=%d iniziato per il client.\n", pid);
        fflush(stdout);
    }

    //chiusura server
    printf("SIGTERM ricevuto, chiusura del server.\n");
    fflush(stdout);

    close(fd_server);

    //ordina a tutti i processi di terminare anche senza saperne il numero di attivi
    signal(SIGTERM, SIG_IGN);
    killpg(getpgrp(), SIGTERM);

    //aspetta tutti i figli esecutori che finiscano
    while (wait(NULL) > 0);

    printf("Tutti gli esecutori sono terminati.\n");
    return 0;
}
