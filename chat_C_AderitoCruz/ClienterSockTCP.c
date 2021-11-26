#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>

#define T_MSG		32			/* TAmanho de messagem */
#define BUFFER      250			/* Numero maximo de BUFFER */
#define FONTELEN    10          /* Tamanho maximo de username */
#define OPCLEN      16         	/* Tamanho de opcao  */
#define LINEBUFF 	2048
#define ANEXO       1000*7098   	/* Tamanho do buffer de anexos */	

char 		username[FONTELEN];		/* Nome do usuario */
int 		servSock; 				/* Descritor do socket de servidor */
pthread_t 	threadID;				/* ID do thread receber */
char target[FONTELEN];
int filesize;

/* Estrutura trocada entre servidor e cliente */
struct PACOTE {
    
    char opcao[OPCLEN]; 		/* Acção que quer o cliente ker realizar */
    char nome[FONTELEN]; 		/* Cleinte que enviou o pacote */
    char buff[BUFFER]; 			/* Contẽm o conteudo da pacote */
};

/* Declaração de funções */
void erro();
void *receberMsg();
void logado();
void logout();
void sendtoall();
void sendtoalias();
void setalias();
void guardaIPP();
void sendFile();
int validaUsers();

void cancelaReceive(){
	
	printf( "\nRecebido: anexo[%s] %d Bytes\n", target, filesize );
	pthread_cancel( threadID );
	filesize = 0;
	pthread_create( &threadID, NULL, (void *)receberMsg, NULL );
}

int main( int argc, char **argv ) {
	
	struct 	sockaddr_in servAddr;		/* Endereço de servidor */
	char 	resp[2];
	int 	ops;						/* Opção */
	int 	resp_val;
	char 	txt[BUFFER];
	struct  sigaction ocioso;
	
	/* Config de signal CTRL-C */
	struct sigaction interro;
	
	/* sa_handler guarda a função a ser invocada */
	interro.sa_handler = logout;
	ocioso.sa_handler = cancelaReceive;
	
	/* Mascara */
    if( sigfillset(&interro.sa_mask) < 0 )
        erro( "Erro com sigfillset()" );
	
	if( sigfillset(&ocioso.sa_mask) < 0 )
		erro( "Erro com sigfillset()" );
	
	ocioso.sa_flags = 0;
	interro.sa_flags = 0;
	
	/* Altera o comportamento padrão do signal */
    if( sigaction(SIGINT, &interro, 0) < 0 )
        erro( "Erro com sigaction()" );
        
	/* Altera o comportamento padrão do signal */
	if( sigaction(SIGALRM, &ocioso, 0) < 0 )
		erro( "Erro em sigaction()" );

	system( "reset" );
	
	if( argc != 3 ){
		
		puts( "Tens de entrar com a porta e IP do servidor" );
		printf( "%s <PORTA> <IP>\n", argv[0] );
		return 0;
	}
	
	sprintf( txt, "Server IP: [%s] e porta [%s]", argv[1], argv[2] );
	guardaIPP( txt );
	
	/* Cria socket */
	if( (servSock = socket(AF_INET, SOCK_STREAM, 0)) == -1 )
		erro( "Erro criando socket" );
	
	/* Configuração do endereço do servidor */
	memset( &servAddr, 0, sizeof(servAddr) );					/* Inicializa a zero a estrutura */
	servAddr.sin_family = AF_INET;								/* Protocolo usado TCP/IP */
    servAddr.sin_port 	= htons( atoi(argv[1]) );				/* Porta de comunicação */
    servAddr.sin_addr.s_addr = inet_addr( argv[2] );			/* IP do servidor */
	
	reconect:
		if( connect(servSock, (struct sockaddr *)&servAddr, sizeof(struct sockaddr)) == -1 ){
			
			memset( &resp, 0, sizeof(resp) );
			
			puts( "Servidor temporareamente indisponivel" );
			repitir:
				printf( "Queres tentas novamente?\n[r]econectar ou [s]air: " );
				scanf( "%s", resp );
			
			if( !strcmp( resp, "r" ) || !strcmp( resp, "R" ) )
				goto reconect;	
			else if( !strcmp( resp, "s" ) || !strcmp( resp, "S" ) ) {
				
				puts( "\n\nObrigado pela \n\n" );	
				return 0;
			}
			else{
				puts( "Opção nao reconhecida" );
				goto repitir;
			}	
		}	/* Fim do if */
		else{
			
			do{	
				
				system( "reset" );
				memset( &ops, 0, sizeof(ops) );	
				
				puts( "	<1>--Login		" );
				puts( "	<2>--Registrar	" );
				puts( "	<3>--Sair		" );
				printf( "\n\tOpção: " );
				scanf( "%d", &ops );
				scanf( "%*c" );
				
				if( ops == 3 )
					break;
				
				switch( ops ){
					
					/* Caso o cliente quiser fazer o login */
					case 1:{
						
						resp_val = validaUsers( servSock, "login" );
						if( resp_val == 1 ){
							
							/* Chama o login */
							logado();
							break;
						}
						else if( resp_val == 0 ) {
							
							puts( "\tUsername não existe na base de dados" );
							sleep( 1 );
						}
						else if( resp_val == -1 ){
							
							puts( "\tEste usuario encontra-se logado" );
							sleep( 1 );
						}
						
					}break;
					
					/* Caso o cliente quiser registrar-se */
					case 2:{
						
						resp_val = validaUsers( servSock, "registrar" );
						if( resp_val == 1 ){
							
							/* Chama o login */
							logado();
							break;
						}
						else if( resp_val == 0 ) {
							
							puts( "\tUsername indisponivel" );
							sleep( 1 );	
						}
						
					}break;
					case 3:{
						
					}
					
					/* Em caso do cliente escolher uma opção desconhecida */
					default:{
						
						puts( "\tOpção desconhecida!!" );
						sleep(1);		
					}	/* Fim do default */
				}	/* Fim do switch case */
				
			}while( 1 );
			
			sleep(1);
			system( "reset" );
			puts( "\tChat desligado" );
			close( servSock );
			pthread_cancel( threadID );
			return 0;	
		}	/* Fim do else */
}

