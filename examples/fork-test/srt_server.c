#include <srt/srt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#define PORT 9000

int run(char *command) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork (intermediate)");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // Parent process
        printf("[GRANDPARENT %d] waiting for grand-child process pid=%d to finish...\n",
                             getpid(),                               pid);
        waitpid(pid, NULL, 0);  // Wait for intermediate child
        printf("[GRANDPARENT] returning\n");
        return 0;
    }
    // Intermediate process
    //srt_cleanup();
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }
    pid_t grandchild_pid = fork();
    if (grandchild_pid < 0) {
        perror("fork (grandchild)");
        exit(EXIT_FAILURE);
    }
    if (grandchild_pid > 0) {
        printf("[PARENT %d] waiting for 10s with child process pid=%d ...\n",
                        getpid(),                                  grandchild_pid);
        // Intermediate process exits immediately
        sleep(10);
        printf("[PARENT] exitting\n");
        exit(0);
    }
    // Grandchild process
    // Redirect stdin to /dev/null
    printf("[CHILD %d] Preparing descriptors...\n", getpid());
    int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    } else {
        perror("open /dev/null");
    }
    // Redirect stdout to stderr
    dup2(STDERR_FILENO, STDOUT_FILENO);
    // Execute the command
    printf("[CHILD] Executing process '%s'...\n", command);
    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    // If execl fails
    perror("execl");
    exit(EXIT_FAILURE);
}

int main() {
    // Initialisation de la bibliothèque SRT
    if (srt_startup() != 0) {
        fprintf(stderr, "Error initializing SRT.\n");
        return 1;
    }

//    if (pthread_atfork(NULL, NULL, (void (*) ()) srt_cleanupAtFork) < 0)
//    {
//        fprintf(stderr, "Error registering srt_cleanup with phtread_atfork.\n");
//        return 1;
//    }
    // Création du socket SRT
    SRTSOCKET serv_sock = srt_create_socket();
    if (serv_sock == SRT_INVALID_SOCK) {
        fprintf(stderr, "Error creating SRT socket: %s\n", srt_getlasterror_str());
        return 1;
    }
    // Configuration de l'adresse
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(PORT);
    sa.sin_addr.s_addr = INADDR_ANY;
    // Liaison
    if (srt_bind(serv_sock, (struct sockaddr*)&sa, sizeof sa) == SRT_ERROR) {
        fprintf(stderr, "Error: srt_bind: %s\n", srt_getlasterror_str());
        return 1;
    }
    // Mise en écoute
    if (srt_listen(serv_sock, 5) == SRT_ERROR) {
        fprintf(stderr, "Error: srt_listen: %s\n", srt_getlasterror_str());
        return 1;
    }
    printf("SRT server is listening on port %d...\n", PORT);
    // Acceptation d'une connexion
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SRTSOCKET client_sock = srt_accept(serv_sock, (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock == SRT_INVALID_SOCK) {
        fprintf(stderr, "Error: srt_accept: %s\n", srt_getlasterror_str());
        return 1;
    }
    printf("Client connected via SRT !\n");
    // Exemple de réception (bloquant)
    char buffer[1500];
    int bytes = srt_recv(client_sock, buffer, sizeof(buffer));
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Message received: %s\n", buffer);
        const char resp [] = "We read you!";
        srt_send(client_sock, resp, (sizeof resp)-1);
    } else {
        printf("Error: reading from srt_recv: %s.\n", srt_getlasterror_str());
    }
    run("date > /tmp/res");
    printf("Server: sleep(1)\n");
    sleep(1);
    // Nettoyage
    printf("Server: closing SRT sockets\n");
    srt_close(client_sock);
    srt_close(serv_sock);
    printf("Server: cleanup\n");
    srt_cleanup();
    printf("Server: exit\n");
    return 0;
}
