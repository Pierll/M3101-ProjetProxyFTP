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
#define DEFAULT_TIMEOUT 500000 				//valeur du temps maximal que peut mettre un client en repondre (en ms) par defaut

int nbrClients = 0; //variable globale indiquant le nombre de client en cours
int descSockCOM; //variable globale indiquant le fd du client
int descSockSERV; //variable globale indiquant le fd du serveur



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
	   	puts("TIMEOUT...");
	   	return 0;
	 }
    
    int len = 0;
    ioctl(desc, FIONREAD, &len);
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
    memset(buffer, 0, sizeof(bufferC)); //nettoie le buffer sinon residus de memoire
	strncpy(buffer, bufferC, strlen(bufferC));
	return 1;    
}

void echange(int desc1, int desc2, int time1, int time2) {
	char buffer1[MAXBUFFERLEN] = {}; 
	char buffer2[MAXBUFFERLEN] = {}; 
	traiterSocket(desc1, buffer1, time1);
	write(desc2, buffer1, strlen(buffer1));
	traiterSocket(desc2, buffer2, time2);
	write(desc1, buffer2, strlen(buffer2));
}

int ecouterClient(char* commande) {
    
    if (chaineCommencePar(commande, "AUTH")) {
        write(descSockCOM, "534\n", sizeof("534\n")); //ne supporte pas l'extension de securite (RFC 2228)
    }

    if(chaineCommencePar(commande, "USER ")) {
        if (regCompare(commande, "..*@..*"))  {//verifie si la chaine est sous la forme nomlogin@nomserveur
        	/*PROCEDURE POUR LE LOGIN */
            int result;
		    char bufferC[MAXBUFFERLEN] = {};
		    char bufferS[MAXBUFFERLEN] = ""; //buffer pour envoyer le login
		    
            strtok(commande, " ");
            char* nomLogin = strtok(NULL, "@");
            char* nomServeur = strtok(NULL, "@");
            nomServeur[strlen(nomServeur)-2] = '\0'; //retire le saut de ligne

            if ((result = connect2Server(nomServeur, "21", &descSockSERV)) == -1) {
                write(descSockCOM, "530 Connexion impossible\n", sizeof("530 Connexion impossible\n")); //code "login failed"
                return 1;
            }

            printf("Desc : %d\n", descSockSERV);

			traiterSocket(descSockSERV, bufferC, -1);
			printf("Reponse serveur : %s\n", bufferC);
			
			strcpy(bufferS, "USER ");
			strncat(bufferS, nomLogin, sizeof(bufferS));
			strcat(bufferS, "\n");			
			printf("envoie login: %s", bufferS);
			/* envoie du nom d'utilisateur au serveur ftp */
			int len;
			if((len = write(descSockSERV, bufferS, strlen(bufferS))) == -1)  //envoie le login au serveur
				perror("write");
			traiterSocket(descSockSERV, bufferC, -1); //le serveur repond si le login est accepte ou pas
			
			/*reception de la reponse du serv ftp */
		    write(descSockCOM, bufferC, sizeof(bufferC)); //relais la reponse au client

			/* le client envoie le mdp au serv */
			echange(descSockCOM, descSockSERV, 180000, -1);
			/*echange avec SYST*/
			echange(descSockCOM, descSockSERV, 180000, -1);
			// le client est log !
        } else {
            write(descSockCOM, "530\n", sizeof("530\n")); //code "login failed"
            //write(descSockCOM, "221\n", sizeof("221\n")); //code "GOODBYE"
        }
    }

    return 0;
}

void traiterFils(int pidPere) {
    printf("[proxy] CLIENT %d CONNECTE\n", descSockCOM);
    int stop = 0;
    char buffer[MAXBUFFERLEN] = {};

    char* strBonjour = "220 BIENVENUE SUR LE PROXY FTP\n";
    strncpy(buffer, strBonjour, MAXBUFFERLEN);
    write(descSockCOM, buffer, sizeof(buffer)); //envoie le bonjour 220
	
    while (!stop) { //boucle principale
    	int retourPoll = traiterSocket(descSockCOM, buffer, -1);
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
              ecouterClient(buffer); 
        }
    }
}

int main() {
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
    gererSocket(descSockRDV, NULL); //on ferme le socket de RDV
}