/* Guarda txt num ficheiro */
void guardaIPP( char *txt ){
    
	FILE *usr;
	usr = fopen( "Ajuda/IPP.txt", "a+" );
    fprintf( usr, "%s\n", txt );   
    fclose( usr );
}

/* Envia username para validar login ou registro */
int validaUsers( int sock, char opcao[] ){
	
	int bytes;
	char Uname[OPCLEN];
	
	/* Envia o servidor a acção login */
	send( sock, opcao, strlen(opcao), 0 );
	
	tamanhoNome:
	    printf( "\tUsername: " );
	    scanf( "%s", Uname );
	    scanf( "%*c" );					/* Limpa buffer */
	
	if( strlen( Uname ) > 10 ){
	    
	    printf( "\tUsername tem de ser menor que 10\n" );
	    goto tamanhoNome;
    }

	send( sock, Uname, strlen(Uname), 0 );
	strcpy( username, Uname );
	
	memset( &Uname, 0, sizeof(Uname) );
		
	bytes = recv( sock, Uname, OPCLEN, 0 );
	if( !bytes )
		erro( "Erro recv()" );
	else if( !strcmp( Uname, "yes" ) )		
		return 1;
	else if( !strcmp( Uname, "no" ) )		
		return 0;
	else if( !strcmp( Uname, "ativo" ) )
		return -1;
		
	return 0;
}

/* Faz o tratamento de erro */
void erro( char *msg ){

	perror( msg );
	exit( 1 );
}

