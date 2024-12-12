#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_USERS 10
#define MAX_LEN 1000

typedef struct User {
    char nom[100];
    int socket;
} User;

User connected_users[MAX_USERS];
int user_count = 0;
pthread_mutex_t user_mutex = PTHREAD_MUTEX_INITIALIZER;

//Nouvelle utilisateur connecté
int add_user(const User *user) {
    pthread_mutex_lock(&user_mutex);
    if (user_count >= MAX_USERS) {
        pthread_mutex_unlock(&user_mutex);
        return -1;
    }
    connected_users[user_count++] = *user;
    pthread_mutex_unlock(&user_mutex);
    return 0;
}

//Déconnexion d'un utilisateur
void delete_user(const int socket) {
    pthread_mutex_lock(&user_mutex);
    for (int i = 0; i < user_count; ++i) {
        if (connected_users[i].socket == socket) {
            connected_users[i] = connected_users[user_count - 1];
            user_count--;
            break;
        }
    }
    pthread_mutex_unlock(&user_mutex);
}

// Fonction pour diffuser un message à tous les utilisateurs
void afficher_message(const char *message) {
    pthread_mutex_lock(&user_mutex);
    for (int i = 0; i < user_count; ++i) {
        if (send(connected_users[i].socket, message, strlen(message), 0) < 0) {
            perror("Error sending to the client");
        }
    }
    pthread_mutex_unlock(&user_mutex);
}

void *client_handler(void *arg) {
    const int socketClient = *(int *)arg;
    free(arg);

    User user;
    if (recv(socketClient, &user, sizeof(user), 0) <= 0) {
        close(socketClient);
        pthread_exit(NULL);
    }

    user.socket = socketClient;

    //Si trop de monde
    if (add_user(&user) < 0) {
        printf("Server is full, connection refused fo r %s\n", user.nom);
        close(socketClient);
        pthread_exit(NULL);
    }

    printf("\033[32m%s is connected.\033[0m\n", user.nom);
    //Affichage de la connection à tous les utilisateurs
    char connection_formatted_message[MAX_LEN];
    snprintf(connection_formatted_message, sizeof(connection_formatted_message), "\033[32m%s: %s is connected.\033[0m\n", "SERVEUR", user.nom);
    afficher_message(connection_formatted_message);

    char buffer[MAX_LEN];
    while (1) {
        const ssize_t bytes_received = recv(socketClient, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        buffer[bytes_received] = '\0';

        // Créer un message formaté avec une taille suffisante
        char formatted_message[MAX_LEN + sizeof(user.nom) + 10];
        snprintf(formatted_message, sizeof(formatted_message), "%s : %s", user.nom, buffer);

        printf("%s\n", formatted_message);

        // Diffuser le message à tous les utilisateurs
        afficher_message(formatted_message);
    }

    printf("\033[31m%s disconnected.\033[0m\n", user.nom);
    //Affichage des messages de déconnection 
    char disconnection_formatted_message[MAX_LEN];
    snprintf(disconnection_formatted_message, sizeof(disconnection_formatted_message), "\033[31m%s : %s disconnected.\033[0m", "SERVEUR", user.nom);
    afficher_message(disconnection_formatted_message);

    delete_user(socketClient);

    close(socketClient);
    pthread_exit(NULL);
}


int main() {
    const int socketServer = socket(AF_INET, SOCK_STREAM, 0);
    if (socketServer < 0) {
        perror("Error when creating the server socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrServer = {0};
    addrServer.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrServer.sin_family = AF_INET;
    addrServer.sin_port = htons(30001);

    if (bind(socketServer, (struct sockaddr *)&addrServer, sizeof(addrServer)) < 0) {
        perror("Binding Error");
        close(socketServer);
        exit(EXIT_FAILURE);
    }

    if (listen(socketServer, 10) < 0) {
        perror("Listening Error");
        close(socketServer);
        exit(EXIT_FAILURE);
    }

    printf("===== Server is open on port 300001 ====\n");

    while (1) {
        struct sockaddr_in addrClient;
        socklen_t addr_len = sizeof(addrClient);
        const int socketClient = accept(socketServer, (struct sockaddr *)&addrClient, &addr_len);

        if (socketClient < 0) {
            perror("Acceptation Error");
            continue;
        }

        int *arg = malloc(sizeof(int));
        *arg = socketClient;

        pthread_t thread;
        pthread_create(&thread, NULL, client_handler, arg);
        pthread_detach(thread);
    }
}
