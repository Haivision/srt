#include <srt/srt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#define PORT 9000

void *run(void *arg) {
    char *command = (char*)arg;
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
        printf("[PARENT %d] waiting for 2s with child process pid=%d ...\n",
                        getpid(),                                  grandchild_pid);
        // Intermediate process exits immediately
        sleep(2);
        printf("[PARENT] exiting\n");
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
    return 0;
}

void *server_thread_f(void *arg) {
    SRTSOCKET serv_sock = srt_create_socket();
    if (serv_sock == SRT_INVALID_SOCK) {
        fprintf(stderr, "Error creating SRT socket: %s\n", srt_getlasterror_str());
        return 0;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(PORT);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (srt_bind(serv_sock, (struct sockaddr*)&sa, sizeof sa) == SRT_ERROR) {
        fprintf(stderr, "Error: srt_bind: %s\n", srt_getlasterror_str());
        return 0;
    }
    if (srt_listen(serv_sock, 5) == SRT_ERROR) {
        fprintf(stderr, "Error: srt_listen: %s\n", srt_getlasterror_str());
        return 0;
    }
    printf("SRT server is listening on port %d...\n", PORT);
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SRTSOCKET client_sock = srt_accept(serv_sock, (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock == SRT_INVALID_SOCK) {
        fprintf(stderr, "Error: srt_accept: %s\n", srt_getlasterror_str());
        return 0;
    }
    printf("Client connected via SRT !\n");
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
    // run("date > /tmp/res");
    printf("Server: sleep(1)\n");
    sleep(1);
    // Nettoyage
    printf("Server: closing SRT sockets\n");
    srt_close(client_sock);
    srt_close(serv_sock);
    return 0;
}

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
void *client_thread_f(void *arg) {
    SRTSOCKET client_sock = srt_create_socket();
    if (client_sock == SRT_INVALID_SOCK) {
        fprintf(stderr, "Error creating a socket: %s\n", srt_getlasterror_str());
        return 0;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &sa.sin_addr);
    if (srt_connect(client_sock, (struct sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) {
        fprintf(stderr, "Error: srt_connect: %s\n", srt_getlasterror_str());
        return 0;
    }
    printf("Connected to SRT server %s:%d\n", SERVER_IP, SERVER_PORT);
    const char* message = "Hello from SRT client!";
    int bytes = srt_send(client_sock, message, strlen(message));
    if (bytes == SRT_ERROR) {
        fprintf(stderr, "Sending error: %s\n", srt_getlasterror_str());
    } else {
        printf("Message sent: %s\n", message);
    }

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
    return 0;
}

int main() {
    if (srt_startup() != 0) {
        fprintf(stderr, "Error initializing SRT.\n");
        return 1;
    }

    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, server_thread_f, NULL) != 0) {
        perror("failed to create server thread");
        exit(EXIT_FAILURE);
    }

    pthread_t client_thread;
    if (pthread_create(&client_thread, NULL, client_thread_f, NULL) != 0) {
        perror("failed to create client thread");
        exit(EXIT_FAILURE);
    }

    const int run_threads_count = 10;
    pthread_t run_threads[run_threads_count];
    for (int i = 0; i < run_threads_count; ++i) {
        if (pthread_create(&run_threads[i], NULL, run, "date > /tmp/res") != 0) {
            perror("failed to create run thread");
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < run_threads_count; ++i) {
        if (pthread_join(run_threads[i], NULL) != 0) {
            perror("failed joining run thread");
        }
    }

    pthread_join(server_thread, NULL);
    pthread_join(client_thread, NULL);

    printf("Server: cleanup\n");
    srt_cleanup();
    printf("Server: exit\n");
    return 0;
}
