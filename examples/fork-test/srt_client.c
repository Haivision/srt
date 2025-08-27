#include <srt/srt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
int main() {
    // Initialisation de la bibliothèque SRT
    if (srt_startup() != 0) {
        fprintf(stderr, "Error initializing SRT.\n");
        return 1;
    }
    // Création du socket
    SRTSOCKET client_sock = srt_create_socket();
    if (client_sock == SRT_INVALID_SOCK) {
        fprintf(stderr, "Error creating a socket: %s\n", srt_getlasterror_str());
        return 1;
    }
    // Configuration de l'adresse du serveur
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &sa.sin_addr);
    // Connexion
    if (srt_connect(client_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) {
        fprintf(stderr, "Error: srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }
    printf("Connected to SRT server %s:%d\n", SERVER_IP, SERVER_PORT);
    // Envoi d'un message
    const char* message = "Hello from SRT client!";
    int bytes = srt_send(client_sock, message, strlen(message));
    if (bytes == SRT_ERROR) {
        fprintf(stderr, "Sending error: %s\n", srt_getlasterror_str());
    } else {
        printf("Message sent: %s\n", message);
    }
    // Nettoyage

    while (1)
    {
        char buffer[1500];
        int nb = srt_recv(client_sock, buffer, sizeof(buffer));
        if (nb <= 0)
        {
            printf("Closed from the server !\n");
            srt_close(client_sock);
            break;
        }
        buffer[nb] = 0;
        printf("Server has sent: %s\n", buffer);
    }
    srt_cleanup();
    return 0;
}

