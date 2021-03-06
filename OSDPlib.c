//
// Created by lesaha on 12.12.15.
//
#define DEBUG
#include "OSDPlib.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include "log.h"
#include <sys/mman.h>
#include <libgen.h>


int64_t pow_mod(int64_t a, uint64_t n, uint64_t mod) {
    if (n == 0) {
        return 1;
    }
    int64_t temp = a;
    while (n > 1) {
        temp = (temp * a) % mod;

        n--;
    }
    return temp;
}

int64_t rabin_fingerprint(char *buf, ssize_t length, ssize_t window_size) {
    int64_t i = 0;
    int64_t a = 3;
    int64_t mod = 1024 * 1024;
    int64_t an = pow_mod(a, window_size, mod);
    int64_t res = 0;
    for (i = 0; i < length; i++) {
        res = (res * a + buf[i]) % mod;
        if (i >= window_size) {
            res = (res - an * buf[i - window_size]) % mod;
        }
    }
    return res;
}

ssize_t get_offset_to_next_chunk(char *buf, ssize_t length) {
    int64_t i = 0;
    int64_t a = 3;
    int64_t mod = RABIN_MOD;
    int64_t res = 0;
    int64_t an = pow_mod(a, WINDOW_SIZE, mod);
    length = length < MAX_CHUNK_SIZE ? length : MAX_CHUNK_SIZE;
    if (length < WINDOW_SIZE) {
        return length;
    }
    while (i < length) {
        res = (res * a + buf[i]) % mod;
        if (i >= WINDOW_SIZE) {
            res = (res - an * buf[i - WINDOW_SIZE]) % mod;
        }
        if (res == 0 && i > MIN_CHUNK_SIZE)
            return i - WINDOW_SIZE;
        i++;
    }
    return length;
}


int get_md5_of_chunk(unsigned char *result, char *data, ssize_t size) {
    MD5_CTX mdContext;
    MD5_Init(&mdContext);
    MD5_Update(&mdContext, data, size);
    MD5_Final((unsigned char *) result, &mdContext);
    return 0;
}

int get_hash_string_representation(char *string_representation, unsigned char *md5_hash) {
    int i;
    for (i = 0; i < MD5_DIGEST_LENGTH; i++)
        sprintf(string_representation + i, "%02x", md5_hash[i]);
    return 0;
}

