//
// SSLStateMachine.cpp
// UIE MultiAccess
//
// Created by Rakuto Furutani on 7/3/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#include <cassert>
#if 1
#include <JNIUtil.h>
#else

#include <cstring>

#define print_log(...) do { \
    char sbuf[2048]; \
    snprintf(sbuf, 2048, __VA_ARGS__); \
    printf("%s\n", sbuf); \
    fflush(stdout); \
} while(0)

#define LOGE print_log
#define LOGW print_log
#define LOGI print_log
#define LOGD print_log
#define LOGV print_log
#endif

#include <mutex>
#include "SSLStateMachine.h"

static void ssl_shutdown() {
    ERR_remove_state(0);
    ERR_free_strings();
    EVP_cleanup();
    sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
    CRYPTO_cleanup_all_ex_data();
}

SSLStateMachine::SSLStateMachine()
: ssl_(nullptr),
  ctx_(nullptr),
  ctx_created_(false),
  rbio_(nullptr),
  wbio_(nullptr),
  valid_(false) {

    static std::once_flag once;
    std::call_once(once, []() {
        // Initialize OpenSSL
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        ERR_load_crypto_strings();
        ERR_load_BIO_strings();
    });
}

SSLStateMachine::SSLStateMachine(SSL_CTX *ctx)
: SSLStateMachine() {
    InitWithSSLContext(ctx, true);
}

SSLStateMachine::SSLStateMachine(const void *pkcs12, size_t length, const char *password, bool is_server)
: SSLStateMachine() {
    auto ctx = SSL_CTX_new((is_server)? TLSv1_server_method() : TLSv1_client_method());
    if (ctx) {
        ctx_created_ = true;
        int ret = SetPKCS12Certificate(ctx, pkcs12, length, password);
        if (ret == 0) {
            InitWithSSLContext(ctx, is_server);
        }
    }
}

SSLStateMachine::SSLStateMachine(const char *cert, const char *pkey, bool is_server)
: SSLStateMachine() {
    assert(cert);
    assert(pkey);

    auto ctx = SSL_CTX_new((is_server)? TLSv1_server_method() : TLSv1_client_method());
    if (ctx) {
        if (!SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM)) {
            LOGE("Cannot load certificate: %s", cert);
        }
        if (!SSL_CTX_use_PrivateKey_file(ctx, pkey, SSL_FILETYPE_PEM)) {
            LOGE("Cannot load private key: %s", pkey);
        }
        InitWithSSLContext(ctx, is_server);
    }
}

void SSLStateMachine::InitWithSSLContext(SSL_CTX *ctx, bool is_server) {
    assert(ctx_ == nullptr);
    assert(ctx);

    ctx_  = ctx;
    ssl_  = SSL_new(ctx);
    rbio_ = BIO_new(BIO_s_mem());
    wbio_ = BIO_new(BIO_s_mem());

    if (ctx_ && ssl_ && rbio_ && wbio_) {
        // See https://www.openssl.org/docs/crypto/BIO_s_mem.html
        BIO_set_mem_eof_return(rbio_, -1);
        BIO_set_mem_eof_return(wbio_, -1);
        SSL_set_bio(ssl_, rbio_, wbio_);
        if (is_server) {
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
            SSL_set_accept_state(ssl_);
        } else {
            SSL_set_connect_state(ssl_);
        }

        // VIPER-2651: Disable DES and Triple-DES ciphers.
        // (CVE-2016-2183) The DES and Triple DES ciphers, as used in the TLS, SSH, and IPSec protocols and other protocols and products,
        // have a birthday bound of approximately four billion blocks, which makes it easier for remote attackers to obtain cleartext data
        // via a birthday attack against a long-duration encrypted session, as demonstrated by an HTTPS session using Triple DES in CBC mode,
        // aka a "Sweet32" attack.
        SSL_CTX_set_cipher_list(ctx_, "DEFAULT:!DES:!3DES");

#if 0
        SSL_set_debug(ssl, 1);
        // Debug
        SSL_set_info_callback(ssl, [](const SSL *ssl, int type, int val) {
            LOGI("== SSL info callback ==");
            LOGI(" State: %s", SSL_state_string_long(ssl));
            LOGI(" Type: %d, Val: %d", type, val);
            LOGI("=======================");
        });
        SSL_set_msg_callback(ssl, [](int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg) {
        });
#endif
        valid_ = true;
    } else {
        LOGE("OOM on SSLStateMachine()");
    }
}

/*
 *SSLStateMachine::SSLStateMachine(const char *pkcs12, const char *password, bool is_server) {
 *}
 */

