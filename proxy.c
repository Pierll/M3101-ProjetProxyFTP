#include <stdio.h>
#include <stdlib.h>
#include <wait.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <regex.h>
#include "simpleSocketAPI.h"

#define SERVPORT 0                // Définition du port d'écoute, si 0 port choisi dynamiquement
#define MAXBUFFERLEN 4096           // Taille du tampon pour les échanges de données
#define MAXCLIENT 16 				// Nombre maximum de clients FTP
#define DEFAULT_TIMEOUT 50000 				//valeur du temps maximal que peut mettre un client en repondre (en ms) par defaut

int nbrClients = 0; //variable globale indiquant le nombre de client en cours
int descSockCOM = -1; //variable globale indiquant le fd du client
int descSockSERV = -1; //variable globale indiquant le fd du serveur
int pidPere; //variable globale indiquant le pid du pere
char serverName[MAXBUFFERLEN];

void fermeture() {
    close(descSockCOM);
    if (kill(pidPere, SIGUSR1) == -1) { //envoie le signal SIGUSR 1 au pere pour signaler sa fin
        perror("kill");
        exit(EXIT_FAILURE);
    }
}

int regCompare(char* chaine, char *pattern) { //comparer 1 chaines avec une expression reguliere
    regex_t reg;
    int r;

    if (regcomp(&reg, pattern, REG_NOSUB | REG_EXTENDED) != 0) {
        fprintf(stderr, "La compilation de regex a echoue");
        exit(EXIT_FAILURE);
    }

    r = regexec(&reg, chaine, 0, NULL, 0);

    switch (r) {
    case 0: //la chaine correspond au motif
        return 1;
    case REG_NOMATCH: //ne correspond pas au motif
        return 0;
    default: //erreur
        fprintf(stderr, "Erreur de comparaison regex");
        exit(EXIT_FAILURE);
    }

}

int chaineCommencePar(char* chaine, char* comparaison) {
    if (strlen(chaine) < strlen(comparaison))
        return 0;
    for (unsigned int i = 0; i < strlen(comparaison); i++) {
        if (chaine[i] != comparaison[i])
            return 0;
    }
    return 1;
}

void traitementSignal(int idSignal) {
    if (idSignal != SIGUSR1) {
        perror("id signal: ");
        exit(EXIT_FAILURE);
    }
    nbrClients--;

}

int traiterSocket(int desc, char buffer[MAXBUFFERLEN], int timeout) {
    int t = 0; //t la valeur du temps maximal que peut mettre un client en repondre (en ms)

    if (timeout == -1) //si le parametre fournis est -1 alors la valeur du timeout est celle par default
        t = DEFAULT_TIMEOUT;
    else
        t = timeout;
    char bufferC[MAXBUFFERLEN] = {};

    struct pollfd fd = {
        .fd = desc,
        .events = POLLIN
    };
    int retourPoll = poll(&fd, 1, t); // ecoute pendant t millisecondes
    if (retourPoll == 0) { //si le client ne repond rien
        printf("timeout ");
        return 0;
    }

    int len = 0;
    ioctl(desc, FIONREAD, &len); //ETAT DU SOCKET
    if (len > 0) {
        len = read(desc, bufferC, len);
        if ( len <= 0 ) { //communication interrompu
            perror("read");
            return -1;
        }
    }
    if (len <= 0) { //communication interrompu
        puts("DECONNEXION DU CLIENT");
        return -1;
    }
    //printf("reception: %s", bufferC);
    memset(buffer, 0, sizeof(bufferC)); //nettoie le buffer sinon residus de memoire
    strncpy(buffer, bufferC, strlen(bufferC));
    return 1;
}

void transfert(int desc1, int desc2) { //pour transferer un flux de donnees
    char buffer[MAXBUFFERLEN-1];
    do {
        if (read(desc1, buffer, MAXBUFFERLEN-1) <= 0 ) {
            break;   //SI plus rien a lire on break la boucle while
        }
        printf("%s",buffer);
        write(desc2, buffer, strlen(buffer));
        bzero(buffer, MAXBUFFERLEN -1 );
    } while (1);

}

