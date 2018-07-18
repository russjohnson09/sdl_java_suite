//
// SSLStateMachine.h
// UIE MultiAccess
//
// Created by Rakuto Furutani on 7/3/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#pragma once

#include <memory>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pkcs12.h>

/**
 * This class manages SSL I/O.
 */
class SSLStateMachine {
 public:
    /**
     * Create SSL state machine with PKCS12 data that contains certificate and private key.
     *
     * @param pkcs12
     * @param length
     * @param password
     */
    SSLStateMachine(const void *pkcs12, size_t length, const char *password, bool is_server);

    /**
     * Instantiate SSL state machine
     *
     * @param PEM certfile file
     * @param PEM private key file
     */
    SSLStateMachine(const char *cert, const char *pkey, bool is_server);

    /**
     * Instantiate SSL state machine with existing SSL context.
     * This method is used to create one for the connection accepted on the SSL server.
     *
     * @param ctx
     */
    SSLStateMachine(SSL_CTX *ctx);

    virtual ~SSLStateMachine();

    /**
     * Return if SSL handshake is completed
     *
     * @return
     */
    inline bool IsHandshakeComplete() { return (ssl_)? SSL_is_init_finished(ssl_) : false; }

    /**
     * Reads decrypted data from the stream.
     */
    int ReadInject(const void *buffer, size_t buflen);

    /**
     * Read decrypted data up to specified bytes into the buffer.
     *
     * @param buffer    Buffer to fill the decrypted data.
     * @param buflen    Maximum length of bytes to read
     */
    int ReadExtract(void *buffer, size_t buflen);

    /**
     * Writes encrypted data to the stream.
     *
     * @param buffer
     * @param length
     * @return
     */
    int WriteInject(const void *buffer, size_t length);

    /**
     * Extract encrypted data to sent to the transport.
     *
     * @param buffer
     * @param length
     * @retrun
     */
    int WriteExtract(void *buffer, size_t length);

    /**
     * Return true if any pending outgoing data exists in the buffer.
     *
     * @return true
     */
    inline bool IsWritePending() {
        return wbio_ ? BIO_ctrl_pending(wbio_) : false;
    }

    /**
     * Return OpenSSL context
     */
    inline SSL_CTX* context() { return ctx_; }

    /**
     * Returns true if state machine is properly constructed.
     */
    inline bool IsValid() { return valid_; }

 private:
#define DELETER(type) void(*)(type*)
    using SSLHandle     = std::unique_ptr<SSL, DELETER(SSL)>;
    using SSLContext    = std::shared_ptr<SSL_CTX>;
    using BIOHandle     = std::unique_ptr<BIO, DELETER(BIO)>;
    using X509Handle    = std::unique_ptr<X509, DELETER(X509)>;
    using SSLStateMachineHandle = std::unique_ptr<SSLStateMachine>;
    using PKeyHandle = std::unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)>;
#undef DELETER

    SSL* ssl_;
    SSL_CTX *ctx_;
    bool ctx_created_;
    BIO *rbio_;
    BIO* wbio_;
    bool valid_;

    SSLStateMachine();

    int SetPKCS12Certificate(SSL_CTX*, const void *pkcs12, size_t length, const char *password);
    void InitWithSSLContext(SSL_CTX*, bool);
    void logSSLError(const char*);
};
