#include <sys/socket.h>		/* socket(), connect(), bind(), listen(), accept() */
#include <sys/types.h>		
#include <arpa/inet.h>		/* inet_addr(), inet_ntoa(), hton*() */
#include <pthread.h>		/* pthread_create(), pthread_kill(), pthread_cancel() */
#include <stdlib.h>			
#include <string.h>			/* memset() */
#include <unistd.h>
#include <signal.h>			/* sigaction(), sigfillset() */
#include <stdio.h>
#include <netdb.h>
#include <limits.h>
#include <errno.h>			/* perror() */
#include <time.h>			/* time(), asctime(), localtime() */

#define FONTELEN    10          /* Tamanho maximo de username */
#define BACKLOG     5			/* Numero maximo de clientes a espera do servidor  */
#define TIMEOUT     10         /* Tempo maximo que o servidor pode ficar a espera de mensagem */
#define OPCLEN      16          /* Tamanho de opcao  */
#define BUFFER      250			/* Numero maximo de BUFFER */
#define ANEXO       1000*7098   /* Tamanho do buffer de anexos */

/* Guarda todos os clientes registrados */
struct USUARIOS{
    
    char nome[FONTELEN];
};

/* Guarda info sobre socket do cliente */
struct CLIENTECONF{
	
    pthread_t ID;
	struct sockaddr_in addr;
	int sock;
};

/* Estrutura trocada entre servidor e cliente */
struct PACOTE {
    
    char opcao[OPCLEN]; 		/* Acção que quer o cliente ker realizar */
    char nome[FONTELEN]; 		/* Cleinte que enviou o pacote */
    char buff[BUFFER]; 		    /* Contẽm o conteudo da pacote */ 
};

/* Estrutura de dados dos clientes */
struct CLIENTEINFO {
    
    struct sockaddr_in addr;
    int sockfd;				    /* Descritor do socket do cliente */
    char nome[FONTELEN]; 		/* Nome do cliente */
};

/* Nó da lista */
struct LLNODE {
    
    struct CLIENTEINFO _INFO;		
    struct LLNODE *proximo;				
};

/* LISTA */
struct LLIST {
    
    struct LLNODE *head, *tail;
    int size;						/* Conta o numero de clientes registados */
};

/* Declaração de variaveis globais */
FILE            *usr;                       
pthread_mutex_t _mutex;				        /* Usado para sincronização de processos */
pthread_t       processo;				    /* ID do processo de io_handler */
time_t          data_sist;				    /* variav */
int             tamanho = -1;               /* Numero de usuarios cadastrados */
char            txt[BUFFER];                /* Guarda a impressão formatada para ser armazenada num ficheiro */
int             servSock;					/* Descritor do servidor retornado pelo socket() */
struct USUARIOS user[100];                  /* Estrutura que guarda usuarios registrados */
struct tm       *data;				        /* Variavel da Estrutura de time */
struct LLIST    _list;                      /* Declaração da lista */

int     login();
int     regis();
void    logado();
void    interr();
void    catchAlarm();
void    *io_handler();
void    catchCancel();
int     validaLogin();
void    *client_handler();
/* Fim de declarações */

/* Compara descritor */
int compare( int sock , struct CLIENTEINFO *b ) {
    
    return sock - b->sockfd;
}

/* Inicializa a lista */
void list_init(struct LLIST *ll) {
    
    ll->head = ll->tail = NULL;
    ll->size = 0;
}

/* Add cliente a lista de online */
int list_insert(struct LLIST *ll, struct CLIENTEINFO *clnt_info) {
    
    if( ll->head == NULL ) {
        
        ll->head = (struct LLNODE *)malloc(sizeof(struct LLNODE));
        ll->head->_INFO = *clnt_info;
        ll->head->proximo = NULL;
        ll->tail = ll->head;
    }
    else {
        
        ll->tail->proximo = (struct LLNODE *)malloc(sizeof(struct LLNODE));
        ll->tail->proximo->_INFO = *clnt_info;
        ll->tail->proximo->proximo = NULL;
        ll->tail = ll->tail->proximo;
    }
    ll->size++;
    return 0;
}