void echange2C(int desc1, int desc2, int time1, int time2) { //echange en 2 fois
    char buffer1[MAXBUFFERLEN] = {};
    char buffer2[MAXBUFFERLEN] = {};
    traiterSocket(desc1, buffer1, time1);
    write(desc2, buffer1, strlen(buffer1));
    traiterSocket(desc2, buffer2, time2);
    write(desc1, buffer2, strlen(buffer2));
}

void echange1C(int desc1, int desc2, int time, char* buffer) {
    char buffer1[MAXBUFFERLEN] = {};
    write(desc1, buffer, strlen(buffer));
    traiterSocket(desc1, buffer1, time);
    write(desc2, buffer1, strlen(buffer1));
}

int ecouterClient(char* commande) {

    if (chaineCommencePar(commande, "AUTH ")) {
        write(descSockCOM, "534\n", strlen("534\n")); //ne supporte pas l'extension de securite (RFC 2228)
    } else if(chaineCommencePar(commande, "USER ")) {
        if (regCompare(commande, "..*@..*"))  {//verifie si la chaine est sous la forme nomlogin@nomserveur avec un regex
            /*PROCEDURE POUR LE LOGIN */
            int result;
            char bufferC[MAXBUFFERLEN] = {};
            char bufferS[MAXBUFFERLEN] = ""; //buffer pour envoyer le login

            strtok(commande, " ");
            char* nomLogin = strtok(NULL, "@");
            char* nomServeur = strtok(NULL, "@");
            nomServeur[strlen(nomServeur)-2] = '\0'; //retire le saut de ligne

            if ((result = connect2Server(nomServeur, 21, &descSockSERV)) == -1) {
                write(descSockCOM, "530 Connexion impossible\n", strlen("530 Connexion impossible\n")); //code "login failed"
                return 1;
            }
            strncpy(serverName, nomServeur, strlen(nomServeur));
            traiterSocket(descSockSERV, bufferC, -1); //il nous envoie 220

            strcpy(bufferS, "USER ");
            strncat(bufferS, nomLogin, sizeof(bufferS));
            strcat(bufferS, "\n"); //CREE la chaine USER ""\n
            /* envoie du nom d'utilisateur au serveur ftp */
            int len;
            if((len = write(descSockSERV, bufferS, strlen(bufferS))) == -1)  //envoie le login au serveur
                perror("write");
            traiterSocket(descSockSERV, bufferC, -1); //le serveur repond si le login est accepte ou pas

            /*reception de la reponse du serv ftp */
            write(descSockCOM, bufferC, sizeof(bufferC)); //relais la reponse au client

            /* le client envoie le mdp au serv */
            echange2C(descSockCOM, descSockSERV, 180000, -1);
            /*echange avec SYST*/
            echange2C(descSockCOM, descSockSERV, 180000, -1);
            // traiterDataSocket(descSockSERV, descSockCOM, bufferC, -1);
            // le client est log !
        } else {
            puts("[proxy] echec du login !");
            write(descSockCOM, "530\n", strlen("530\n")); //code "login failed"
            write(descSockCOM, "221\n", strlen("221\n")); //code "GOODBYE"
            fermeture();
            exit(EXIT_SUCCESS);
        }
    } else if (chaineCommencePar(commande, "PORT ")) { //si le client veut initier une connexion passive
        char buf[MAXBUFFERLEN] = {};
        int descSockRCV; //le socket que le serveur ouvre pour envoyer la liste des fichiers
        int descSockSND; //le socket que le client ouvre pour recevoir la liste des fichiers
        int tmp; //variable qui sert just a recevoir les donnees inutiles de sscanf
        int p1, p2; //les ports de la commande PORT
        int portSND, portRCV; //le port du client et le port du serveur
        sscanf(commande, "PORT %d,%d,%d,%d,%d,%d", &tmp, &tmp, &tmp, &tmp, &p1, &p2);
        portSND = p1*256+p2; //calcul du port (voir doc ftp)

        write(descSockSERV, "EPSV\n", strlen("EPSV\n")); //demande le port du passif au serveur
        traiterSocket(descSockSERV, buf, -1); //recoie la reponse du serveur de EPSV
        sscanf(buf, "229 Entering Extended Passive Mode (|||%d|)", &portRCV);

        write(descSockCOM, "200 PORT OK\n", strlen("200 PORT OK\n")); //signale au client que sa demande PORT a ete accepte

        traiterSocket(descSockCOM, buf, -1); // recoie commande (client)
        if (chaineCommencePar(buf, "LIST")) {
            connect2Server(serverName, portRCV, &descSockRCV); //connexion au port de donnees
            write(descSockSERV, "LIST\n", strlen("LIST\n")); //demande au serveur d'envoyer les donnees
            traiterSocket(descSockSERV, buf, -1); //recoie 220 bonjour (serveur)

            connect2Server("127.0.0.1", portSND, &descSockSND); //se connecte au client pour envoyer les donnees
            write(descSockCOM, "150 LISTING...\n", strlen("150 LISTING...\n")); //dit au client de se preparer a recevoir les donnees

            transfert(descSockRCV, descSockSND);
            close(descSockSND);
            close(descSockRCV);

            traiterSocket(descSockSERV, buf, -1); //recoie 226 (serveur)
            write(descSockCOM, "226 OK\n", strlen("226 OK\n"));
        }

    } else {
        echange1C(descSockSERV, descSockCOM, -1, commande);
    }


    return 0;
}

