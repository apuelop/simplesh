// Shell `simplesh`
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <getopt.h>
#include <dirent.h>
#include <stdbool.h>
#include <pwd.h>
#include <ftw.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Libreadline
#include <readline/readline.h>
#include <readline/history.h>

// Estructuras
#include "structs.h"

// Parser y función panic
#include "parser.h"
#include "panic.h"

// Tipos presentes en la estructura `cmd`, campo `type`.
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXPATH 150
#define MAXFILES 10

// Declaración de funciones necesarias
int fork1(void);  // Fork but panics on failure.
void run_pwd();
int run_cd(char * dir);
int run_tee(char * argv[MAXARGS]);
int run_du(char * argv[MAXARGS]);

// Ejecuta un `cmd`. Nunca retorna, ya que siempre se ejecuta en un
// hijo lanzado con `fork()`.
void
run_cmd(struct cmd *cmd)
{
    int p[2];
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if(cmd == 0)
        exit(0);

    switch(cmd->type)
    {
    default:
        panic("run_cmd");

        // Ejecución de una única orden.
    case EXEC:
        ecmd = (struct execcmd*)cmd;
        if (ecmd->argv[0] == 0)
            exit(0);
		else if (!strcmp(ecmd->argv[0], "pwd")){
		        run_pwd();
		        exit(0);
		}
		else if (!strcmp(ecmd->argv[0], "tee")){
			run_tee(&ecmd->argv[0]);
		        exit(0);
		}
    else if (!strcmp(ecmd->argv[0], "du")){
      run_du(&ecmd->argv[0]);
            exit(0);
    }
		else {
           execvp(ecmd->argv[0], ecmd->argv);
        }

        // Si se llega aquí algo falló
        fprintf(stderr, "exec %s failed\n", ecmd->argv[0]);
        exit (1);
        break;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        close(rcmd->fd);
        if (open(rcmd->file, rcmd->mode, S_IRWXU) < 0)
        {
            fprintf(stderr, "open %s failed\n", rcmd->file);
            exit(1);
        }
        run_cmd(rcmd->cmd);
        break;

    case LIST:
        lcmd = (struct listcmd*)cmd;
        if (fork1() == 0)
            run_cmd(lcmd->left);
        wait(NULL);
        run_cmd(lcmd->right);
        break;

    case PIPE:
        pcmd = (struct pipecmd*)cmd;
        if (pipe(p) < 0)
            panic("pipe");

        // Ejecución del hijo de la izquierda
        if (fork1() == 0)
        {
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            run_cmd(pcmd->left);
        }

        // Ejecución del hijo de la derecha
        if (fork1() == 0)
        {
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            run_cmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);

        // Esperar a ambos hijos
        wait(NULL);
        wait(NULL);
        break;

    case BACK:
        bcmd = (struct backcmd*)cmd;
        if (fork1() == 0)
            run_cmd(bcmd->cmd);
        break;
    }

    // Salida normal, código 0.
    exit(0);
}

// Muestra un *prompt* y lee lo que el usuario escribe usando la
// librería readline. Ésta permite almacenar en el historial, utilizar
// las flechas para acceder a las órdenes previas, búsquedas de
// órdenes, etc.
char*
getcmd()
{
    char *buf;
    int retval = 0;

    struct passwd *passwd;

    char *prompt;
    char *usuario;
    char *directorio;

    // Obtener el nombre del usuario usando la estructura passwd.
    uid_t uid = getuid();

    if (NULL != (passwd = getpwuid(uid))) {
        usuario = passwd->pw_name;
    }
    else {
        perror("getpwuid");
        exit(EXIT_FAILURE);
    }

    // Obtener el directorio en el que estamos.
    char buffDir[MAXPATH+1];

    if (NULL == (directorio = getcwd(buffDir, MAXPATH+1))) {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }

    // Concatenar prompt

    char buffprompt[MAXPATH+1];

    if (snprintf(buffprompt, MAXPATH+1, "%s@%s$ ", usuario, basename(directorio)) < 0) {
        perror("snprintf");
        exit(EXIT_FAILURE);
    }

    // Lee la entrada del usuario
    buf = readline (buffprompt);

    // Si el usuario ha escrito algo, almacenarlo en la historia.
    if(buf)
        add_history (buf);

    return buf;
}

// Función que implementa el comando interno pwd.
// Envía a la salida de error: simplesh: pwd:
// Muestra por pantalla la ruta completa en la que nos encontramos.
void
run_pwd(void)
{
    // Obtener el directorio en el que estamos.
    char *directorio;
    char buffDir[MAXPATH+1];

    if (NULL == (directorio = getcwd(buffDir, MAXPATH+1))) {
        perror("pwd");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "simplesh: pwd: ");
    printf("%s\n",directorio);
}

// Función que implementa el comando interno cd.
// Si no hay argumentos, devuelve el directorio HOME.
int run_cd(char * dir){

    char * homeDir = getenv("HOME");

    if (dir == NULL){
        chdir("/home/");
    } else {
		struct stat fileStat;
		if (stat(dir, &fileStat) == -1){
			perror("No existe el directorio");
			return -1;
		} //no existe
		else if (S_ISDIR(fileStat.st_mode) == 0){
			perror("No es un directorio");
			return -1;
		}
		chdir(dir);
	}
    return 0;
};

// Función para abrir ficheros
FILE * abrir_fichero(char * fichero, char * permiso) {
	FILE *fp;
	fp = fopen(fichero, permiso);

	if (fp == NULL){
		fprintf(stderr, "open %s failed\n", fichero);
		exit(1);
	}

	return fp;
}