/* Verfica se um dado nome esta online */
int onlineCheck( struct LLIST *ll, char nome[] ) {
    
    struct LLNODE *curr;
    
    if( ll->head == NULL ) 
        return 1;
    
    if( strcmp(nome, ll->head->_INFO.nome) == 0 )
        return 0;
        
    for( curr = ll->head ; curr->proximo != NULL ; curr = curr->proximo ) 
        if( strcmp(nome, curr->proximo->_INFO.nome) == 0 )
            return 0;
            
    return 1;
}

/* Elimina cliente da lista de onlines */
int list_delete( struct LLIST *ll, int sock ) {
    
    struct LLNODE *curr, *temp;
    if( ll->head == NULL ) 
        return -1;
    
    if( compare(sock, &ll->head->_INFO) == 0 ) {
        
        temp = ll->head;
        ll->head = ll->head->proximo;
        
        if( ll->head == NULL ) 
            ll->tail = ll->head;
            
        free( temp );
        ll->size--;
        return 0;
    }
    for( curr = ll->head ; curr->proximo != NULL ; curr = curr->proximo ) {
        
        if( compare(sock, &curr->proximo->_INFO) == 0 ) {

            temp = curr->proximo;

            if( temp == ll->tail ) 
                ll->tail = curr;
            
            curr->proximo = curr->proximo->proximo;
            free( temp );
            ll->size--;
            return 0;
        }
    }
    return -1;
}

/* Lista os clientes onlines */
void list_dump( struct LLIST *ll ) {
    
    struct LLNODE *curr;
    struct CLIENTEINFO *clnt_info;
    
    printf( "\tLista de pessoas online : %d\n", ll->size );
    
    for( curr = ll->head ; curr != NULL ; curr = curr->proximo ) {
        
        clnt_info = &curr->_INFO;
        printf( "[%d] %s %s\n", clnt_info->sockfd, clnt_info->nome, inet_ntoa(clnt_info->addr.sin_addr) );
    }
}

/* Faz o tratamento de erro */
void erro( char *msg ){

	perror( msg );
	exit( 1 );
}

/* Guarda log do servidor */
void save_log( char *str ){
    
    FILE *log;
    log = fopen( "Admin/log.txt", "a+" );
    fprintf( log, "%s", str );
    fclose( log );
}

/* Guarda cliente no ficheiro de cadastro.txt */
void guardar(){

	int i = 0;
	usr = fopen( "Admin/Usuario.txt", "w+" );
	for( i = 0 ; i < tamanho ; i++ )    		
    	if( strcmp(user[i].nome, "-1") )
    		fprintf( usr, "%s\n", user[i].nome  );   	
	fclose( usr );
}

/* Carrega dados do ficheiro para a estrutura Usuarios */
void carregar(){
    
    int i = 0;
    usr = fopen( "Admin/Usuario.txt", "r" );
    if( !usr )
        return;
    else{
        
        while( !feof(usr) ){
            
            tamanho++;
            fscanf( usr, "%s", user[i].nome );
            i++;
        }  
        fclose( usr );
        return;
    }
}

