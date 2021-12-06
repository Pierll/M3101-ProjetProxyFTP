#include  <stdio.h>
#include  <stdlib.h>
#include  <sys/socket.h>
#include <wait.h>
#include  <netdb.h>
#include  <string.h>
#include  <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include "./simpleSocketAPI.h"


#define SERVADDR "127.0.0.1"        // Définition de l'adresse IP d'écoute
#define SERVPORT "0"                // Définition du port d'écoute, si 0 port choisi dynamiquement
#define LISTENLEN 1                 // Taille de la file des demandes de connexion
#define MAXBUFFERLEN 1024           // Taille du tampon pour les échanges de données
#define MAXHOSTLEN 64               // Taille d'un nom de machine
#define MAXPORTLEN 64               // Taille d'un numéro de port
#define MAXCLIENT 16 				// Nombre maximum de clients FTP
#define TIMEOUT 5000 				//valeur du temps maximal que peut mettre un client en repondre (en ms)

int gererSocket(int mode, socklen_t* len) { //si mode = -1 alors creation d'un nv socket, sinon le mode est considere comme le num du socket et on le ferme
    if (mode == -1) {
        int ecode;                       // Code retour des fonctions
        char serverAddr[MAXHOSTLEN];     // Adresse du serveur
        char serverPort[MAXPORTLEN];     // Port du server
        int descSockRDV;                 // Descripteur de socket de rendez-vous#
        struct addrinfo hints;           // Contrôle la fonction getaddrinfo
        struct addrinfo *res;            // Contient le résultat de la fonction getaddrinfo
        struct sockaddr_storage myinfo;  // Informations sur la connexion de RDV
        // Initialisation de la socket de RDV IPv4/TCP
        descSockRDV = socket(AF_INET, SOCK_STREAM, 0);
        if (descSockRDV == -1) {
            perror("Erreur création socket RDV\n");
            exit(2);
        }
        // Publication de la socket au niveau du système
        // Assignation d'une adresse IP et un numéro de port
        // Mise à zéro de hints
        memset(&hints, 0, sizeof(hints));
        // Initialisation de hints
        hints.ai_flags = AI_PASSIVE;      // mode serveur, nous allons utiliser la fonction bind
        hints.ai_socktype = SOCK_STREAM;  // TCP
        hints.ai_family = AF_INET;        // seules les adresses IPv4 seront présentées par
        // la fonction getaddrinfo

        // Récupération des informations du serveur
        ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
        if (ecode) {
            fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(ecode));
            exit(EXIT_FAILURE);
        }
        // Publication de la socket
        ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
        if (ecode == -1) {
            perror("Erreur liaison de la socket de RDV");
            exit(3);
        }
        // Nous n'avons plus besoin de cette liste chainée addrinfo
        freeaddrinfo(res);

        // Récupération du nom de la machine et du numéro de port pour affichage à l'écran
        *len=sizeof(struct sockaddr_storage);
        ecode=getsockname(descSockRDV, (struct sockaddr *) &myinfo, len);
        if (ecode == -1)
        {
            perror("SERVEUR: getsockname");
            exit(4);
        }
        ecode = getnameinfo((struct sockaddr*)&myinfo, sizeof(myinfo), serverAddr,MAXHOSTLEN,
                            serverPort, MAXPORTLEN, NI_NUMERICHOST | NI_NUMERICSERV);
        if (ecode != 0) {
            fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(ecode));
            exit(4);
        }
        printf("L'adresse d'ecoute est: %s\n", serverAddr);
        printf("Le port d'ecoute est: %s\n", serverPort);

        // Definition de la taille du tampon contenant les demandes de connexion
        ecode = listen(descSockRDV, LISTENLEN);
        if (ecode == -1) {
            perror("Erreur initialisation buffer d'écoute");
            exit(5);
        }
        *len = sizeof(struct sockaddr_storage);
        return descSockRDV;
    } else {
        close(mode);
    }
    return 0;

}

int ecouterClient(int descSockCOM, struct pollfd* fd) {
    char bufferC[MAXBUFFERLEN] = {};
    ssize_t rc;
    while ( fd->revents & POLLIN ) {//revents = l'event retourne
        rc = read(descSockCOM, bufferC, sizeof(bufferC));

        if ( rc <= 0 ) { //communication interrompu
            return -1;
        }

        poll(fd, 1, 0); //sinon le programme loop a l'infinie (?)
    }
    printf("[client %d] Lis %d bytes: %s\n", descSockCOM, (int)rc, bufferC);

    write(descSockCOM, bufferC, sizeof(bufferC));
    memset(bufferC, 0, sizeof(bufferC)); //nettoie le buffer sinon residus de memoire
    return 0;
}

void traiterFils(int descSockCOM) {
    printf("[serveur] CLIENT %d CONNECTE\n", descSockCOM);
    int stop = 0;
    char buffer[MAXBUFFERLEN] = {};

    char* strtest = "220 BLABLABLA\n";
    strncpy(buffer, strtest, MAXBUFFERLEN);
    struct pollfd fd = {
        .fd = descSockCOM,
        .events = POLLIN
    };

    while (!stop) {
        int retourPoll = poll(&fd, 1, TIMEOUT); // attend 5 secondes
        if (retourPoll == 0) { //si le client ne repond rien durant le laps de temps
            printf("[serveur] timeout client %d\n", descSockCOM);
        } else {
            if (ecouterClient(descSockCOM, &fd) == -1) {
                printf("[serveur] communication avec le client %d interrompue\n", descSockCOM);
                return;
            }
        }
    }


}

int main() {
    int descSockCOM;      // Descripteur de socket de communication
    int clientConnectes = 0; 		 //indique le nombre de clients connectes
    socklen_t len;                   // Variable utilisée pour stocker les longueurs des structures de socket#
    struct sockaddr_storage from;    // Informations sur le client connecté
    int descSockRDV = gererSocket(-1, &len); //cree le socket du proxy
    int bufferR;

    while (1 == 1) {
        descSockCOM = accept(descSockRDV, (struct sockaddr *) &from, &len);
        if (descSockCOM == -1) {
            perror("Erreur accept\n");
            exit(6);
        }
        pid_t idProc;
        idProc = fork(); //on forke chaque demande de connection dans un nv processus
        switch (idProc) {
        case -1:
            printf("Erreur lors de la creation du processus fils\n");
            exit(EXIT_FAILURE);
        case 0: //on passe le relai au proc fils
            bufferR = 0;
            traiterFils(descSockCOM);
            printf("[serveur] CLIENT %d DECONNECTE\n", descSockCOM);
            close(descSockCOM);
            exit(EXIT_SUCCESS);
        }
        puts("ACTION");

        /* suite du processus pere */
        /* attente de la terminaison de tous les fils */

        //Fermeture de la connexion
    }
    close(descSockCOM);
    gererSocket(descSockRDV, NULL); //on ferme le socket de RDV
}