SSLStateMachine::~SSLStateMachine() {
    if (ssl_) SSL_free(ssl_);
    if (ctx_) {
        if (ctx_created_) {
            SSL_CTX_free(ctx_);
        }
    }

    // SSL_free decrements the reference count of these BIOs.
    // Not need to call BIO_free here.
    /*
    if (rbio_) BIO_free(rbio_);
    if (wbio_) BIO_free(wbio_);
    */
}

int SSLStateMachine::WriteInject(const void *buffer, size_t length) {
    assert(buffer);
    if (valid_) {
        return SSL_write(ssl_, buffer, length);
    }
    return -1;
}

int SSLStateMachine::WriteExtract(void *buffer, size_t length) {
    assert(buffer);
    if (valid_) {
        return BIO_read(wbio_, buffer, length);
    }
    return -1;
}

int SSLStateMachine::ReadInject(const void *buffer, size_t length) {
    assert(buffer);
    if (valid_) {
        int ret = BIO_write(rbio_, buffer, length);
        assert(ret == length);
        return ret;
    }
    return -1;
}

int SSLStateMachine::ReadExtract(void *buffer, size_t buflen) {
    if (valid_) {
        int ret, err;
        // Check if SSL handshaking completed
        if (!SSL_is_init_finished(ssl_)) {
            /*
             * When we use SSL_get_error() to check the error produced by OpenSSL API, we must clear
             * current thread's error queue before calling the API. Please refer to the man page of
             * SSL_get_error():
             *
             * "... In addition to ssl and ret, SSL_get_error() inspects the current thread's
             * OpenSSL error queue. Thus, SSL_get_error() must be used in the same thread that
             * performed the TLS/SSL I/O operation, and no other OpenSSL function calls should
             * appear in between. The current thread's error queue must be empty before the TLS/SSL
             * I/O operation is attempted, or SSL_get_error() will not work reliably."
             */
            ERR_clear_error();
            ret = SSL_do_handshake(ssl_);
            if (ret < 0) {
                err = SSL_get_error(ssl_, ret);
                if (err == SSL_ERROR_WANT_READ) {
                    return 0;
                }
                logSSLError("SSL_do_handshake");
                return -1;
            } else if (ret == 0) {
                // handshake was not successful but shut down cleanly.
                logSSLError("SSL_do_handshake");
                return -1;
            }
            return 0;
        }

        ERR_clear_error();
        ret  = SSL_read(ssl_, buffer, buflen);
        if (ret < 0) {
            err = SSL_get_error(ssl_, ret);
            if (err == SSL_ERROR_WANT_READ) {
                LOGV("SSL_read requires more data to read");
                return 0;
            }
            logSSLError("SSL_read failed");
            return err;
        }
        return ret;
    }
    return -1;
}

int SSLStateMachine::SetPKCS12Certificate(SSL_CTX *ctx, const void *pkcs12, size_t length, const char *password) {
    assert(ctx);

    LOGV("SetPKCS12Certificate()");

    auto h_bio = BIOHandle(BIO_new_mem_buf((char *)(pkcs12), length), [](BIO *bio){ if (bio) BIO_free(bio);});
    auto bio = h_bio.get();
    if (!bio) {
        return ENOMEM;
    }

    // Load pkcs store
    auto p12 = d2i_PKCS12_bio(bio, nullptr);
    if (!p12) {
        logSSLError("d2i_PKCS12_bio");
        return ERR_LIB_PKCS12;
    }

    X509 *cert;
    EVP_PKEY *pkey;
    STACK_OF(X509) *ca = nullptr;

    int err;
    if (!(err = PKCS12_parse(p12, password, &pkey, &cert, &ca))) {
        logSSLError("PKCS12_parse failed");
        return err;
    }

    auto h_cert = X509Handle(cert, [](X509 *x509) { if (x509) X509_free(x509); });
    auto h_pkey = PKeyHandle(pkey, [](EVP_PKEY *pkey) { if (pkey) EVP_PKEY_free(pkey); });
    err = SSL_CTX_use_certificate(ctx, cert);
    if (!err) {
        logSSLError("SSL_use_certificate");
        return err;
    }
    err = SSL_CTX_use_PrivateKey(ctx, pkey);
    if (!err) {
        logSSLError("SSL_use_PrivateKey");
        return err;
    }

    LOGV("SetPKCS12Certificate OK");

    return 0;
}

void SSLStateMachine::logSSLError(const char *prefix) {
    unsigned long l;
    while ((l = ERR_get_error())) {
        char buf[1024];
        ERR_error_string_n(l, buf, sizeof(buf));
        LOGE("%s: %s", prefix, buf);
    }
}
