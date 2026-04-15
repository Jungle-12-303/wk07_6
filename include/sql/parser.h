#ifndef PARSER_H
#define PARSER_H

#include "statement.h"

/* Parse a single SQL line.  Returns 0 on success, -1 on error. */
int parse(const char *input, statement_t *stmt);

#endif /* PARSER_H */
