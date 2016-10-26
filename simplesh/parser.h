#ifndef PARSER_H_
#define PARSER_H_

int gettoken(char **ps, char *end_of_str, char **q, char **eq);
int peek(char **ps, char *end_of_str, char *toks);

struct cmd *parse_cmd(char *s);
struct cmd *parse_line(char**, char*);
struct cmd *parse_pipe(char**, char*);
struct cmd *parse_redirs(struct cmd *cmd, char **ps, char *end_of_str);
struct cmd *parse_block(char **ps, char *end_of_str);
struct cmd *parse_exec(char**, char*);
struct cmd *nulterminate(struct cmd*);

#endif
