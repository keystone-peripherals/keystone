#ifndef __DOWNLOAD_H__
#define __DOWNLOAD_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <malloc.h>

#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "fcntl.h"
#include <ucontext.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define REQUEST_STRINGS_BUF_SIZE 1024
#define RESPONSE_BUF_SIZE 16384 // Max size for TLS

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Downloads a file from a website via OpenSSL to a specified buffer.
 * 
 * @param out Pointer to buffer where file should be downloaded to.
 * @param host Website host name.
 * @param file_to_download Path to the file on the host.
 * @return long: size of file downloaded
 */
long download_to(unsigned char* out, char* host, char* file_to_download);

/**
 * @brief Downloads a file from a website via OpenSSL. Self allocates memory for file.
 * 
 * @param out Pointer to output pointer to the downloaded file.
 * @param host Website host name.
 * @param file_to_download Path to the file on the host.
 * @param alignment Memory alignment of out. If out does not need to be memory aligned, pass in 0.
 * @return long: size of file downloaded
 */
long download(unsigned char** out, char* host, char* file_to_download, size_t alignment);

#define print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, x509) __print_errors_and_clean_openssl(__LINE__, ctx, connection_bio, certificate_bio, x509)
void __print_errors_and_clean_openssl(int line, SSL_CTX* ctx, BIO* connection_bio, BIO* certificate_bio, X509* x509);

#ifdef __cplusplus
}
#endif

#endif // __DOWNLOAD_H__
