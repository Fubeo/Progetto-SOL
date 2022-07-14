#ifndef CUSTOM_PRINT_H
#define CUSTOM_PRINT_H

#define RD    "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"


enum Color { CYAN = 'c', GREEN = 'g', YELLOW = 'y', BLUE = 'b', MAGENTA = 'm', RED = 'r', WHITE = 'w', STANDARD = 'd'};

void pcolor(enum Color c, char* s, ...);

void psucc(char *s, ...);

void pwarn(char *s, ...);

void perr(char *s, ...);


#endif
