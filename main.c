#include <stdio.h>
#include <stdlib.h>
#include "SerialManager.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>

#define USB 1
#define std_out 1
#define msgButton1 ">TOGGLE STATE:0"
#define msgButton2 ">TOGGLE STATE:1"
#define msgButton3 ">TOGGLE STATE:2"
#define msgButton4 ">TOGGLE STATE:3"
#define header ">OUTS:" // primera parte del mensaje de estados
#define bSize 16
#define ADRESS "127.0.0.1"
#define PORT 10000
#define RATE 115200
#define ST_OFFSET 7
#define S_EOF 0

static pthread_t inet_thread;
static int s_fd;
static int new_fd;

/* handler para sigint y sigterm */
void sigint_sigterm_handler(int sig) 
{
	write(std_out, "Cerrando...\n", 13); // mensaje de SIGINT/SIGTERM recibido
	if(pthread_cancel(inet_thread) != 0)
	{
		perror("pthread_cancel");
		exit(1);
	}
	if(pthread_join (inet_thread, NULL) != 0)
	{
		perror("pthread_join");
		exit(1);
	}
	close(new_fd);
	close(s_fd);
	serial_close();
	exit(0);
}

/* función para bloquear señales */
void bloquearSign(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT); // se bloquea SIGINT
	sigaddset(&set, SIGTERM); // se bloquea SIGTERM
	if(pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
	{
		perror("pthread_sigmask");
		exit(1);
	}
}

/* función para desbloquear señales */
void desbloquearSign(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT); // se desbloquea SIGINT
	sigaddset(&set, SIGTERM); // se desbloquea SIGTERM
	if(pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0)
	{
		perror("pthread_sigmask");
		exit(1);
	}
}
/* thread para esperar conexiones de cliente*/
void *accept_thread(void *message)
{
	char estado[7]; // segunda parte del mensaje serie de estados
	socklen_t addr_len;
	struct sockaddr_in clientaddr;
	int n;
	char buffer[bSize];
	char mensajeSerie[14]; //concatenación de las dos partes mensaje de estados

	addr_len = sizeof(struct sockaddr_in);

	while (1)
	{
		// Aceptación de conexiones entrantes
		write(std_out, "esperando conexión\n", 21); 
		if ((new_fd = accept(s_fd, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
		{
			perror("accept");
			exit(1);
		}
		write(std_out, "conexion aceptada\n", 18); 
		char ipClient[32];
		inet_ntop(AF_INET, &(clientaddr.sin_addr), ipClient, sizeof(ipClient));

		//mientras exista conexion se ejecuta este loop
		while(1)        
		{
			n = read(new_fd, buffer, bSize);
			if( n == -1)
			{
				write(std_out, "lectura de socket invalida\n", 28);  
				break;// error de read, cierra socket y espera nueva conexión
			}
			else if(( n == S_EOF))
			{
				write(std_out, "conexión cerrada por cliente (EOF)\n", 37);  
				break;// socket remoto cerrado, cierra socket y espera nueva conexión
			}
			else
			{		
				write(std_out, "lectura de socket valida\n", 25); 
				buffer[n] = 0x00;
				if (strncmp(buffer, ":STATES", 7) == 0)
				{
					write(std_out, "trama correcta\n", 15); 
					// Composición de la trama serie de salida
					for (int i = 0; i <= 3; i++)
					{
						estado[i * 2] = buffer[i + ST_OFFSET]; // carga de los estados (posiciones pares)
						estado[i * 2 + 1] = ',';			   // intercalado de las comas (pos. impares)
					}
					strcpy(mensajeSerie, header);	  // carga del header
					strncat(mensajeSerie, estado, 7); // concatenación de la segunda parte
					serial_send(mensajeSerie, 14);	  // envío
				}
			}
		}
		// Se cierra conexion con cliente
		write(std_out, "cerrando socket\n", 16); 
		close(new_fd);
	}
	return NULL;
}

int main(void)
{
	/*Declaracion locales de main*/
	char buff[17];
	int longitud;
	struct sigaction sa;
	struct sockaddr_in serveraddr;

	/*Configuración de handlers de señales*/
	sa.sa_handler = sigint_sigterm_handler;
	sa.sa_flags = 0; 
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) == -1)
	{
		perror("sigaction");
		exit(1);
	}

	if (sigaction(SIGTERM, &sa, NULL) == -1)
	{
		perror("sigaction");
		exit(1);
	}

	/*Setup*/
	printf("Inicio Serial Service\r\n");
	if (serial_open(USB, RATE) != 0)
	{
		printf("error en serial_open");
		exit(1);
	}

	// Creción del socket
	if ((s_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		{
			perror("socket");
			exit(1);
		}

	// Carga de datos de IP:PORT del server
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(PORT);
	if (inet_pton(AF_INET, ADRESS, &(serveraddr.sin_addr)) <= 0)
	{
		fprintf(stderr, "ERROR invalid server IP\r\n");
		return 1;
	}

	// Apertura del puerto con bind()
	if (bind(s_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
	{
		close(s_fd);
		perror("listener: bind");
		return 1;
	}

	// Seteo de socket en modo Listening
	if (listen(s_fd, 5) == -1) // backlog=5
	{
		perror("listen");
		exit(1);
	}

	bloquearSign();
	if(pthread_create(&inet_thread, NULL, accept_thread, NULL) != 0)
	{
		perror("pthread_create");
		exit(1);
	}
	desbloquearSign();

	while (1)
	{
		longitud = (serial_receive(buff, 17));
		if (longitud == -1)
		{
			perror("Recepción UART");
			exit(1);
		}
		if(longitud != 0)
		{
			if (strncmp(buff, msgButton1, 15) == 0)
			{
				if (write(new_fd, ":LINE0TG\n", 10) == 1)
				// Enviamos mensaje a cliente
				{
					perror("socket: write");
					exit(1);
				}
			}
			else if (strncmp(buff, msgButton2, 15) == 0)
			{
				// Envío de mensaje a cliente
				if (write(new_fd, ":LINE1TG\n", 10) == 1)
				{
					perror("socket: write");
					exit(1);
				}
			}
			else if (strncmp(buff, msgButton3, 15) == 0)
			{
				// Envío de mensaje a cliente
				if (write(new_fd, ":LINE2TG\n", 10) == 1)
				{
					perror("socket: write");
					exit(1);
				}
			}
			else if (strncmp(buff, msgButton4, 15) == 0)
			{
				// Envío de mensaje a cliente
				if (write(new_fd, ":LINE3TG\n", 10) == 1)
				{
					perror("socket: write");
					exit(1);
				}
			}
		}
		usleep(10000);
	}
	exit(EXIT_SUCCESS);
	return 0;
}
