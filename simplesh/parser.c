#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>

#include <libgen.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

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

// Parsing
// ----

const char whitespace[] = " \t\r\n\v";
const char symbols[] = "<|>&;()";

// Obtiene un *token* de la cadena de entrada `ps`, y hace que `q` apunte a
// él (si no es `NULL`).
int
gettoken(char **ps, char *end_of_str, char **q, char **eq)
{
    char *s;
    int ret;

    s = *ps;
    while (s < end_of_str && strchr(whitespace, *s))
        s++;
    if (q)
        *q = s;
    ret = *s;
    switch (*s)
    {
    case 0:
        break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if (*s == '>')
        {
            ret = '+';
            s++;
        }
        break;

    default:
        // El caso por defecto (no hay caracteres especiales) es el de un
        // argumento de programa. Se retorna el valor `'a'`, `q` apunta al
        // mismo (si no era `NULL`), y `ps` se avanza hasta que salta todos
        // los espacios **después** del argumento. `eq` se hace apuntar a
        // donde termina el argumento. Así, si `ret` es `'a'`:
        //
        //     |-----------+---+---+---+---+---+---+---+---+---+-----------|
        //     | (espacio) | a | r | g | u | m | e | n | t | o | (espacio) |
        //     |-----------+---+---+---+---+---+---+---+---+---+-----------|
        //                   ^                                   ^
        //                   |q                                  |eq
        //
        ret = 'a';
        while (s < end_of_str && !strchr(whitespace, *s) && !strchr(symbols, *s))
            s++;
        break;
    }

    // Apuntar `eq` (si no es `NULL`) al final del argumento.
    if (eq)
        *eq = s;

    // Y finalmente saltar los espacios en blanco y actualizar `ps`.
    while(s < end_of_str && strchr(whitespace, *s))
        s++;
    *ps = s;

    return ret;
}

// La función `peek()` recibe un puntero a una cadena, `ps`, y un final de
// cadena, `end_of_str`, y un conjunto de tokens (`toks`). El puntero
// pasado, `ps`, es llevado hasta el primer carácter que no es un espacio y
// posicionado ahí. La función retorna distinto de `NULL` si encuentra el
// conjunto de caracteres pasado en `toks` justo después de los posibles
// espacios.
int
peek(char **ps, char *end_of_str, char *toks)
{
    char *s;

    s = *ps;
    while(s < end_of_str && strchr(whitespace, *s))
        s++;
    *ps = s;

    return *s && strchr(toks, *s);
}

// Función principal que hace el *parsing* de una línea de órdenes dada por
// el usuario. Llama a la función `parse_line()` para obtener la estructura
// `cmd`.
struct cmd*
parse_cmd(char *s)
{
    char *end_of_str;
    struct cmd *cmd;

    end_of_str = s + strlen(s);
    cmd = parse_line(&s, end_of_str);

    peek(&s, end_of_str, "");
    if (s != end_of_str)
    {
        fprintf(stderr, "restante: %s\n", s);
        panic("syntax");
    }

    // Termina en `'\0'` todas las cadenas de caracteres de `cmd`.
    nulterminate(cmd);

    return cmd;
}

// *Parsing* de una línea. Se comprueba primero si la línea contiene alguna
// tubería. Si no, puede ser un comando en ejecución con posibles
// redirecciones o un bloque. A continuación puede especificarse que se
// ejecuta en segundo plano (con `&`) o simplemente una lista de órdenes
// (con `;`).
struct cmd*
parse_line(char **ps, char *end_of_str)
{
    struct cmd *cmd;

    cmd = parse_pipe(ps, end_of_str);
    while (peek(ps, end_of_str, "&"))
    {
        gettoken(ps, end_of_str, 0, 0);
        cmd = backcmd(cmd);
    }

    if (peek(ps, end_of_str, ";"))
    {
        gettoken(ps, end_of_str, 0, 0);
        cmd = listcmd(cmd, parse_line(ps, end_of_str));
    }

    return cmd;
}

// *Parsing* de una posible tubería con un número de órdenes.
// `parse_exec()` comprobará la orden, y si al volver el siguiente *token*
// es un `'|'`, significa que se puede ir construyendo una tubería.
struct cmd*
parse_pipe(char **ps, char *end_of_str)
{
    struct cmd *cmd;

    cmd = parse_exec(ps, end_of_str);
    if (peek(ps, end_of_str, "|"))
    {
        gettoken(ps, end_of_str, 0, 0);
        cmd = pipecmd(cmd, parse_pipe(ps, end_of_str));
    }

    return cmd;
}


