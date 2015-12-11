#include <stdlib.h>

/*
 * This function is used to get sithe of the next chunk
 * Errors are encrupted in negative values.
 * Maximal and minimal size of the chunk are fixed.
 * Use this function to get ephasize identical chunks from similar files.
*/
int get_chunk_len( char * buff, ssize_t max_len );

#define MIN_FILE_SIZE_FOR_REPRESENTATIVE_HASH (10*1024)
char* get_representative_hash( char* file_beginning );

char* get_hash_of_chunk( char* chunk, ssize_t chunk_size);



