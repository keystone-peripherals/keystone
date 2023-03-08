#include "download.h"

#include "../util/util.h"

extern unsigned int certificate_bytes_len;
extern unsigned char certificate_bytes[];

extern "C" {
  long download_to(unsigned char* out, char* host, char* file_to_download) {
    SSL_library_init(); SSL_load_error_strings();

    const SSL_METHOD* method = TLS_client_method();
    if (method == NULL) return -1;

    SSL_CTX* ctx = SSL_CTX_new(method);
    if (ctx == NULL) return -1;

    BIO* certificate_bio = NULL;
    X509* cert = NULL;
    certificate_bio = BIO_new_mem_buf((void*)certificate_bytes, certificate_bytes_len);

    // Loads PEM certificate(s) from .crt or .pem file specified in CMakeLists.txt.
    // There may be more than one PEM certificate in the file. Reads them all in the while loop.
    while ((cert = PEM_read_bio_X509(certificate_bio, NULL, 0, NULL)) != NULL) {
      X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), cert);
    }

    BIO* connection_bio = BIO_new_ssl_connect(ctx);
    if (connection_bio == NULL) {
      // ctx is automatically freed when BIO_new_ssl_connect fails
      print_errors_and_clean_openssl(NULL, NULL, certificate_bio, cert);
      return -1;
    }

    SSL* ssl = NULL; // automatically freed when ctx is freed
    BIO_get_ssl(connection_bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

    if (SSL_set_tlsext_host_name(ssl, host) != 1) {
      printf("!!! SSL_set_tlsext_host_name() failed!\n");
      return -1;
    }

    char name[REQUEST_STRINGS_BUF_SIZE];
    sprintf(name, "%s:%s", host, "https"); 
    BIO_set_conn_hostname(connection_bio, name); 

    int connection_tries;
    int bio_do_connect_result;
    do {
      // printf("!!! Trying BIO_do_connect()...\n");
      bio_do_connect_result = BIO_do_connect(connection_bio);
      connection_tries += 1;
    } while (BIO_should_retry(connection_bio));
    record(connection_tries_phase, connection_tries);

    if (bio_do_connect_result <= 0) {
      printf("!!! BIO_do_connect() failed! errno: %d, retval: %d\n", errno, bio_do_connect_result);
      print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
      return -1;
    }

    long verify_flag = SSL_get_verify_result(ssl); 
    if (verify_flag != X509_V_OK) {
      // https://www.openssl.org/docs/man1.0.2/man1/verify.html
      // printf("!!! SSL_get_verify_result not successful: (%i)\n", (int)verify_flag);
      print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
      return -1;
    }
    // printf("!!! SSL_get_verify_result succeeded!\n");

    char request[REQUEST_STRINGS_BUF_SIZE];
    sprintf(request, 
            "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: Close\r\n"
            "\r\n",
            file_to_download,
            host); 
    BIO_puts(connection_bio, request);

    char* response = new char[RESPONSE_BUF_SIZE];
    char* header = new char[REQUEST_STRINGS_BUF_SIZE]; char* header_cur = header;
    unsigned char* content = out; unsigned char* content_cur;
    long content_length;
    bool header_finished = false;

    const char* end_of_header_token = "\r\n\r\n";
    const int end_of_header_token_len = 4;
    while (true) { 
      memset(response, '\0', RESPONSE_BUF_SIZE);
      int n = BIO_read(connection_bio, response, RESPONSE_BUF_SIZE);
      // printf("!!! bytes read from BIO_read: %d\n", n);
      if (n == 0) {
        break; // File completely read successfully
      } else if (n == -1) {
        if (BIO_should_retry(connection_bio)) {
          // printf("!!! Retrying BIO_read()...\n");
          continue;
        } else {
          print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
          // printf("!!! BIO_read failed with ret -1\n");
          return -1;
        }
      } else if (n == -2) {
        print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
        // printf("!!! BIO_read failed with ret -2\n");
        return -1;
      } 

      if (!header_finished) {
        char* end_of_header_token_ptr = strstr(response, end_of_header_token);
        if (end_of_header_token_ptr == NULL) {
          memcpy(header_cur, response, n);
          header_cur += n;
        } else {
          memcpy(header_cur, response, end_of_header_token_ptr + end_of_header_token_len - response);
          const char* content_length_token = "Content-Length: ";
          const int content_length_token_len = 16;
          char* content_length_str = strstr(header, content_length_token) + content_length_token_len;
          char* content_length_str_end = strchr(content_length_str, '\r');
          *content_length_str_end = '\0';
          content_length = atol(content_length_str);
          *content_length_str_end = '\r';

          content_cur = content;

          memcpy(content_cur, end_of_header_token_ptr + end_of_header_token_len, (response + n) - (end_of_header_token_ptr + end_of_header_token_len));
          content_cur += (response + n) - (end_of_header_token_ptr + end_of_header_token_len);
          header_cur[end_of_header_token_ptr + end_of_header_token_len - response] = '\0';

          header_finished = true;
          }
      } else {
        memcpy(content_cur, response, n);
        content_cur += n;
      }
    }
    // printf("!!! Sucessfully downloaded to memory\n");

    // printf("First 32 bytes out of %ld byte file: ", content_length);
    // for (int i = 0; i < 32; i += 1) printf("0x%02X, ", content[i]);
    // printf("...\n");
    // printf("\n");

    // HTML response header is not being used
    // free(header);
    delete[] header;

    return content_length;
  }

  long download(unsigned char** out, char* host, char* file_to_download, size_t alignment) {
    SSL_library_init(); SSL_load_error_strings();

    const SSL_METHOD* method = TLS_client_method();
    if (method == NULL) return -1;

    SSL_CTX* ctx = SSL_CTX_new(method);
    if (ctx == NULL) return -1;

    BIO* certificate_bio = NULL;
    X509* cert = NULL;
    certificate_bio = BIO_new_mem_buf((void*)certificate_bytes, certificate_bytes_len);

    // Loads PEM certificate(s) from .crt or .pem file specified in CMakeLists.txt.
    // There may be more than one PEM certificate in the file. Reads them all in the while loop.
    while ((cert = PEM_read_bio_X509(certificate_bio, NULL, 0, NULL)) != NULL) {
      X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), cert);
    }

    BIO* connection_bio = BIO_new_ssl_connect(ctx);
    if (connection_bio == NULL) {
      // ctx is automatically freed when BIO_new_ssl_connect fails
      print_errors_and_clean_openssl(NULL, NULL, certificate_bio, cert);
      return -1;
    }

    SSL* ssl = NULL; // automatically freed when ctx is freed
    BIO_get_ssl(connection_bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

    if (SSL_set_tlsext_host_name(ssl, host) != 1) {
      printf("!!! SSL_set_tlsext_host_name() failed!\n");
      return -1;
    }

    char name[REQUEST_STRINGS_BUF_SIZE];
    sprintf(name, "%s:%s", host, "https"); 
    BIO_set_conn_hostname(connection_bio, name); 

    int bio_do_connect_result;
    do {
      // printf("!!! Trying BIO_do_connect()...\n");
      bio_do_connect_result = BIO_do_connect(connection_bio);
    } while (BIO_should_retry(connection_bio));

    if (bio_do_connect_result <= 0) {
      printf("!!! BIO_do_connect() failed! errno: %d, retval: %d\n", errno, bio_do_connect_result);
      print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
      return -1;
    }

    long verify_flag = SSL_get_verify_result(ssl); 
    if (verify_flag != X509_V_OK) {
      // https://www.openssl.org/docs/man1.0.2/man1/verify.html
      printf("!!! SSL_get_verify_result not successful: (%i)\n", (int)verify_flag);
      print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
      return -1;
    }
    // printf("!!! SSL_get_verify_result succeeded!\n");

    char request[REQUEST_STRINGS_BUF_SIZE];
    sprintf(request, 
            "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: Close\r\n"
            "\r\n",
            file_to_download,
            host); 
    BIO_puts(connection_bio, request);

    char* response = (char*)malloc(RESPONSE_BUF_SIZE);
    char* header = (char*)malloc(REQUEST_STRINGS_BUF_SIZE); char* header_cur = header;
    unsigned char* content; unsigned char* content_cur;
    long content_length;
    bool header_finished = false;

    const char* end_of_header_token = "\r\n\r\n";
    const int end_of_header_token_len = 4;
    while (true) { 
      memset(response, '\0', RESPONSE_BUF_SIZE);
      int n = BIO_read(connection_bio, response, RESPONSE_BUF_SIZE);
      // printf("!!! bytes read from BIO_read: %d\n", n);
      if (n == 0) {
        break; // File completely read successfully
      } else if (n == -1) {
        if (BIO_should_retry(connection_bio)) {
          // printf("!!! Retrying BIO_read()...\n");
          continue;
        } else {
          print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
          // printf("!!! BIO_read failed with ret -1\n");
          return -1;
        }
      } else if (n == -2) {
        print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
        // printf("!!! BIO_read failed with ret -2\n");
        return -1;
      } 

      if (!header_finished) {
        char* end_of_header_token_ptr = strstr(response, end_of_header_token);
        if (end_of_header_token_ptr == NULL) {
          memcpy(header_cur, response, n);
          header_cur += n;
        } else {
          memcpy(header_cur, response, end_of_header_token_ptr + end_of_header_token_len - response);
          const char* content_length_token = "Content-Length: ";
          const int content_length_token_len = 16;
          char* content_length_str = strstr(header, content_length_token) + content_length_token_len;
          char* content_length_str_end = strchr(content_length_str, '\r');
          *content_length_str_end = '\0';
          content_length = atol(content_length_str);
          *content_length_str_end = '\r';

          if (alignment == 0) {
            content = (unsigned char*)malloc(content_length);
            if (content == NULL) {
              print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
              free(header);
              // printf("!!! malloc failed on line %d!\n", __LINE__);
            }
          } else {
            int content_length_pages = (content_length / alignment) + 1;
            int content_length_aligned = content_length_pages * alignment;
            if (posix_memalign((void**)&content, alignment, content_length_aligned) != 0) {
              print_errors_and_clean_openssl(ctx, connection_bio, certificate_bio, cert);
              free(header);
              // printf("!!! posix_memalign failed on line %d!\n", __LINE__);
              return -1;
            }

            memset(content + content_length, 0, content_length_aligned - content_length);
            content_length = content_length_aligned;
          }

          content_cur = content;

          memcpy(content_cur, end_of_header_token_ptr + end_of_header_token_len, (response + n) - (end_of_header_token_ptr + end_of_header_token_len));
          content_cur += (response + n) - (end_of_header_token_ptr + end_of_header_token_len);
          header_cur[end_of_header_token_ptr + end_of_header_token_len - response] = '\0';

          header_finished = true;
        }
      } else {
        memcpy(content_cur, response, n);
        content_cur += n;
      }
    }
    // printf("!!! Sucessfully downloaded to memory\n");

    // printf("First 32 bytes out of %ld byte file: ", content_length);
    // for (int i = 0; i < 32; i += 1) printf("0x%02X, ", content[i]);
    // printf("...\n");
    // printf("\n");

    // HTML response header is not being used
    free(header);
    *out = content;
    return content_length;
  }

  void __print_errors_and_clean_openssl(int line, SSL_CTX* ctx, BIO* connection_bio, BIO* certificate_bio, X509* x509) {
    // printf("!!! Cleaning up on line %d\n", line);
    ERR_print_errors_fp(stdout);
    if (BIO_should_read(connection_bio)) printf("!!! BIO_should_read() is true!\n");
    if (BIO_should_write(connection_bio)) printf("!!! BIO_should_write() is true!\n");
    if (BIO_should_io_special(connection_bio)) printf("!!! BIO_should_io_special() is true!\n");
    if (ctx != NULL) SSL_CTX_free(ctx);
    if (connection_bio != NULL) BIO_free_all(connection_bio);
    if (certificate_bio != NULL) BIO_free_all(certificate_bio);
    if (x509 != NULL) X509_free(x509);
  }

  int __libc_single_threaded = 0;
  void *__dso_handle = nullptr;

  int __cxa_thread_atexit_impl(void (*func)(void*), void* obj, void* dso_symbol) {
    // printf("!!! %s called\n", __func__);
    return 0;
  }

  void * __memset_chk(void * dest, int c, size_t len, size_t destlen) {
    // printf("!!! %s called\n", __func__);
    return memset(dest, c, len);
  }

  void * __memcpy_chk(void * dest, const void * src, size_t len, size_t destlen) {
    // printf("!!! %s called\n", __func__);
    return memcpy(dest, src, len);
  }

  int __sprintf_chk(char * str, int flag, size_t strlen, const char * format, ...) {
    // printf("!!! %s called\n", __func__);

    // Courtesy https://opensource.apple.com/source/Libc/Libc-498/secure/sprintf_chk.c.auto.html
    va_list arg;
    int done;
    va_start (arg, format);
    if (strlen > (size_t) INT_MAX)
      done = vsprintf (str, format, arg);
    else {
      done = vsnprintf (str, strlen, format, arg);
      // if (done >= 0 && (size_t) done >= strlen) __chk_fail ();
    }
    va_end (arg);
    return done;
  }

  char * __strcat_chk(char * dest, const char * src, size_t destlen) {
    // printf("!!! %s called\n", __func__);
    return strcat(dest, src);
  }

  long int __fdelt_chk (long int d)
  {
    // printf("!!! %s called\n", __func__);
    // not sure what this function is supposed to be? can't find any good info on it
    return 0; 
  }

  int
  getcontext(ucontext_t *ucp)
  {
    // printf("!!! %s called\n", __func__);
    // TODO: This may or may not need to be filled in?
  	return 0;
  }

  int
  setcontext(const ucontext_t *ucp)
  {
    // printf("!!! %s called\n", __func__);
    // TODO: This may or may not need to be filled in?
    return 0;
  }

  int
  swapcontext(ucontext_t *oucp, const ucontext_t *ucp)
  {
    // printf("!!! %s called\n", __func__);
    // TODO: This may or may not need to be filled in?
  	return 0;
  }

  void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...) {
    // printf("!!! %s called\n", __func__);
    // TODO: This may or may not need to be filled in?
  }
}