int store_file(char *name,const char *base_dir) {
    int fd;
    int fis;
    int res;
    int ret_value = -1;
    struct stat sb;
    char *fis_path;
    char *fdata_path;
    char *temp_path;
    char *path_to_saved_chunk;
    char representative_chunk_hash[MD5_DIGEST_LENGTH * 2 + 1];
    char *mmap_start;
    chunk_info* fis_data;
    ssize_t fis_size = 100;
    ssize_t length;
    ssize_t size_of_next_chunk = 0;
    ssize_t offset_to_this_chunk = 0;
    LOGMESG(LOG_ERROR,"Saving file %s", name);
    char temp_chunk[MD5_DIGEST_LENGTH * 2 + 1];
    if (name == NULL || base_dir == NULL) {
        LOGMESG(LOG_ERROR, "Wrong name");
        goto store_file_exit_err_0;
    }
    fd = open(name, O_RDONLY);
    if (fd < 0) {
        LOGMESG(LOG_ERROR, "Can not open file");
        goto store_file_exit_err_0;
    }

    fis_path = (char *) malloc(strlen(name) + strlen(FIS_BASE_NAME) + strlen(base_dir) + 2);// need check
    fdata_path = (char *) malloc(strlen(name) + strlen(FDATA_BASE_NAME) + MD5_DIGEST_LENGTH * 2 + 2); //need check
    path_to_saved_chunk = (char *) malloc(strlen(name) + strlen(FDATA_BASE_NAME) + MD5_DIGEST_LENGTH * 2 + MD5_DIGEST_LENGTH * 2+ 3); //need check
    fis_data = (chunk_info*) malloc( sizeof(chunk_info)*fis_size );
    fis_data[0].length_of_chunk = 0;
    sprintf(fis_path, "%s%s%s", base_dir, FIS_BASE_NAME, basename(name));
    LOGMESG(LOG_INFO,"fis_path = %s fdata_path = %s",fis_path ,  fdata_path);
    fis = open(fis_path, O_EXCL| O_CREAT | O_WRONLY, 0666);
    if (fis < 0) {
        perror("fis file creating");
        LOGMESG(LOG_ERROR, "This file is already slored");
        goto store_file_exit_err_1;
    }
    fstat(fd, &sb);
    length = sb.st_size;
    LOGMESG(LOG_INFO, "File size = %ld", sb.st_size);
    mmap_start = (char*)mmap(NULL, sb.st_size, PROT_READ,
                      MAP_SHARED, fd, 0);
    if (mmap_start == NULL) {
        LOGMESG(LOG_WARN, "Can not create mmap");
        goto store_file_exit_err_2;
    }
    get_representative_hash(representative_chunk_hash,mmap_start, length);
    strcpy((char*)fis_data[0].md5_hash, representative_chunk_hash);
    sprintf(fdata_path, "%s%s%s", base_dir, FDATA_BASE_NAME, representative_chunk_hash);
    res = mkdir(fdata_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (fis < 0) {
        LOGMESG(LOG_ERROR, "This directory is already exist: %s", fdata_path);
        goto store_file_exit_err_3;
    }



    unsigned char md5[MD5_DIGEST_LENGTH];
    while (offset_to_this_chunk + size_of_next_chunk < length) {
        offset_to_this_chunk = offset_to_this_chunk + size_of_next_chunk;
        size_of_next_chunk = get_offset_to_next_chunk(mmap_start + offset_to_this_chunk,
                                                      length - offset_to_this_chunk);
        get_md5_of_chunk(md5, mmap_start + offset_to_this_chunk, size_of_next_chunk);
        get_hash_string_representation(temp_chunk, md5);
        sprintf(path_to_saved_chunk, "%s/%s",fdata_path,temp_chunk);
        save_chunk_data(path_to_saved_chunk, mmap_start+offset_to_this_chunk, size_of_next_chunk);
        if ( fis_data[0].length_of_chunk+1 >= fis_size){
            fis_size*=2;
            fis_data = (chunk_info*)realloc(fis_data, sizeof(chunk_info)*fis_size );
        }
        fis_data[0].length_of_chunk = fis_data[0].length_of_chunk+1;
        fis_data[fis_data[0].length_of_chunk].length_of_chunk = size_of_next_chunk;
        fis_data[fis_data[0].length_of_chunk].offset_in_file = offset_to_this_chunk;
        strcpy(fis_data[fis_data[0].length_of_chunk].md5_hash,temp_chunk );
        LOGMESG(LOG_INFO,"Storing data offset = %llu length = %llu", offset_to_this_chunk, size_of_next_chunk);
    }
    write(fis, (char*)fis_data,sizeof(chunk_info)*(fis_data[0].length_of_chunk +1) );
    ret_value = 0;


    store_file_exit_err_3:
    munmap(mmap_start, sb.st_size);
    store_file_exit_err_2:
    free(fis_path);
    free(fdata_path);
    free(path_to_saved_chunk);
    free(fis_data);
    store_file_exit_err_1:
    close(fd);
    store_file_exit_err_0:
    return ret_value;
}

int get_representative_hash(char *result, char *file_beginning, ssize_t length) {
    ssize_t size_of_next_chunk = 0;
    ssize_t offset_to_this_chunk = 0;
    length = length < MAX_SIZE_FOR_REPRESENTATIVE_CHUNK_FINDING ? length : MAX_SIZE_FOR_REPRESENTATIVE_CHUNK_FINDING;
    char min_chunk[MD5_DIGEST_LENGTH * 2 + 1];
    char temp_chunk[MD5_DIGEST_LENGTH * 2 + 1];
    unsigned char md5[MD5_DIGEST_LENGTH];
    size_of_next_chunk = get_offset_to_next_chunk(file_beginning, length);
    get_md5_of_chunk(md5, file_beginning, size_of_next_chunk);
    get_hash_string_representation(min_chunk, md5);
    while (offset_to_this_chunk + size_of_next_chunk < length) {
        offset_to_this_chunk = offset_to_this_chunk + size_of_next_chunk;
        size_of_next_chunk = get_offset_to_next_chunk(file_beginning + offset_to_this_chunk,
                                                      length - offset_to_this_chunk);
        get_md5_of_chunk(md5, file_beginning + offset_to_this_chunk, size_of_next_chunk);
        get_hash_string_representation(temp_chunk, md5);
        if (strcmp(temp_chunk, min_chunk) < 0) {
            strcpy(min_chunk, temp_chunk);
        }
    }
    strcpy(result, min_chunk);

}

int save_chunk_data(char *path_for_data_to_save, char *data, ssize_t length) {
    int fd;
    saved_chunk_header ch_header;
    LOGMESG(LOG_INFO, "Saveing to %s, %llu bytes", path_for_data_to_save, length);
    fd = open(path_for_data_to_save, O_CREAT | O_RDWR , 0666);
    if (fd<0){
        LOGMESG(LOG_ERROR, "Can not open file");
        return -1;
    }
    struct stat sb;
    fstat(fd, &sb);
    if (sb.st_size != 0) {
        LOGMESG(LOG_INFO, "This chunk already was stored!");
        if ( read(fd, (char*)&ch_header, sizeof(saved_chunk_header))==sizeof(saved_chunk_header)){
            ch_header.ref_counter = ch_header.ref_counter+1;
            lseek(fd, 0, SEEK_SET);
            if ( write(fd, (char*)&ch_header, sizeof(saved_chunk_header))==sizeof(saved_chunk_header)){
                close(fd);
                return 0;
            }
        }
    }else{
        ch_header.length_of_chunk = length;
        ch_header.ref_counter = 1;
        if ( write(fd, (char*)&ch_header, sizeof(saved_chunk_header))!=sizeof(saved_chunk_header)){
            LOGMESG(LOG_ERROR, "Can not write to file");
            close(fd);
            return -1;
        }
        ssize_t written = 0;
        ssize_t len;
        while ( written<length){
            len = write(fd, data+written, length);//(1024*4) < (length-written)?(1024*4):(length-written) );
            if ( len == 0){
                LOGMESG(LOG_ERROR, "Can not write to file");
                close(fd);
                return -1;
            }
            written += len;
        }
        close(fd);
        return 0;
    }
    close(fd);
    return -1;
}

int restore_file(char *name, const char* new_name , const char *base_dir) {
    int ret_val = -1;
    int fd, fis;
    struct stat sb;
    char *fis_path;
    char *fdata_path;
    char *temp_path;
    char *path_to_saved_chunk;
    char representative_chunk_hash[MD5_DIGEST_LENGTH * 2 + 1];
    char *mmap_start;
    char * temp_data;
    ssize_t i;
    chunk_info* fis_data;
    ssize_t fis_size = 100;
    ssize_t length;
    ssize_t size_of_next_chunk = 0;
    ssize_t offset_to_this_chunk = 0;
    LOGMESG(LOG_INFO,"Restoring file %s", name);
    if (name == NULL || base_dir == NULL || new_name == NULL) {
        LOGMESG(LOG_ERROR, "Wrong name");
        goto restore_file_exit_err_0;
    }
    fd = open(new_name, O_EXCL| O_CREAT | O_WRONLY, 0666);
    if (fd < 0) {
        perror("new_name file creating");
        LOGMESG(LOG_ERROR, "This file is already slored");
        goto restore_file_exit_err_1;
    }
    fis_path = (char *) malloc(strlen(name) + strlen(FIS_BASE_NAME) + strlen(base_dir) + 2);// need check
    fdata_path = (char *) malloc(strlen(name) + strlen(FDATA_BASE_NAME) + MD5_DIGEST_LENGTH * 2 + 2); //need check
    path_to_saved_chunk = (char *) malloc(strlen(name) + strlen(FDATA_BASE_NAME) + MD5_DIGEST_LENGTH * 2 + MD5_DIGEST_LENGTH * 2+ 3); //need check
    temp_data = (char*) malloc(MAX_CHUNK_SIZE);
    sprintf(fis_path, "%s%s%s", base_dir, FIS_BASE_NAME, name);
    fis = open(fis_path, O_RDONLY);
    if (fis < 0) {
        LOGMESG(LOG_ERROR, "Can not open fis: %s", fis_path);
        goto restore_file_exit_err_2;
    }
    fstat(fis, &sb);
    fis_data = (chunk_info*) malloc(sb.st_size);
    read(fis,(char*)fis_data, sb.st_size );
    sprintf(fdata_path, "%s%s%s", base_dir, FDATA_BASE_NAME, fis_data[0].md5_hash);
    for( i = 1; i <= fis_data[0].length_of_chunk; i++ ){
        LOGMESG(LOG_INFO,"Restoring data offset = %llu length = %llu", fis_data[i].offset_in_file, fis_data[i].length_of_chunk);
        int fdata;
        sprintf(path_to_saved_chunk, "%s/%s",fdata_path,fis_data[i].md5_hash);
        fdata = open(path_to_saved_chunk,O_RDONLY );
        lseek(fdata, sizeof(saved_chunk_header), SEEK_SET);
        read(fdata, temp_data, fis_data[i].length_of_chunk);
        lseek(fd, fis_data[i].offset_in_file, SEEK_SET);
        write(fd, temp_data, fis_data[i].length_of_chunk);
        close(fdata);
    }
    ret_val = 0;
    close(fis);
    restore_file_exit_err_2:
        free(fis_path);
        free(fdata_path);
        free(path_to_saved_chunk);
        free(temp_data);
    close(fd);
    restore_file_exit_err_1:
    restore_file_exit_err_0:
    return ret_val;
}