void traiterFils(int pidPere_p) {
    pidPere = pidPere_p;
    printf("[proxy] CLIENT %d CONNECTE\n", descSockCOM);
    char buffer[MAXBUFFERLEN] = {};

    char* strBonjour = "220 BIENVENUE SUR LE PROXY FTP\n";
    strncpy(buffer, strBonjour, MAXBUFFERLEN);
    write(descSockCOM, buffer, sizeof(buffer)); //envoie le bonjour 220

    while (1) { //boucle principale
        int retourPoll = traiterSocket(descSockCOM, buffer, -1);
        if (retourPoll == 0) { //si le client ne repond rien durant le laps de temps
            printf("[proxy] timeout client %d\n", descSockCOM);
        } else if (retourPoll == -1) {
            printf("[proxy] communication avec le client %d interrompue\n", descSockCOM);
            fermeture();
            return;
        } else {
            ecouterClient(buffer);
        }
    }
}


int main() {
    socklen_t len;                   // Variable utilisée pour stocker les longueurs des structures de socket
    struct sockaddr_storage from;    // Informations sur le client connecté
    int descSockRDV = gererSocket(-1, &len, SERVPORT); //cree le socket du proxy
    int pidPere = getpid(); //pid du pere

    signal(SIGUSR1, traitementSignal); //pour recevoir le signal SIGUSR1 du fils

    /*Programme principal*/
    while (1 == 1) {
        printf("Nombre clients : %d\n", nbrClients);
        descSockCOM = accept(descSockRDV, (struct sockaddr *) &from, &len); //attend la connexion
        if (nbrClients >= MAXCLIENT) {
            printf("[proxy] le nombre maximal de clients a ete atteint : %d\n", nbrClients);
        } else {
            if (descSockCOM == -1) {
                perror("Erreur accept\n");
                exit(6);
            }
            nbrClients++;

            pid_t idProc;
            idProc = fork(); //on forke chaque demande de connection dans un nv processus
            switch (idProc) {
            case -1:
                printf("[proxy] Erreur lors de la creation du processus fils\n");
                exit(EXIT_FAILURE);
            case 0: //on passe le relai au proc fils
                traiterFils(pidPere);
                printf("[proxy] CLIENT %d DECONNECTE\n", descSockCOM);
                close(descSockCOM);
                exit(EXIT_SUCCESS);
            }
        }


        /* suite du processus pere */
        /* attente de la terminaison de tous les fils */

        //Fermeture de la connexion
    }
    close(descSockCOM);
    gererSocket(descSockRDV, NULL, 0); //on ferme le socket de RDV
}