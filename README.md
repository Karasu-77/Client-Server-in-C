#Progetto d'esame, un'applicazione client-server.

'''bash

#./progetto.sh (compila e fa partire il server)
#./progetto.sh client (inizializza un client su localhost)

set -e #se qualcosa va storto lo script si ferma

echo "Compilazione server.c"
gcc -Wall -Wextra -o server server.c #per maggiore sicurezza
echo "Server ok"

echo "Compilazione client.c"
gcc -Wall -Wextra -o client client.c #per maggiore sicurezza
echo "Client ok"

if [ "$1" = "client" ]; then #verifica che il primo aargomento dello script sia client
    SERVER_IP="${2:-127.0.0.1}"
    echo "Inizializzazione client, connesso su $SERVER_IP"
    ./client "$SERVER_IP"
else
    echo ""
    echo "Inizializzazione server"
    echo "Per terminare: kill -TERM \$(cat server.pid)"
    echo ""
    ./server & #il server parte in background e salva il suo PID
    echo $! > server.pid 
    echo "Server PID: $(cat server.pid)"
    echo ""
    echo "Apri un altro terminale e runna: ./client"
    wait #blocca lo script finché il server non termina
fi

'''