/* Função principal */
int main( int argc, char **argv ){

    
    FILE       *wlan0;				        /* Ficheiro para guardar IP do servidor wlan */
    int        ponter = 1;
	int        _size;						/* Guarda tamanho da estrutura */	
	int        clntSock; 					/* Descritor de cliente retornado pelo accept() */
	char       wlanIP[BUFFER];			    /* O IP do ficheiro sera carregado neste string */
	struct     sockaddr_in servAddr;	    /* Endereço do servidor */
	struct     sockaddr_in clntAddr;	    /* Endereço do cliente */
    struct     CLIENTECONF clntConf;
	struct     sigaction BrutCancel;      
	struct     sigaction ocioso;

    if( argc != 2 ){
        
        printf( "Falta introduzir a porta: %s <port>\n", argv[0] );
        return 0;
    }
    
    /* Configuração dos signal */
        /* sa_handler guarda a função a ser invocada */
    	BrutCancel.sa_handler = catchCancel;
        ocioso.sa_handler = catchAlarm;
        
        /* Mascara */
        if( sigfillset(&ocioso.sa_mask) < 0 )
            erro( "Erro com sigfillset()" );
    	/* Mascara */
        if( sigfillset(&BrutCancel.sa_mask) < 0 )
            erro( "Erro com sigfillset()" );
        
    	BrutCancel.sa_flags = 0;
        ocioso.sa_flags = 0;
        
        /* Altera o comportamento padrão do signal */
        if( sigaction(SIGALRM, &ocioso, 0) < 0 )
            erro( "Erro em sigaction()" );
    		
    	/* Altera o comportamento padrão do signal */
        if( sigaction(SIGINT, &BrutCancel, 0) < 0 )
            erro( "Erro em sigaction()" );
    /* Fim Configuração dos signal */
    
    /* Criando socket */
     if( (servSock = socket(AF_INET, SOCK_STREAM, 0)) == -1 )
 		erro( "Erro em socket()" );
    
    if( setsockopt( servSock, SOL_SOCKET, SO_REUSEADDR, &ponter, sizeof(ponter) ) == -1 )
		erro( "falha em setsockopt()" );
        
 	/* Configuração do endereço do servidor */
    servAddr.sin_family = AF_INET;					/* Protocolo usado */
    servAddr.sin_port = htons( atoi(argv[1]) );		/* PORTA de comunicação */
    servAddr.sin_addr.s_addr = INADDR_ANY;		    /* IP servidor */
    memset( &(servAddr.sin_zero), 0, 8 );			/* Inicia a estrutura a zero */

    /* Faz a ligação entre o endereço servidor e o descritor de sock do servidor */
 	if( bind(servSock, (struct sockaddr *)&servAddr, sizeof(struct sockaddr)) == -1 )
 		erro( "Erro em bind()" );

    /* Marca o socket descriptor como pronto para aceitar conexões */
 	if( listen(servSock, BACKLOG) == -1 )
		erro( "Erro em listen()" );

	/* Cria um processo para que o admin possa trabalhar */
	if( pthread_create(&processo, NULL, io_handler, NULL) != 0 )
        erro( "Erro em pthread_create()" );
        
    /* Inicializa lista */
    list_init( &_list );

    /* Inicializa mutex */
    pthread_mutex_init(&_mutex, NULL);
    
    wlan0 = popen( "ifconfig | grep 'inet addr' | cut -d':' -f2 | cut -d' ' -f1", "r" );
  	while ( fgets(wlanIP, sizeof wlanIP, wlan0) )
    
    system( "reset" );
    puts( "****Caracteristica do servidor****" );
	printf( "IP 		: %s", wlanIP );
	printf( "Porta 		: %s\n", argv[1] );
	printf( "Protocolo 	: TCP\\IP\n"   );
	printf( "Descritor 	: %d\n", servSock );
	puts( "***********************************" );
    
    /* Load user regis */
    carregar();
    
    _size = sizeof(struct sockaddr);
    while( 1 ){	/* Loop infinito */
        
        memset( &txt, 0, sizeof(txt) );
		memset( &clntAddr, 0, sizeof(struct sockaddr) );
        
        if( (clntSock = accept(servSock, (struct sockaddr *)&clntAddr, (socklen_t*)&_size)) == -1 )
            erro("Falha com o accept");//break;
        
        clntConf.addr = clntAddr;
		clntConf.sock = clntSock;
		
		/* Configuração para apanhar hora do sistema */
		data_sist = time( NULL );
        data = localtime( &data_sist );
		
		printf( "%s conectou-se %s", inet_ntoa( clntAddr.sin_addr ), asctime(data) );
		sprintf( txt, "%s conectou-se %s", inet_ntoa( clntAddr.sin_addr ), asctime(data) );
        save_log( txt );
		
		/* Craindo processo filho */
		pthread_create( &clntConf.ID, NULL, client_handler, (void *)&clntConf );
    }
	
    //erro( "Erro accept()" );
	return 0;
}