/* Esta função é executado depois de cliente fazer o registro ou o login */
void logado(){
	
	char 	opcao[BUFFER];
	int 	aliaslen;
	
	puts( "\tBenvindo, qualquer duvida digite 'help'" );
	pthread_create( &threadID, NULL, (void *)receberMsg, NULL );
	sleep( 1 );
	system( "reset" );
	
	while( 1 ){
		
		memset( &opcao, 0, sizeof(opcao) );
		scanf( "%[^\n]s", opcao );
		scanf( "%*c" );
		
		if( !strncmp(opcao, "*", 1) ) {
            rep:
				printf( "\tTens acerteza que queres sair?\n[s]im ou [n]ão\n" );
				scanf( "%[^\n]s", opcao );
				scanf( "%*c" );
			
            if( !strcmp( opcao, "s" ) || !strcmp( opcao, "S" ) ){
				
				logout();
            	break;
			}
			else  if( !strcmp( opcao, "n" ) || !strcmp( opcao, "N" ) )
				scanf( "%*c" );
			else{
				goto rep;
				scanf( "%*c" );
			}
        }	/* Fim do if de sair */
		else if( !strncmp(opcao, "X", 1) ){
			
			struct PACOTE packet;
		    
		    memset( &packet, 0, sizeof(struct PACOTE) );
		    strcpy( packet.opcao, "X" );
		    strcpy( packet.nome, "-1" );
		    
		    send( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
			system( "reset" );
			break;
		}
		else if( !strncmp(opcao, "help", 4) )
            system( "cat Ajuda/help.txt" );
		/* Altera nome do usuario */
		else if( !strncmp(opcao, "#", 1) ) {
			
            char 	*ptr = strtok( opcao, " " );
			char 	alias[FONTELEN];
			
			ptr = strtok( 0, " " );
            memset( alias, 0, sizeof(alias) );
            
			if( ptr != NULL ) {
				
                if( strlen( ptr ) > FONTELEN ) 
					printf( "\tUsername tem de ser menor que 10\n" );
				else{
					
					strcpy( username, ptr );
                	setalias( username );	
				}
            }
		}
		else if( !strncmp(opcao, ">", 1) ) {
			
            char *ptr = strtok( opcao, " " );
            char temp[BUFFER];
			
            ptr = strtok( 0, " " );
            
			/* Garante que o cliente não envia menssagem para si mesmo */
			if( !strcmp( ptr, username ) ){
				
				puts( "\tNão podes enviar mensagem para voce mesmo" );
				sleep( 1 );
				system( "clear" );
			}
			else{
				
				memset( &temp, 0, sizeof(temp) );
			
	            if( ptr != NULL ) {
					
	                aliaslen =  strlen( ptr );
					
	                if( aliaslen > FONTELEN ) 
						ptr[FONTELEN] = 0;
	                
					strcpy(temp, ptr);
	                while( *ptr ) 
						ptr++; 
					
					ptr++;
					while( *ptr <= ' ' ) 
						ptr++;
						
	               sendtoalias( temp, ptr );
	            }
			}
        }
        else if( !strncmp(opcao, "<>", 2) ) {
            
            if( strlen( &opcao[3] ) > 32 )
                printf( "\tMensagem não pode ter mais do que 32 caracter\n\tTente novamente\n" );
            else
                sendtoall( &opcao[3] );
        }
        else if( !strncmp(opcao, "sair", 6) ){
            
			system( "reset" );
			logout();
			break;
		}
		else if(  !strncmp(opcao, "@", 1) ){
			
			char *ptr = strtok( opcao, " " );
            char temp[BUFFER];
			
            ptr = strtok( 0, " " );
				
			memset( &temp, 0, sizeof(temp) );
			
            if( ptr != NULL ) {
				
                aliaslen =  strlen( ptr );
				
                if( aliaslen > FONTELEN ) 
					ptr[FONTELEN] = 0;
                
				strcpy(temp, ptr);
                while( *ptr ) 
					ptr++; 
				
				ptr++;
				while( *ptr <= ' ' ) 
					ptr++;
					
               sendFile( temp, ptr );
            }
		}
        else 
			printf( "\tOpção desconhecida...\n" );
	}	/* Fim do while */
}

void sendFile( char *target, char *file ){
	
	int targetlen;
    struct PACOTE packet;
	char caminho[100];

    targetlen = strlen( target );
    
    memset( &packet, 0, sizeof(struct PACOTE) );
    
	strcpy( packet.opcao, "@" );
    strcpy( packet.nome, username );
    strcpy( packet.buff, target );
    strcpy( &packet.buff[targetlen], " " );
    strcpy( &packet.buff[targetlen + 1], file );
    
    send( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
	
	printf( "Caminho do ficherio: " );
	scanf( "%[^\n]s", caminho );
	scanf( "%*c" );
	
	FILE *fp = fopen( caminho, "rb" );
	if( !fp )
		puts( "Ficheiro não existe" );
	else{
		
		/* First read file in chunks of 256 bytes */
		char buff[ANEXO];
		int nread;
		
		printf( "Enviando...\n" );
		while( 1 ){
			
			memset( &buff, 0, sizeof buff );
            nread = fread( buff, 1, ANEXO, fp );
			
			if( nread > 0 ){
				
				filesize += nread;
				write( servSock, buff, nread );
				fflush( stdout );
			}

			if ( ferror(fp) || nread <= 0 )
				break;
		}/* Fim do while */
		
		fflush( stdin );
		fflush( stdout );
		printf( "Foi enviado [%d] com sucesso\n", filesize );
	}/* Fim do else */
}

/* Executado num thread para ficar a espera de mensagem vindo do sirvidor */
void *receberMsg() {
    
    int bytes;
    struct PACOTE packet;
	char file[ANEXO];   		/* Armazena os bytes do ficheiro */
	
	memset( &packet, 0, sizeof(struct PACOTE) );
	
    while( 1 ) {
        
        bytes = recv( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
        if( !bytes )
			break;
        
		if( !strcmp(packet.opcao, "msg") )
        	printf( "\t\t[%s] > %s\n", packet.nome, packet.buff );
		/* Executar dentro do fork */
		else if( !strcmp(packet.opcao, "@") ){
		
			FILE *fp = fopen( packet.buff, "ab" );
			
			strcpy( target, packet.nome );
			
			while( 1 ){
				
				alarm( 10 );
				if( (bytes = read(servSock, file, ANEXO)) <= 0 )
					break;
				alarm( 0 );
				filesize += bytes;
				fwrite( file, 1, bytes, fp );
				memset( &file, 0, sizeof( file ) );
			}
		}
		
        memset( &packet, 0, sizeof(struct PACOTE) );
    }
	
	puts( "\tEncerrando sessão..." );
	sleep( 2 );
	system( "reset" );
	puts( "Sessão terminado" );
    close( servSock );
    exit( 1 );
	
    return NULL;
}

/* Termina a sessão do cliente */
void logout() {
	
    struct PACOTE packet;
    
    memset( &packet, 0, sizeof(struct PACOTE) );
	
    strcpy( packet.opcao, "terminar" );
    strcpy( packet.nome, username );
    
    send( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
	puts( "\t\tUsuario desconetado do servidor" );
	/* Cancela o thread de receberMsg */
	pthread_cancel( threadID );
}	

/* Envia mensagem para todo as pessoas onlines */
void sendtoall( char *msg ) {

    struct PACOTE packet;
    
    msg[BUFFER] = 0;
    
    memset( &packet, 0, sizeof(struct PACOTE) );
    strcpy( packet.opcao, "<>" );
    strcpy( packet.nome, username );
    strcpy( packet.buff, msg );
    
    send( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
}

void sendtoalias( char *target, char *msg ) {
 
    int targetlen;
    struct PACOTE packet;
    
    //msg[BUFFER] = 0
    targetlen = strlen( target );
    
    memset( &packet, 0, sizeof(struct PACOTE) );
    
	strcpy( packet.opcao, ">" );
    strcpy( packet.nome, username );
    strcpy( packet.buff, target );
    strcpy( &packet.buff[targetlen], " " );
    strcpy( &packet.buff[targetlen + 1], msg );
    
    send( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
}

void setalias( char *me ) {
    
    struct PACOTE packet;
    
    memset( &packet, 0, sizeof(struct PACOTE) );
    strcpy( packet.opcao, "#" );
    strcpy( packet.nome, me );
    
    send( servSock, (void *)&packet, sizeof(struct PACOTE), 0 );
}