// Construye los comandos de redirección si encuentra alguno de los
// caracteres de redirección.
struct cmd*
parse_redirs(struct cmd *cmd, char **ps, char *end_of_str)
{
    int tok;
    char *q, *eq;

    // Si lo siguiente que hay a continuación es una redirección...
    while (peek(ps, end_of_str, "<>"))
    {
        // La elimina de la entrada
        tok = gettoken(ps, end_of_str, 0, 0);

        // Si es un argumento, será el nombre del fichero de la
        // redirección. `q` y `eq` tienen su posición.
        if (gettoken(ps, end_of_str, &q, &eq) != 'a')
            panic("missing file for redirection");

        switch(tok)
        {
        case '<':
            cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>':
            cmd = redircmd(cmd, q, eq, O_RDWR|O_CREAT|O_TRUNC, 1);
            break;
        case '+':  // >>
            cmd = redircmd(cmd, q, eq, O_RDWR|O_CREAT|O_APPEND, 1);
            break;
        }
    }

    return cmd;
}

// *Parsing* de un bloque de órdenes delimitadas por paréntesis.
struct cmd*
parse_block(char **ps, char *end_of_str)
{
    struct cmd *cmd;

    // Esperar e ignorar el paréntesis
    if (!peek(ps, end_of_str, "("))
        panic("parse_block");
    gettoken(ps, end_of_str, 0, 0);

    // Parse de toda la línea hsta el paréntesis de cierre
    cmd = parse_line(ps, end_of_str);

    // Elimina el paréntesis de cierre
    if (!peek(ps, end_of_str, ")"))
        panic("syntax - missing )");
    gettoken(ps, end_of_str, 0, 0);

    // ¿Posibles redirecciones?
    cmd = parse_redirs(cmd, ps, end_of_str);

    return cmd;
}

// Hace en *parsing* de una orden, a no ser que la expresión comience por
// un paréntesis. En ese caso, se inicia un grupo de órdenes para ejecutar
// las órdenes de dentro del paréntesis (llamando a `parse_block()`).
struct cmd*
parse_exec(char **ps, char *end_of_str)
{
    char *q, *eq;
    int tok, argc;
    struct execcmd *cmd;
    struct cmd *ret;

    // ¿Inicio de un bloque?
    if (peek(ps, end_of_str, "("))
        return parse_block(ps, end_of_str);

    // Si no, lo primero que hay una línea siempre es una orden. Se
    // construye el `cmd` usando la estructura `execcmd`.
    ret = execcmd();
    cmd = (struct execcmd*)ret;

    // Bucle para separar los argumentos de las posibles redirecciones.
    argc = 0;
    ret = parse_redirs(ret, ps, end_of_str);
    while (!peek(ps, end_of_str, "|)&;"))
    {
        if ((tok=gettoken(ps, end_of_str, &q, &eq)) == 0)
            break;

        // Aquí tiene que reconocerse un argumento, ya que el bucle para
        // cuando hay un separador
        if (tok != 'a')
            panic("syntax");

        // Apuntar el siguiente argumento reconocido. El primero será la
        // orden a ejecutar.
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS)
            panic("too many args");

        // Y de nuevo apuntar posibles redirecciones
        ret = parse_redirs(ret, ps, end_of_str);
    }

    // Finalizar las líneas de órdenes
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;

    return ret;
}

// Termina en NUL todas las cadenas de `cmd`.
struct cmd*
nulterminate(struct cmd *cmd)
{
    int i;
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if(cmd == 0)
        return 0;

    switch(cmd->type)
    {
    case EXEC:
        ecmd = (struct execcmd*)cmd;
        for(i=0; ecmd->argv[i]; i++)
            *ecmd->eargv[i] = 0;
        break;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        nulterminate(rcmd->cmd);
        *rcmd->efile = 0;
        break;

    case PIPE:
        pcmd = (struct pipecmd*)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;

    case LIST:
        lcmd = (struct listcmd*)cmd;
        nulterminate(lcmd->left);
        nulterminate(lcmd->right);
        break;

    case BACK:
        bcmd = (struct backcmd*)cmd;
        nulterminate(bcmd->cmd);
        break;
    }

    return cmd;
}