/* Testado!! mas falta fazer algumas alteraçoes ainda */
void catchAlarm(){
    
	struct PACOTE spacket;
	struct LLNODE *curr;

	data_sist = time( NULL );
	data = localtime( &data_sist );
        
	for( curr = _list.head ; curr != NULL ; curr = curr->proximo ) {
    
		memset( &spacket, 0, sizeof(struct PACOTE) );
	
    	strcpy( spacket.opcao, "msg" );
    	strcpy( spacket.nome, "PpKumi" );
    	strcpy( spacket.buff, "Servidor ocioso por mais de 30s" );
    
		send( curr->_INFO.sockfd, (void *)&spacket, sizeof(struct PACOTE), 0 );

		data_sist = time( NULL );
		data = localtime( &data_sist );
	
		printf( "Conexão [%s %s] desconetou forçada %s", inet_ntoa(curr->_INFO.addr.sin_addr), curr->_INFO.nome, asctime(data) );
		sprintf( txt, "Conexão [%s %s] desconetou forçada %s", inet_ntoa(curr->_INFO.addr.sin_addr), curr->_INFO.nome, asctime(data) );
		save_log( txt );
        
		pthread_mutex_lock( &_mutex );
		list_delete( &_list, curr->_INFO.sockfd );	
		pthread_mutex_unlock( &_mutex );
	}
	
	system( "reset" );
	puts( "Servidor Desligado" );
	guardar();
	close( servSock );
	exit( 1 );
}

/* Testado!! mas falta fazer algumas alteraçoes ainda */
void catchCancel(){
    
	struct PACOTE spacket;
	struct LLNODE *curr;

	data_sist = time( NULL );
	data = localtime( &data_sist );
        
	for( curr = _list.head ; curr != NULL ; curr = curr->proximo ) {
    
		memset( &spacket, 0, sizeof(struct PACOTE) );
		
		strcpy( spacket.opcao, "msg" );
		strcpy( spacket.nome, "PpKumi" );
		strcpy( spacket.buff, "Servidor fora de serviço" );
	
		send( curr->_INFO.sockfd, (void *)&spacket, sizeof(struct PACOTE), 0 );
	
		data_sist = time( NULL );
		data = localtime( &data_sist );
		
		printf( "Conexão [%s %s] desconetou forçada %s", inet_ntoa(curr->_INFO.addr.sin_addr), curr->_INFO.nome, asctime(data) );
		sprintf( txt, "Conexão [%s %s] desconetou forçada %s", inet_ntoa(curr->_INFO.addr.sin_addr), curr->_INFO.nome, asctime(data) );
		save_log( txt );
		pthread_mutex_lock( &_mutex );
		list_delete( &_list, curr->_INFO.sockfd );	
		pthread_mutex_unlock( &_mutex );
	}
	
	system( "reset" );
	puts( "Servidor Desligado" );
	guardar();
	close( servSock );
	exit( 1 );
}

/* Função que faz o valida o login e registra o cliente e 
   depois lida com as acções do cliente */
void *client_handler( void *clntConfig ) {
    
    struct  CLIENTECONF clnt = *(struct CLIENTECONF *)clntConfig;
	int     clntSock = clnt.sock;
    int     bytes;
    char    opc[OPCLEN];						/* Opção escolhida pelo cliente */
    
    /* colocar um signal de alarme aqui */
    while( (bytes = recv( clntSock, opc, OPCLEN, 0 )) ){
        
        opc[bytes] = '\0';
        
        if( strcmp( opc, "login" ) == 0 ){
            
            if( login( clntSock, clnt ) == 0 )
                break;   
        }
        else if( strcmp( opc, "registrar" ) == 0 ){
            
            if( regis( clntSock, clnt ) == 0 )
                break;
        }            
        
        memset( &opc, 0, sizeof(opc) );
        memset( &bytes, 0, sizeof(bytes) );
        
    }   /* fim do while */
 
    
	data_sist = time( NULL );
    data = localtime( &data_sist );
	
	printf( "Conexão com [%s] perdida %s", inet_ntoa( clnt.addr.sin_addr ), asctime(data) );
    sprintf( txt, "Conexão com [%s] perdida %s", inet_ntoa(clnt.addr.sin_addr), asctime(data) );
	save_log( txt );
    
    pthread_mutex_lock( &_mutex );
    list_delete( &_list, clnt.sock );
    pthread_mutex_unlock( &_mutex );
    
    pthread_cancel( clnt.ID );
    close( clnt.sock );
	return NULL;
}

