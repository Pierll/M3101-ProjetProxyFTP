#include  <stdio.h>
#include  <stdlib.h>
#include  <sys/types.h>
#include  <netdb.h>
#include  <string.h>
#include  <unistd.h>
#include  <stdbool.h>
#include "simpleSocketAPI.h"

#define SERVADDR "127.0.0.1"        // Définition de l'adresse IP d'écoute
#define SERVPORT "0"                // Définition du port d'écoute, si 0 port choisi dynamiquement
#define LISTENLEN 1                 // Taille de la file des demandes de connexion
#define MAXHOSTLEN 64               // Taille d'un nom de machine
#define MAXPORTLEN 64               // Taille d'un numéro de port

int connect2Server(const char *serverName, const char *port, int *descSock){

    int ecode;                     // Retour des fonctions
	struct addrinfo *res,*resPtr;  // Résultat de la fonction getaddrinfo
	struct addrinfo hints;		   // Structure pour contrôler getaddrinfo
	bool isConnected;      // booléen indiquant que l'on est bien connecté
    
    // Initialisation de hints
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;  // TCP
	hints.ai_family = AF_UNSPEC;      // les adresses IPv4 et IPv6 seront présentées par 
				                      // la fonction getaddrinfo

	//Récupération des informations sur le serveur
	
	ecode = getaddrinfo(serverName,port,&hints,&res);
	if (ecode) {
		printf("erreur sur NOM: %s\n", serverName);
		fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(ecode));
		return -1;
	} else {
		printf("Succes de la connexion sur %s\n", serverName);
	}

	resPtr = res;

	isConnected = false;

	while(!isConnected && resPtr!=NULL){

		//Création de la socket IPv4/TCP
		*descSock = socket(resPtr->ai_family, resPtr->ai_socktype, resPtr->ai_protocol);
		if (*descSock == -1) {
			perror("Erreur creation socket");
			return -1;
		}
  
  		//Connexion au serveur
		ecode = connect(*descSock, resPtr->ai_addr, resPtr->ai_addrlen);
		if (ecode == -1) {
			resPtr = resPtr->ai_next;    		
			close(*descSock);	
		}
		// On a pu se connecter
		else isConnected = true;
	}
	freeaddrinfo(res);
	
	// On retourne -1 si pas possible de se connecter
	if (!isConnected){
		perror("Connexion impossible");
		return -1;
	}

	//On retourne 0 si on a pu établir la connexion TCP
    return 0;
}

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