//Función tee
int run_tee(char * argv[MAXARGS]){

	int aflag = 0;
	int c, argc, f = 0;
	FILE *fp;

	char buffer[10];
	FILE* fileArray[MAXFILES]; 
	int n = 0, i = 0;
	int escrito = 0;

	for (int i = 0; argv[i] != NULL; i++){
		argc++;	
	}

	fileArray[MAXFILES] = malloc(sizeof(FILE*)*MAXFILES);
	
	// Comprobación por si hay opciones (-h y/o -a)
	int contflag=0;
	while ((c = getopt(argc, argv, "ha")) != -1) {
		switch(c) {
			case 'h':
				printf("Uso: tee [-h] [-a] [FICHERO ]...\n");
				printf("\tCopia stdin a cada FICHERO y a stdout. \n");
				printf("\tOpciones: \n");
				printf("\t-a Añade al final de cada FICHERO\n");
				printf("\t-h help\n");
				return 0;
				break;
			case 'a':
				aflag=1;
				f = contflag+=aflag+1; // Añade el flag a y +1 para pasar a la posición siguiente a las opciones. 
				while(argv[f] != NULL){
					fp=abrir_fichero(argv[f], "a+");		
					fileArray[i] = fp; 
					i++;			
					f++;
				}
				break;
			default:	// Equivalente a '?' para cualquier otra opción.
				perror("Argumento incorrecto\n");
				return -1;
		}
	}	

	// Comprobación si existen ficheros y no han sido abiertos con el flag -a	
	if (!aflag) {	
		f = contflag+=1;
		while(argv[f] != NULL){
			fp=abrir_fichero(argv[f], "w+");
			fileArray[i] = fp; 
			i++;
			f++;
		}
	}

	// Lectura y escritura
    int written = 0;
	while ((n = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0){
		for (int k = 0; fileArray[k] != NULL; k++){
			written = fwrite(buffer, sizeof(char), sizeof(buffer), fileArray[k]);		  	
		}
		escrito = write(STDOUT_FILENO, buffer, sizeof(buffer));
	}

	for (int k = 0; fileArray[k] != NULL; k++){
		fclose(fileArray[k]);		
	}

	return 0;

}

void du_aux(char * path){

}


//Función du
int run_du(char * argv[MAXARGS]){

	int c, f, argc = 0;
	DIR * currentDir;
	FILE * fp;
	struct dirent * entry;
	char *path = (char*)malloc(sizeof(char) * MAXPATH);

	for (int i = 0; argv[i] != NULL; i++){
		argc++;
	}

	// Comprobación por si hay opciones (-h y/o -a)
	int contflag=0;
	while ((c = getopt(argc, argv, "hbt:v")) != -1) {
		switch(c) {
			case 'h':
				printf("Uso: du [-h] [-b] [-t SIZE] [- v] [FICHERO | DIRECTORIO]...\n");
				printf("Para cada fichero, imprime su tamano.\n");
				printf("Para cada directorio, imprime la suma de los tamaños de todos los ficheros de todos sus subdirectorios.\n");
				printf("\tOpciones:\n");
				printf("\t-b Imprime el tamano ocupado en disco por todos los bloques del fichero.\n");
				printf("\t-t SIZE Excluye todos los ficheros más pequeños que SIZE bytes, si es\n");
				printf("\tnegativo, o más grandes que SIZE bytes, si es positivo, cuando se procesa un directorio.\n");
				printf("\t-v procesa un directorio. Imprime el tamano de todos y cada uno\n");
				printf("\tde los ficheros cuando se procesa un directorio.\n");
				printf("\t-h help\n");
				printf("Nota: Todos los tamanos estan expresados en bytes.\n");
				return 0;
				break;
			case 'b':
        		contflag+=1;
				break;
      		case 't':
        		contflag+=1;
       			break;
      		case 'v':
        		contflag+=1;
        		break;
			default:	// Equivalente a '?' para cualquier otra opción.
				perror("Argumento incorrecto\n");
				return -1;
		}
	}

	if (contflag == 0){
		// Obtener el directorio en el que estamos.
		char buffDir[MAXPATH+1];

		if (NULL == (path = getcwd(buffDir, MAXPATH+1))) {
			perror("getcwd");
			exit(EXIT_FAILURE);
		}
	}else{
		f = contflag+=1;
		path = argv[f];		
	}

	if ((currentDir = opendir(path)) == NULL) {
		fprintf(stderr,"%s: Permiso denegado. \n", path);
		return -1;
	}else{
		printf("Path: %s\n",path);
	}

	return 0;

}


// Función `main()`.
// ----

int
main(void)
{
    char* buf;

    // Bucle de lectura y ejecución de órdenes.
    while (NULL != (buf = getcmd()))
    {
	// Si el comando es CD o EXIT no se crea el hijo

		struct cmd *cmd = parse_cmd(buf);
		struct execcmd *ecmd = (struct execcmd*)cmd;

		if (!strcmp(ecmd->argv[0], "cd")){
			run_cd(ecmd->argv[1]);
		} else if(!strcmp(ecmd->argv[0], "exit")){
			exit(0);
		}else if(fork1() == 0)		// Crear siempre un hijo para ejecutar el comando leído
            		run_cmd(cmd);

		// Esperar al hijo creado
        	wait(NULL);

        free ((void*)buf);
    }

    return 0;
}

// Como `fork()` salvo que muestra un mensaje de error si no se puede
// crear el hijo.
int
fork1(void)
{
    int pid;

    pid = fork();
    if(pid == -1)
        panic("fork");
    return pid;
}

/*
 * Local variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 75
 * eval: (auto-fill-mode t)
 * End:
 */