/* Login dos clientes */
int login( int clntSock, struct CLIENTECONF addr ){
    
    struct CLIENTEINFO clienteinfo;
    char alias[FONTELEN];
    int boolean = 0;
    int bytes;
    int val;                                /* Guarda a resposta de valida */
    
    /* Fica a espera de um nome por parte do cliente para fazer o login */
    memset( &alias, 0, sizeof(alias) );
    
    bytes = recv( clntSock, alias, FONTELEN, 0 );
    alias[bytes] = '\0';
    
    if( !bytes )
        boolean = 0;
    else{
        
        boolean = 1;
        
        pthread_mutex_lock( &_mutex );
        val = validaLogin( clntSock, alias );    
        pthread_mutex_unlock( &_mutex );
        
        if( onlineCheck( &_list, alias ) == 0 ){
            
            send( clntSock, "ativo", strlen("ativo"), 0 );            
            return boolean;
        }
    	if( val == 1 ){
            
    		send( clntSock, "yes", strlen("yes"), 0 );
            
            /* Guarda info do cliente logado */
            strcpy( clienteinfo.nome, alias );
            clienteinfo.sockfd = clntSock;
            clienteinfo.addr = addr.addr;
            
            pthread_mutex_lock( &_mutex );
            list_insert( &_list, &clienteinfo );
            pthread_mutex_unlock( &_mutex );
            
            /* Configuração para apanhar hora do sistema */
			data_sist = time( NULL );
	        data = localtime( &data_sist );
			
			printf( "%s %s entrou no chat %s", alias, inet_ntoa( addr.addr.sin_addr ), asctime(data) );
			sprintf( txt, "%s %s entrou no chat %s", alias, inet_ntoa( addr.addr.sin_addr ), asctime(data) );
	        save_log( txt );
			
			logado( clienteinfo, alias );
    	}
    	else
    		send( clntSock, "no", strlen("no"), 0 );   
    }
    return boolean;
}

/* Faz o registo de um novo cliente */
int regis( int clntSock, struct CLIENTECONF addr ){
    
    struct CLIENTEINFO clienteinfo;
    int bytes;                  /* Armazena bytes do recv */
    int val;                    /* Guarda a resposta de valida 0 ou 1 */
    char alias[FONTELEN];       /* Nome do cliente */
    int boolean = 0;
    
    /* Fica a espera de um nome por parte do cliente para fazer o registro */
    bytes = recv( clntSock, alias, FONTELEN, 0 );
    alias[bytes] = '\0';
    
    if( !bytes )
        boolean = 0;
    else{
        
        boolean = 1;
        
        val = validaLogin( clntSock, alias );    
                
    	if( val == 0 ){
            
            send( clntSock, "yes", strlen("yes"), 0 );
            
             /* Guarda info do cliente logado */
            strcpy( clienteinfo.nome, alias );
            clienteinfo.sockfd = clntSock;
            clienteinfo.addr = addr.addr;
            
            strcpy( user[tamanho].nome, alias );
            tamanho++;                          /* incrementa o numero de usuarios */
            
            pthread_mutex_lock( &_mutex );
            list_insert( &_list, &clienteinfo );
            pthread_mutex_unlock( &_mutex );
            
            guardar();
            
            printf( "%s %s entrou no chat %s", alias, inet_ntoa( addr.addr.sin_addr ), asctime(data) );
			sprintf( txt, "%s %s entrou no chat %s", alias, inet_ntoa( addr.addr.sin_addr ), asctime(data) );
	        save_log( txt );
			
            logado( clienteinfo, alias );
    	}
    	else
    		send( clntSock, "no", strlen("no"), 0 );
    }
    
    memset( &alias, 0, sizeof(alias) );
    
    return boolean;
}

/* Recebe dois argumentos sockfd e nome para verificar se o nome
   esta presente no ficheiro Usuario.txt */
int validaLogin( int sock, char *alias ){
	
    int i;
 
	for( i = 0 ; i < tamanho ; i++ )
        if( !strcmp(user[i].nome, alias) )
            return 1;   
            
    return 0;
}

