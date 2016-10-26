#ifndef STRUCTS_H_
#define STRUCTS_H_

#define MAXARGS 15

// Estructuras
// -----

// La estructura `cmd` se utiliza para almacenar la información que
// servirá al shell para guardar la información necesaria para
// ejecutar las diferentes tipos de órdenes (tuberías, redirecciones,
// etc.)
//
// El formato es el siguiente:
//
//     |----------+--------------+--------------|
//     | (1 byte) | ...          | ...          |
//     |----------+--------------+--------------|
//     | type     | otros campos | otros campos |
//     |----------+--------------+--------------|
//
// Nótese cómo las estructuras `cmd` comparten el primer campo `type`
// para identificar el tipo de la estructura, y luego se obtendrá un
// tipo derivado a través de *casting* forzado de tipo. Se obtiene así
// un polimorfismo básico en C.
struct cmd {
    int type;
};

// Ejecución de un comando con sus parámetros
struct execcmd {
    int type;
    char * argv[MAXARGS];
    char * eargv[MAXARGS];
};

// Ejecución de un comando de redirección
struct redircmd {
    int type;
    struct cmd *cmd;
    char *file;
    char *efile;
    int mode;
    int fd;
};

// Ejecución de un comando de tubería
struct pipecmd {
    int type;
    struct cmd *left;
    struct cmd *right;
};

// Lista de órdenes
struct listcmd {
    int type;
    struct cmd *left;
    struct cmd *right;
};

// Tarea en segundo plano (background) con `&`.
struct backcmd {
    int type;
    struct cmd *cmd;
};

struct cmd* execcmd(void);
struct cmd* redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd);
struct cmd* pipecmd(struct cmd *left, struct cmd *right);
struct cmd* listcmd(struct cmd *left, struct cmd *right);
struct cmd* backcmd(struct cmd *subcmd);

#endif
