#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <termios.h>

#define MAX_LEN 1000
char bufferCurrentMessage[MAX_LEN] = {0}; // Stocke le message en cours de saisie
int bufferLength = 0; // Longueur actuelle du message
int socketClient;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex pour synchronisation

typedef struct User {
    char nom[100];
} User;

// Fonction pour effacer la ligne courante
void clear_line() {
    printf("\r\033[K"); // Retour au début de la ligne et effacement
}

// Fonction pour configurer le terminal en mode non-canonique
void set_non_canonical_mode() {
    struct termios tattr;
    tcgetattr(STDIN_FILENO, &tattr);
    tattr.c_lflag &= ~(ICANON | ECHO); // Désactiver mode canonique et écho (=plus de regroupement par ligne et l'écriture du caractère n'est plus automatique)
    tattr.c_cc[VMIN] = 1;             // Lire un caractère à la fois
    tattr.c_cc[VTIME] = 0;            // Pas de délai
    tcsetattr(STDIN_FILENO, TCSANOW, &tattr);
}

// Fonction pour écouter les messages du serveur
void *listen_to_server() {
    char buffer[MAX_LEN];
    while (1) {
        const ssize_t reception = recv(socketClient, buffer, sizeof(buffer) - 1, 0);
        if (reception <= 0) {
            clear_line();
            printf("\nDisconnected from the server.\n");
            break;
        }
        buffer[reception] = '\0';

        pthread_mutex_lock(&mutex);
        clear_line(); // Efface la ligne courante (prompt)
        printf("%s\n", buffer); // Affiche le message reçu
        printf("> %s", bufferCurrentMessage); // Réaffiche le prompt et le message en cours
        fflush(stdout);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

// Fonction principale pour gérer l'entrée utilisateur
void handle_user_input() {
    while (1) {
        // Lire un caractère à la fois
        const char ch = getchar(); //Fonction dispo grâce à STDIO

        pthread_mutex_lock(&mutex);
        if (ch == '\n') { // Si l'utilisateur appuie sur Entrée
            bufferCurrentMessage[bufferLength] = '\0'; // Terminer le message
            if (bufferLength > 0) {
                if (send(socketClient, bufferCurrentMessage, bufferLength, 0) < 0) {
                    perror("Error sending message");
                }
                bufferLength = 0; // Réinitialiser le buffer
                memset(bufferCurrentMessage, 0, sizeof(bufferCurrentMessage));
            }
            clear_line();
            printf("> "); // Réaffiche le prompt vide
        } else if (ch == 127) { // Code ASCII de suppression
            if (bufferLength > 0) {
                bufferLength--; // Supprimer un caractère
                bufferCurrentMessage[bufferLength] = '\0';
                clear_line();
                printf("> %s", bufferCurrentMessage);
            }
        } else { // Ajouter le caractère au buffer
            if (bufferLength < MAX_LEN - 1) {
                bufferCurrentMessage[bufferLength++] = ch;
                bufferCurrentMessage[bufferLength] = '\0';
            }
            clear_line();
            printf("> %s", bufferCurrentMessage);
        }
        fflush(stdout);
        pthread_mutex_unlock(&mutex);
    }
}

// Saisie du nom d'utilisateur
void saisie_nom(User *user) {
    printf("Enter your name : ");
    fgets(user->nom, sizeof(user->nom), stdin);
    user->nom[strcspn(user->nom, "\n")] = '\0'; // Retirer le \n final
}

int main() {
    User user;
    saisie_nom(&user);

    // Création du socket
    socketClient = socket(AF_INET, SOCK_STREAM, 0);
    if (socketClient < 0) {
        perror("Error creating the socket");
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse du serveur
    struct sockaddr_in addrClient = {0};
    addrClient.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrClient.sin_family = AF_INET;
    addrClient.sin_port = htons(30001);

    // Connexion au serveur
    if (connect(socketClient, (struct sockaddr *)&addrClient, sizeof(addrClient)) < 0) {
        perror("Connection Error");
        close(socketClient);
        exit(EXIT_FAILURE);
    }

    // Envoi du nom d'utilisateur au serveur
    if (send(socketClient, &user, sizeof(user), 0) < 0) {
        perror("Error sending the user's name");
        close(socketClient);
        exit(EXIT_FAILURE);
    }

    // Mode non-canonique
    set_non_canonical_mode();

    // Création du thread pour écouter les messages du serveur
    pthread_t listen_thread;
    if (pthread_create(&listen_thread, NULL, listen_to_server, NULL) != 0) {
        perror("Error when creating the thread");
        close(socketClient);
        exit(EXIT_FAILURE);
    }

    // Gestion des entrées utilisateur
    handle_user_input();

    pthread_join(listen_thread, NULL);
    close(socketClient);

    printf("\nDisconnecting...\n");
    return 0;
}
