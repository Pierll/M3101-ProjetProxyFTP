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

#define MAXBUFFERLEN 1024           // Taille du tampon pour les échanges de données
#define MAXCLIENT 16 				// Nombre maximum de clients FTP
#define TIMEOUT 5000 				//valeur du temps maximal que peut mettre un client en repondre (en ms)

int nbrClients = 0; //variable globale indiquant le nombre de client en cours

int regCompare(char* chaine, char *pattern) { //comparer 2 chaines par regex
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

int traiterSocket(int desc, char buffer[MAXBUFFERLEN]) {
    char bufferC[MAXBUFFERLEN] = {};

	struct pollfd fd = {
        .fd = desc,
        .events = POLLIN
    };
	int retourPoll = poll(&fd, 1, TIMEOUT); // ecoute pendant 5 secondes
    if (retourPoll == 0) {
	   	puts("TIMEOUT...");
	   	return 0;
	 }
    
    int len = 0;
    ioctl(desc, FIONREAD, &len);
    if (len > 0) {
      len = read(desc, bufferC, len);
        if ( len <= 0 ) { //communication interrompu
            return -1;
        }
    }
    if (len <= 0) { //communication interrompu
    	puts("DECONNEXION DU CLIENT");
    	return -1;
    }
    memset(buffer, 0, sizeof(bufferC)); //nettoie le buffer sinon residus de memoire
	strncpy(buffer, bufferC, strlen(bufferC));
	return 1;    
}

int ecouterClient(int descSockCOM, char* commande) {
    
    if (chaineCommencePar(commande, "AUTH")) {
        write(descSockCOM, "534\n", sizeof("534\n")); //ne supporte pas l'extension de securite (RFC 2228)
    }

    if(chaineCommencePar(commande, "USER ")) {
        if (regCompare(commande, "..*@..*"))  {//verifie si la chaine est sous la forme nomlogin@nomserveur
            int descSockSERV;

            /*DeBUG*/
            strtok(commande, " ");
            int result;
            char* nomLogin = strtok(NULL, "@");
            char* nomServeur = strtok(NULL, "@");
            nomServeur[strlen(nomServeur)-2] = '\0'; //retire le saut de ligne

            if ((result = connect2Server(nomServeur, "21", &descSockSERV)) == -1) {
                write(descSockCOM, "530 Connexion impossible\n", sizeof("530 Connexion impossible\n")); //code "login failed"
                return 1;
            }

            printf("Desc : %d\n", descSockSERV);
			char bufferC[MAXBUFFERLEN] = {};
			traiterSocket(descSockSERV, bufferC);
			printf("Reponse serveur : %s\n", bufferC);
            /* FIN CODE TEMPORAIRE */
        } else {
            write(descSockCOM, "530\n", sizeof("530\n")); //code "login failed"
            //write(descSockCOM, "221\n", sizeof("221\n")); //code "GOODBYE"
        }
    }

    return 0;
}

void traiterFils(int descSockCOM, int pidPere) {
    printf("[proxy] CLIENT %d CONNECTE\n", descSockCOM);
    int stop = 0;
    char buffer[MAXBUFFERLEN] = {};

    char* strBonjour = "220 BIENVENUE SUR LE PROXY FTP\n";
    strncpy(buffer, strBonjour, MAXBUFFERLEN);
    write(descSockCOM, buffer, sizeof(buffer)); //envoie le bonjour 220
	
    while (!stop) { //boucle principale
    	int retourPoll = traiterSocket(descSockCOM, buffer);
    	printf("je lis %s\n", buffer);
        if (retourPoll == 0) { //si le client ne repond rien durant le laps de temps
            printf("[proxy] timeout client %d\n", descSockCOM);
        } else if (retourPoll == -1) {
        	 printf("[proxy] communication avec le client %d interrompue\n", descSockCOM);
             if (kill(pidPere, SIGUSR1) == -1) { //envoie le signal SIGUSR 1 au pere pour signaler sa fin
                 perror("kill");
                 exit(EXIT_FAILURE);
             }
             return;
        } else {
              ecouterClient(descSockCOM, buffer); 
        }
    }
}

int main() {
    int descSockCOM;      // Descripteur de socket de communication
    socklen_t len;                   // Variable utilisée pour stocker les longueurs des structures de socket#
    struct sockaddr_storage from;    // Informations sur le client connecté
    int descSockRDV = gererSocket(-1, &len); //cree le socket du proxy
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
                traiterFils(descSockCOM, pidPere);
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
    gererSocket(descSockRDV, NULL); //on ferme le socket de RDV
}