/* Lida com os camandos inseridos pelo admin do servidor */
void *io_handler(){
    
	char opcao[FONTELEN];
    
	while( 1 ) {
    		
		memset( &opcao, 0, sizeof(opcao) );
		scanf( "%s", opcao );

		system( "reset" );

		if( !strcmp(opcao, "sair") )
			break;
		else if( !strcmp(opcao, "users") ) {
   
			pthread_mutex_lock( &_mutex );
			list_dump( &_list );
			pthread_mutex_unlock( &_mutex );
		}
		else if( !strcmp(opcao, "log") )
			system( "cat Admin/log.txt" );
		else            
			puts( "Comando desconhecido..." );
	}
    	
	/* Enviar mensagem aos cliente logados depois desligar */
	puts( "Desligado o servidor" );
	exit( 1 );
	return NULL;
}

/*
 *  Incompleto ainda 
 *  falta guarda todas as transações num ficheiro
 */
void logado( struct CLIENTEINFO clntinfo, char *alias ){
    
    struct PACOTE pacote;
    struct LLNODE *curr;
    int bytes;
    
    while( 1 ) {
        
        alarm( TIMEOUT );   /* Se o servidor ficar ocioso eh desligado */
        memset( &pacote, 0, sizeof( struct PACOTE ) );
        bytes = recv( clntinfo.sockfd, (void *)&pacote, sizeof(struct PACOTE), 0 );
        if( !bytes ){
            
            close( clntinfo.sockfd );
            break;
        }
            
        alarm( 0 ); /* cancela o alarm anterior */
        
        /* Configuração para apanhar hora do sistema */
		data_sist = time( NULL );
        data = localtime( &data_sist );
        
        if( !strcmp(pacote.opcao, "X") ){
            
            printf( "%s [%s] eliminou a sua conta [%s]  %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.nome, asctime(data) );
            sprintf( txt, "%s [%s] eliminou a sua conta [%s] %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.nome, asctime(data) );
            save_log( txt );
            pthread_mutex_lock( &_mutex );
            
            int i;
            /* Altera na estrutura de usuarios registrados */
            for ( i = 0 ; i < tamanho ; i++ ) {
                
                if( !strcmp(user[i].nome, alias) ){
                    
                    strcpy( user[i].nome, pacote.nome );
                    break;
                }
            }
            pthread_mutex_lock( &_mutex );
            list_delete( &_list, clntinfo.sockfd );
            
            guardar();
            pthread_mutex_unlock( &_mutex );
            break;
        }
        else if( !strcmp(pacote.opcao, "#") ) {   /* depos de altera username guarda na ficheiro*/
            
            printf( "%s [%s] alterou o seu nome para [%s]  %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.nome, asctime(data) );
            sprintf( txt, "%s [%s] alterou o seu nome para [%s] %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.nome, asctime(data) );
            save_log( txt );
            pthread_mutex_lock( &_mutex );
            
            int i;
            /* Altera na estrutura de usuarios registrados */
            for ( i = 0 ; i < tamanho ; i++ ) {
                
                if( !strcmp(user[i].nome, alias) ){
                    
                    strcpy( alias, pacote.nome );
                    strcpy( user[i].nome, pacote.nome );
                    break;
                }
            }
            /* Altera online */
            for (curr = _list.head ; curr != NULL ; curr = curr->proximo ) {
                
                if( compare(curr->_INFO.sockfd, &clntinfo) == 0 ) {
                    
                    strcpy( curr->_INFO.nome, pacote.nome );
                    strcpy( clntinfo.nome, pacote.nome );
                    break;
                }
            }
            guardar();
            pthread_mutex_unlock( &_mutex );
        }
        // envia para um determinado cliente e guarda num ficheiro especifico
        else if( !strcmp(pacote.opcao, ">") ) {
            
            int i;
            char target[FONTELEN];
            
            printf( "%s [%s] > %s %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.buff, asctime(data) );
            sprintf( txt, "%s [%s] > %s %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.buff, asctime(data) );
            save_log( txt );
            
            for( i = 0 ; pacote.buff[i] != ' ' ; i++ ); pacote.buff[i++] = 0;
            
            strcpy( target, pacote.buff );
            pthread_mutex_lock( &_mutex );
            
            for( curr = _list.head ; curr != NULL ; curr = curr->proximo ) {
                
                if( strcmp(target, curr->_INFO.nome) == 0 ) {
                    
                    struct PACOTE spacket;
                    
                    memset( &spacket, 0, sizeof(struct PACOTE) );
                    
                    if( !compare(curr->_INFO.sockfd, &clntinfo) ) continue;
                    
                    strcpy( spacket.opcao, "msg" );
                    strcpy( spacket.nome, pacote.nome );
                    strcpy( spacket.buff, &pacote.buff[i] );
                    
                    send( curr->_INFO.sockfd, (void *)&spacket, sizeof(struct PACOTE), 0 );
                }
            }
            pthread_mutex_unlock( &_mutex );
        }
        // envia para todo cliente e guarda num ficheiro especifico
        else if( !strcmp(pacote.opcao, "<>") ) {
            
            struct PACOTE spacket;
            
            printf( "%s [%s] <> %s %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.buff, asctime(data) );
            sprintf( txt, "%s [%s] <> %s %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.buff, asctime(data) );
            save_log( txt );
            
            memset( &spacket, 0, sizeof(struct PACOTE) );
            
            pthread_mutex_lock( &_mutex );
            
            for( curr = _list.head ; curr != NULL ; curr = curr->proximo ) {
                
                if( !compare(curr->_INFO.sockfd, &clntinfo) ) continue;
                
                strcpy( spacket.opcao, "msg" );
                strcpy( spacket.nome, pacote.nome );
                strcpy( spacket.buff, pacote.buff );
                
                send( curr->_INFO.sockfd, (void *)&spacket, sizeof(struct PACOTE), 0 );
                memset( &spacket, 0, sizeof(struct PACOTE) );
            }
            pthread_mutex_unlock( &_mutex );
        }
        else if( !strcmp(pacote.opcao, "terminar") ) {
            
            data_sist = time( NULL );
            data = localtime( &data_sist );
            
            printf( "[%d] %s desconectou-se do chat %s %s", clntinfo.sockfd, clntinfo.nome, inet_ntoa(clntinfo.addr.sin_addr), asctime(data) );
            sprintf( txt, "[%d] %s desconectou-se do chat %s %s", clntinfo.sockfd, clntinfo.nome, inet_ntoa(clntinfo.addr.sin_addr), asctime(data) );
            save_log( txt );
            
            pthread_mutex_lock( &_mutex );
            list_delete( &_list, clntinfo.sockfd );
            pthread_mutex_unlock( &_mutex );
            
            break;
        }
        else if( !strcmp(pacote.opcao, "@") ) {
            
            int i;
            char target[FONTELEN];
            char file[ANEXO];   /* Armazena os bytes do ficheiro */
            int descritor;
            
            printf( "Enviando anexo de %s [%s] para %s %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.buff, asctime(data) );
            sprintf( txt, "Enviando anexo de %s [%s] para %s %s", alias, inet_ntoa(clntinfo.addr.sin_addr), pacote.buff, asctime(data) );
            save_log( txt );
            
            for( i = 0 ; pacote.buff[i] != ' ' ; i++ ); pacote.buff[i++] = 0;
            
            strcpy( target, pacote.buff );
            pthread_mutex_lock( &_mutex );

            /* Envia nome do ficheiro  */
            for( curr = _list.head ; curr != NULL ; curr = curr->proximo ) {
                
                if( strcmp(target, curr->_INFO.nome) == 0 ) {
                    
                    struct PACOTE spacket;
                    memset( &spacket, 0, sizeof(struct PACOTE) );
                    if( !compare(curr->_INFO.sockfd, &clntinfo) ) continue;
        
                    strcpy( spacket.opcao, "@" );
                    strcpy( spacket.nome, pacote.nome );
                    strcpy( spacket.buff, &pacote.buff[i] );
                    descritor = curr->_INFO.sockfd;
                    send( curr->_INFO.sockfd, (void *)&spacket, sizeof(struct PACOTE), 0 );
                    break;
                }
            }
            pthread_mutex_unlock( &_mutex );
            
            /* envia ficheiro */
            while( (bytes = read(clntinfo.sockfd, file, ANEXO)) > 0 ){
                
                printf( "Copiado %d Bytes\n", bytes );    
                write( descritor, file, bytes );
                memset( &file, 0, sizeof file );
            }
            
            puts( "juka feio\n\n" );
            
            fflush( stdin );
			fflush( stdout );
        }
    }   /* Fim do while */
}   /* Fim da função logado */
