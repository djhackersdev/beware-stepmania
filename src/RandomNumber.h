/*this is the recommended way to call rand to get numbers, because C's random generator
has bad randomness in the low bits (aka, it sucks). do not use "rand() % n"
SM is known to have awful randomness for this reason -beware */
#define randomnumber(X)        int(rand()/(RAND_MAX + 1.0) * X );

