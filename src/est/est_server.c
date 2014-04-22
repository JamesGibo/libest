/** @file */
/*------------------------------------------------------------------
 * est/est_server.c - EST Server specific code
 *
 *	       Assumptions:  - Web server using this module utilizes
 *	                       OpenSSL for HTTPS services.
 *	                     - OpenSSL is linked along with this
 *	                       modulue.
 *
 * April, 2013
 *
 * Copyright (c) 2013-2014 by cisco Systems, Inc.
 * All rights reserved.
 **------------------------------------------------------------------
 */
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "est.h"
#include "est_server_http.h"
#include "est_locl.h"
#include "est_ossl_util.h"
#include <openssl/x509v3.h>

static ASN1_OBJECT *o_cmcRA = NULL;

/*
 * This function sends an HTTP error response.
 */
void est_send_http_error (EST_CTX *ctx, void *http_ctx, int fail_code)
{
    switch (fail_code) {
    case EST_ERR_BAD_PKCS10:
        if (!mg_write(http_ctx, EST_HDR_BAD_PKCS10, strnlen(EST_HDR_BAD_PKCS10, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        if (!mg_write(http_ctx, EST_BODY_BAD_PKCS10, strnlen(EST_BODY_BAD_PKCS10, EST_BODY_MAX_LEN))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    case EST_ERR_AUTH_FAIL:
        if (!mg_write(http_ctx, EST_HDR_UNAUTHORIZED, strnlen(EST_HDR_UNAUTHORIZED, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        if (!mg_write(http_ctx, EST_BODY_UNAUTHORIZED, strnlen(EST_BODY_UNAUTHORIZED, EST_BODY_MAX_LEN))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    case EST_ERR_WRONG_METHOD:
        if (!mg_write(http_ctx, EST_HDR_BAD_METH, strnlen(EST_HDR_BAD_METH, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        if (!mg_write(http_ctx, EST_BODY_BAD_METH, strnlen(EST_BODY_BAD_METH, EST_BODY_MAX_LEN))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    case EST_ERR_NO_SSL_CTX:
        if (!mg_write(http_ctx, EST_HDR_BAD_SSL, strnlen(EST_HDR_BAD_SSL, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        if (!mg_write(http_ctx, EST_BODY_BAD_SSL, strnlen(EST_BODY_BAD_SSL, EST_BODY_MAX_LEN))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    case EST_ERR_HTTP_NOT_FOUND:
        if (!mg_write(http_ctx, EST_HDR_NOT_FOUND, strnlen(EST_HDR_NOT_FOUND, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        if (!mg_write(http_ctx, EST_BODY_NOT_FOUND, strnlen(EST_BODY_NOT_FOUND, EST_BODY_MAX_LEN))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    case EST_ERR_HTTP_NO_CONTENT:
        if (!mg_write(http_ctx, EST_HTTP_HDR_204, strnlen(EST_HTTP_HDR_204, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    default:
        if (!mg_write(http_ctx, EST_HDR_UNKNOWN_ERR, strnlen(EST_HDR_UNKNOWN_ERR, EST_HTTP_HDR_MAX))) {
	    EST_LOG_ERR("mg_write failed");
        }
        if (!mg_write(http_ctx, EST_BODY_UNKNOWN_ERR, strnlen(EST_BODY_UNKNOWN_ERR, EST_BODY_MAX_LEN))) {
	    EST_LOG_ERR("mg_write failed");
        }
        break;
    }
}

/*
 * This function handles an incoming cacerts request from
 * the client.
 */
int est_handle_cacerts (EST_CTX *ctx, void *http_ctx)
{
    char http_hdr[EST_HTTP_HDR_MAX];
    int hdrlen;

    if (ctx->ca_certs  == NULL) {
        return (EST_ERR_HTTP_NOT_FOUND);
    }
        
    /*
     * Send HTTP header
     */
    snprintf(http_hdr, EST_HTTP_HDR_MAX, "%s%s%s%s", EST_HTTP_HDR_200, EST_HTTP_HDR_EOL,
             EST_HTTP_HDR_STAT_200, EST_HTTP_HDR_EOL);
    hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
    snprintf(http_hdr + hdrlen, EST_HTTP_HDR_MAX, "%s: %s%s", EST_HTTP_HDR_CT,
             EST_HTTP_CT_PKCS7, EST_HTTP_HDR_EOL);
    hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
    snprintf(http_hdr + hdrlen, EST_HTTP_HDR_MAX, "%s: %s%s", EST_HTTP_HDR_CE,
             EST_HTTP_CE_BASE64, EST_HTTP_HDR_EOL);
    hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
    snprintf(http_hdr + hdrlen, EST_HTTP_HDR_MAX, "%s: %d%s%s", EST_HTTP_HDR_CL,
             ctx->ca_certs_len, EST_HTTP_HDR_EOL, EST_HTTP_HDR_EOL);
    if (!mg_write(http_ctx, http_hdr, strnlen(http_hdr, EST_HTTP_HDR_MAX))) {
        return (EST_ERR_HTTP_WRITE);
    }

    /*
     * Send the CA certs in the body
     */
    if (!mg_write(http_ctx, ctx->ca_certs, ctx->ca_certs_len)) {
        return (EST_ERR_HTTP_WRITE);
    }
    
    return (EST_ERR_NONE);
}

/*! @brief est_server_generate_auth_digest() is used by an application 
    to calculate the HTTP Digest value based on the header values
    provided by an EST client.  
 
    @param ah Authentication header values from client, provided by libest
    @param HA1 The precalculated HA1 value for the user.  HA1 is defined in
           RFC 2617.  It's the MD5 calculation of the user's ID, HTTP realm,
	   and the user's password.

    This is a helper function that an application can use to calculate
    the HTTP Digest value when performing HTTP Digest Authentication
    of an EST client.  libest does not maintain a user database. 
    This is left up to the application, with the intent of integrating  
    an external user database (e.g. Radius/AAA).
    
    The HA1 value should be calculated by the application as
    defined in RFC 2617.  HA1 is the MD5 hash of the user ID, HTTP realm,
    and user password.  This MD5 value is then converted to a hex string.
    HA1 is expected to be 32 bytes long.
 
    @return char* containing the digest, or NULL if an error occurred.
 */
char *est_server_generate_auth_digest (EST_HTTP_AUTH_HDR *ah, char *HA1)
{
    EVP_MD_CTX *mdctx;
    const EVP_MD *md = EVP_md5();
    uint8_t ha2[EVP_MAX_MD_SIZE];
    unsigned int ha2_len;
    char ha2_str[33];
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int d_len;
    char *rv;

    if (!ah) {
	EST_LOG_ERR("Null auth header");
        return (NULL);
    }

    if (!HA1) {
	EST_LOG_ERR("Null HA1");
        return (NULL);
    }

    /*
     * Calculate HA2 using method, URI,
     */
    mdctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, "POST", 4); 
    EVP_DigestUpdate(mdctx, ":", 1);
    EVP_DigestUpdate(mdctx, ah->uri, strnlen(ah->uri, MAX_REALM));
    EVP_DigestFinal(mdctx, ha2, &ha2_len);
    EVP_MD_CTX_destroy(mdctx);
    est_hex_to_str(ha2_str, ha2, ha2_len);

    /*
     * Calculate auth digest using HA1, nonce, nonce count, client nonce, qop, HA2
     */
    mdctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, HA1, 32); 
    EVP_DigestUpdate(mdctx, ":", 1);
    EVP_DigestUpdate(mdctx, ah->nonce, strnlen(ah->nonce, MAX_NONCE));
    EVP_DigestUpdate(mdctx, ":", 1);
    EVP_DigestUpdate(mdctx, ah->nc, strnlen(ah->nc, MAX_NC));
    EVP_DigestUpdate(mdctx, ":", 1);
    EVP_DigestUpdate(mdctx, ah->cnonce, strnlen(ah->cnonce, MAX_NONCE));
    EVP_DigestUpdate(mdctx, ":", 1);
    EVP_DigestUpdate(mdctx, "auth", 4);
    EVP_DigestUpdate(mdctx, ":", 1);
    EVP_DigestUpdate(mdctx, ha2_str, ha2_len * 2);
    EVP_DigestFinal(mdctx, digest, &d_len);
    EVP_MD_CTX_destroy(mdctx);

    rv = malloc(33);
    est_hex_to_str(rv, digest, d_len);
    return (rv);
}

/*
 * This function allocates an HTTP authentication header
 * structure, which is used to pass the auth credentials
 * to the application layer to allow the app to authenticate
 * an EST client.
 */
static EST_HTTP_AUTH_HDR * est_create_ah()
{
    EST_HTTP_AUTH_HDR *ah;

    ah = malloc(sizeof(EST_HTTP_AUTH_HDR));
    memset(ah, 0, sizeof(EST_HTTP_AUTH_HDR));
    return (ah);
}

/*
 * This function frees all the elements on an HTTP
 * authentication header structure.
 */
static void est_destroy_ah(EST_HTTP_AUTH_HDR *ah)
{
    if (!ah) return;
    if (ah->user) free(ah->user);
    if (ah->pwd) free(ah->pwd);
    if (ah->uri) free(ah->uri);
    if (ah->cnonce) free(ah->cnonce);
    if (ah->qop) free(ah->qop);
    if (ah->nc) free(ah->nc);
    if (ah->nonce) free(ah->nonce);
    if (ah->response) free(ah->response);
    free(ah);
}

/*
 * This function verifies that the peer either provided a certificate
 * that was verifed by the TLS stack, or HTTP authentication
 * credentials were provided. 
 *
 * Returns a EST_AUTH_STATE authorization result 
 */
EST_AUTH_STATE est_enroll_auth (EST_CTX *ctx, void *http_ctx, SSL *ssl,
	                        int reenroll)
{
    EST_AUTH_STATE rv = EST_UNAUTHORIZED;
    X509 *peer = NULL;
    struct mg_connection *conn = (struct mg_connection*)http_ctx;
    EST_HTTP_AUTH_HDR *ah;
    EST_HTTP_AUTH_HDR_RESULT pr;
    int v_result;

    /*
     * Get client certificate from TLS stack.  
     */
    if ((peer = SSL_get_peer_certificate(ssl)) != NULL) {
        // check TLS based client authorization (is client cert authorized)
        v_result = SSL_get_verify_result(ssl);
        if (X509_V_OK == v_result) {
            EST_LOG_INFO("TLS: client certificate is valid");
	    rv = EST_CERT_AUTH;
	}
	else if (X509_V_ERR_UNABLE_TO_GET_CRL == v_result) {
            EST_LOG_WARN("Peer cert is valid, but no CRL was loaded. Unable to determine if peer cert is revoked.");
	    rv = EST_CERT_AUTH;
        } else {
            EST_LOG_INFO("TLS: client certificate not verified (v_result=%d)",
		         v_result);
	    /* We need to bail since the client is using a bogus cert,
	     * no need to contiue with HTTP authentication below */
	    X509_free(peer);
	    return(EST_UNAUTHORIZED);
        }
    } else {
        EST_LOG_INFO("TLS: no peer certificate");
	rv = EST_UNAUTHORIZED;
    }

    /*
     * If the application layer has enabled HTTP authentication we
     * will attempt HTTP authentication when TLS client auth fails
     * or when the require_http_auth flag is set by the application.
     * All this assumes the application layer has provided the HTTP auth
     * callback facility.
     */
    if (ctx->est_http_auth_cb && 
	(rv != EST_CERT_AUTH || HTTP_AUTH_REQUIRED == ctx->require_http_auth)) {
        /*
         * Try HTTP authentication.
         */
	ah = est_create_ah();
        pr = mg_parse_auth_header(conn, ah);
	switch (pr) {
        case EST_AUTH_HDR_GOOD:
	    /*
	     * Invoke the application specific auth check now 
	     * that we have the user's credentials
	     */
	    if (ctx->est_http_auth_cb(ctx, ah, peer, ctx->ex_data)) {
		rv = EST_HTTP_AUTH;
	    } else {
                EST_LOG_WARN("HTTP authentication failed. Auth type=%d", 
                             ah->mode);
		rv = EST_UNAUTHORIZED;
	    }
	    break;
        case EST_AUTH_HDR_MISSING:
            // ask client to send us authorization headers
            mg_send_authorization_request(conn);
	    EST_LOG_INFO("HTTP auth headers missing, sending HTTP auth request to client.");
            rv = EST_HTTP_AUTH_PENDING;
	    break;
        case EST_AUTH_HDR_BAD:
	default:
            EST_LOG_WARN("Client sent incomplete HTTP authorization header"); 
	    if (reenroll && rv == EST_CERT_AUTH) {
		EST_LOG_INFO("Client cert was authenticated, HTTP auth not required for reenroll"); 
	    } else {
		rv = EST_UNAUTHORIZED;
	    }
	    break;
	}
	est_destroy_ah(ah);
    } 
    if (peer) {
	X509_free(peer);
    }
    return (rv);

}

/*
 * This function is used to determine if the EST client, which could be
 * an RA, is using a certificate that contains the id-kp-cmcRA usage
 * extension.  When this usage bit is present, the PoP check is disabled
 * to allow the RA use case. 
 *
 * This logic was taken from x509v3_cache_extensions() in v3_purp.c (OpenSSL).
 *
 * Returns 1 if the cert contains id-kp-cmcRA extended key usage extension.
 * Otherwise it returns 0.
 */
static int est_check_cmcRA (X509 *cert) 
{
    int cmcRA_found = 0;
    EXTENDED_KEY_USAGE *extusage;
    int i;
    ASN1_OBJECT *obj;

    /*
     * Get the extended key usage extension.  If found
     * loop through the values and look for the ik-kp-cmcRA
     * value in this extension.
     */
    if((extusage = X509_get_ext_d2i(cert, NID_ext_key_usage, NULL, NULL))) {
	/*
	 * Iterate through the extended key usage values
	 */
        for(i = 0; i < sk_ASN1_OBJECT_num(extusage); i++) {
	    obj =  sk_ASN1_OBJECT_value(extusage,i);
	    /*
	     * Compare the current iteration with the global
	     * id-kp-cmcRA value that was created earlier
	     */
            if (!OBJ_cmp(obj, o_cmcRA)) {
                cmcRA_found = 1; 
                break;
            }
        }
        sk_ASN1_OBJECT_pop_free(extusage, ASN1_OBJECT_free);
    }

    return (cmcRA_found);
}

/*
 * This is a utility function to convert the base64 DER encoded
 * CSR to an OpenSSL X509_REQ pointer.  Returns NULL if there
 * was a problem.
 */
X509_REQ * est_server_parse_csr (unsigned char *pkcs10, int pkcs10_len)
{
    BIO *in, *b64;
    X509_REQ *req;

    /*
     * Get the original pkcs10 request from the client
     */
    b64 = BIO_new(BIO_f_base64());
    if (b64 == NULL) {
	EST_LOG_ERR("Unable to open PKCS10 b64 buffer");
	return (NULL);
    }
    in = BIO_new_mem_buf(pkcs10, pkcs10_len);
    if (in == NULL) {
	EST_LOG_ERR("Unable to open PKCS10 raw buffer");
	BIO_free(b64);
	return (NULL);
    }
    in = BIO_push(b64, in);

    /*
     * Read the PEM encoded pkcs10 cert request
     */
    if ((req = d2i_X509_REQ_bio(in, NULL)) == NULL) {
        EST_LOG_ERR("Problem reading DER encoded certificate request");
	ossl_dump_ssl_errors();
        BIO_free_all(in);
	return (NULL);
    }
    BIO_free_all(in);

    return req;
}

/*
 * This function implements the Proof of Posession check (PoP).  The TLS UID has
 * already been saved from the TLS session earlier.  This TLS UID should match the
 * value of the challengePassword attribute in the pkcs10 client certificate.  The
 * client will have provided this value when signing the pkcs10 cert request
 * with its private key, which proves the client is in possession of the private key.
 * This check is enforced as follows:
 *     1. If CSR contains the PoP, it must be valid.
 *     2. If CSR didn't contain the PoP and the server is configured
 *        to require the PoP, then the authentication fails.
 *     3. Otherwise, if CSR didn't contain the PoP and the server is not
 *        configured to require PoP, then authentication passes.
 *
 * Parameters:
 *	ctx:	    Pointer to EST context
 *	ssl:        Pointer to SSL context
 *	pkcs10:	    Pointer to raw PKCS10 data
 *	pkcs10_len: Length of raw PKCS10 data
 *
 * Return value:
 *	EST_ERR_NONE when PoP check passes
 */
int est_tls_uid_auth (EST_CTX *ctx, SSL *ssl, X509_REQ *req) 
{
    X509_ATTRIBUTE *attr;
    int i, j;

    ASN1_TYPE *at;
    ASN1_BIT_STRING *bs = NULL;
    ASN1_TYPE *t;
    int rv = EST_ERR_NONE;
    char *tls_uid;

    /*
     * Get the index of the challengePassword attribute in the request
     */
    i = X509_REQ_get_attr_by_NID(req, NID_pkcs9_challengePassword, -1);
    if (i < 0) {
        EST_LOG_INFO("Cert request does not contain PoP challengePassword attribute");
	/*
	 * If PoP is enabled, we must fail at this point
	 * since the clinet didn't send the channel binding
	 * info in the CSR.
	 */
	if (ctx->server_enable_pop) {
	    EST_LOG_WARN("PoP enabled, CSR was not authenticated");
            return (EST_ERR_AUTH_FAIL_TLSUID);
	} else {
	    return (EST_ERR_NONE);
	}
    } else {
        /*
         * Get a reference to the attribute now that we know where it's located
	 * RFC 7030 requires that we check the PoP when it's present
         */
        attr = X509_REQ_get_attr(req, i);

        /*
         * If we found the attribute, get the actual value of the challengePassword
         */
        if (attr) {
            if (attr->single) {
                t = attr->value.single;
                bs = t->value.bit_string;
            } else {
                j = 0;
                at = sk_ASN1_TYPE_value(attr->value.set, j);
                bs = at->value.asn1_string;
            }
        } else {
            EST_LOG_WARN("PoP challengePassword attribute not found in client cert request");
            return (EST_ERR_AUTH_FAIL_TLSUID);
        }

        /*
         * Now that we have the challengePassword from the client cert request,
         * compare it to the TLS UID we calculated on the server side.
         * This implements the PoP check to verify the client holds the private
         * key used to sign the cert request.
         */
        tls_uid = est_get_tls_uid(ssl, 0);
        if (tls_uid) {
	    if (!memcmp(tls_uid, bs->data, EST_TLS_UID_LEN)) {
                EST_LOG_INFO("PoP is valid");
                rv = EST_ERR_NONE;
            } else {
                EST_LOG_WARN("PoP is not valid");
                rv = EST_ERR_AUTH_FAIL_TLSUID;
            }
            free(tls_uid);
        } else {
            EST_LOG_WARN("Local TLS channel binding info is not available");
            rv = EST_ERR_AUTH_FAIL_TLSUID;
        }
    }

    return rv;
}

/*
 * This function performs a simple sanity check on a PKCS10
 * CSR.  It will check the signature in the CSR.
 * Returns 0 for success, non-zero if the sanity check failed.
 */
int est_server_check_csr (X509_REQ *req) 
{
    EVP_PKEY *pub_key = NULL;
    int rc;

    /*
     * Extract the public key from the CSR
     */
    if ((pub_key = X509_REQ_get_pubkey(req)) == NULL) {
	EST_LOG_ERR("Unable to extract public key from CSR");
	return 1;
    }

    /*
     * Verify the signature in the CSR 
     */
    rc = X509_REQ_verify(req, pub_key);
    EVP_PKEY_free(pub_key);

    /*
     * Check the result
     */
    if (rc < 0) {
	EST_LOG_ERR("CSR signature check failed");
        return 1;
    } else if (rc == 0) {
	EST_LOG_ERR("CSR signature mismatch");
        return 1;
    } else {
        return 0;
    }
}

/*
 * This function is used by the server to process and incoming
 * Simple Enroll request from the client.
 */
static EST_ERROR est_handle_simple_enroll (EST_CTX *ctx, void *http_ctx, SSL *ssl,
                                           const char *ct, char *body, int body_len,
				     int reenroll)
{
    int rv, cert_len;
    struct mg_connection *conn = (struct mg_connection*)http_ctx;
    unsigned char *cert;
    char http_hdr[EST_HTTP_HDR_MAX];
    int hdrlen;
    X509 *peer_cert;
    X509_REQ *csr = NULL;
    int client_is_ra = 0;

    if (!reenroll && !ctx->est_enroll_pkcs10_cb) {
	EST_LOG_ERR("Null enrollment callback");
        return (EST_ERR_NULL_CALLBACK);
    }

    if (reenroll && !ctx->est_reenroll_pkcs10_cb) {
	EST_LOG_ERR("Null reenroll callback");
        return (EST_ERR_NULL_CALLBACK);
    }

    /*
     * Make sure the client has sent us a PKCS10 CSR request
     */
    if (strncmp(ct, "application/pkcs10", 18)) {
        return (EST_ERR_BAD_CONTENT_TYPE);
    }


    /*
     * Authenticate the client
     */
    switch (est_enroll_auth(ctx, http_ctx, ssl, reenroll)) {
    case EST_HTTP_AUTH:
    case EST_CERT_AUTH:
	/*
	 * this means the user was authorized, either through
	 * HTTP authoriztion or certificate authorization
	 */
        break;
    case EST_HTTP_AUTH_PENDING:
        return (EST_ERR_AUTH_PENDING);
        break;
    case EST_UNAUTHORIZED:
    default:
        return (EST_ERR_AUTH_FAIL);
        break;
    }

    /*
     * Parse the PKCS10 CSR from the client
     */
    csr = est_server_parse_csr((unsigned char*)body, body_len);
    if (!csr) {
	EST_LOG_ERR("Unable to parse the PKCS10 CSR sent by the client");
	return (EST_ERR_BAD_PKCS10);
    }
    
    /*
     * Perform a sanity check on the CSR
     */
    if (est_server_check_csr(csr)) {
	EST_LOG_ERR("PKCS10 CSR sent by the client failed sanity check");
	X509_REQ_free(csr);
	return (EST_ERR_BAD_PKCS10);
    }

    /*
     * Get the peer certificate if available.  This
     * identifies the client. The CA may desire
     * this information.
     */
    peer_cert = SSL_get_peer_certificate(ssl);

    if (peer_cert) {
	client_is_ra = est_check_cmcRA (peer_cert);
    }
    EST_LOG_INFO("id-kp-cmcRA present: %d", client_is_ra);

    /*
     * Do the PoP check (Proof of Possession).  The challenge password
     * in the pkcs10 request should match the TLS uniqe ID.
     * The PoP check is not performend when the client is an RA.
     */
    if (!client_is_ra) {
	rv = est_tls_uid_auth(ctx, ssl, csr);
	if (rv != EST_ERR_NONE) {
	    X509_REQ_free(csr);
	    X509_free(peer_cert);
	    return (EST_ERR_AUTH_FAIL_TLSUID);
	} 
    }

    /* body now points to the pkcs10 data, pass
     * this to the enrollment routine */
    if (reenroll) {
        rv = ctx->est_reenroll_pkcs10_cb((unsigned char*)body, body_len, 
                                         &cert, (int*)&cert_len,
                                         conn->user_id, peer_cert, ctx->ex_data);
    } else {
        rv = ctx->est_enroll_pkcs10_cb((unsigned char*)body, body_len, 
                                       &cert, (int*)&cert_len,
                                       conn->user_id, peer_cert, ctx->ex_data);
    }

    /*
     * Peer cert is no longer needed, delete it if we have one
     */
    if (peer_cert) {
	X509_free(peer_cert);
    }

    if (rv == EST_ERR_NONE && cert_len > 0) {
        /*
         * Send HTTP header
         */
        snprintf(http_hdr, EST_HTTP_HDR_MAX, "%s%s%s%s", EST_HTTP_HDR_200, EST_HTTP_HDR_EOL,
                 EST_HTTP_HDR_STAT_200, EST_HTTP_HDR_EOL);
        hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
        snprintf(http_hdr + hdrlen, EST_HTTP_HDR_MAX, "%s: %s%s", EST_HTTP_HDR_CT,
                 EST_HTTP_CT_PKCS7, EST_HTTP_HDR_EOL);
        hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
        snprintf(http_hdr + hdrlen, EST_HTTP_HDR_MAX, "%s: %s%s", EST_HTTP_HDR_CE,
                 EST_HTTP_CE_BASE64, EST_HTTP_HDR_EOL);
        hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
        snprintf(http_hdr + hdrlen, EST_HTTP_HDR_MAX, "%s: %d%s%s", EST_HTTP_HDR_CL,
                 cert_len, EST_HTTP_HDR_EOL, EST_HTTP_HDR_EOL);
        if (!mg_write(http_ctx, http_hdr, strnlen(http_hdr, EST_HTTP_HDR_MAX))) {
            free(cert);
	    X509_REQ_free(csr);
            return (EST_ERR_HTTP_WRITE);
        }

        /*
         * Send the signed PKCS7 certificate in the body
         */
        if (!mg_write(http_ctx, cert, cert_len)) {
            free(cert);
	    X509_REQ_free(csr);
            return (EST_ERR_HTTP_WRITE);
        }
        free(cert);
    } else if (rv == EST_ERR_CA_ENROLL_RETRY) {
        /*
         * The CA did not sign the request and has asked the
         * client to retry in the future.  This may occur if
         * the CA is not configured for automatic enrollment.
         * Send the HTTP retry response to the client.
         */
        EST_LOG_INFO("CA server requests retry, likely not setup for auto-enroll");
        snprintf(http_hdr, EST_HTTP_HDR_MAX, "%s%s%s%s%s: %d%s%s", 
		EST_HTTP_HDR_202,
                EST_HTTP_HDR_EOL, 
		EST_HTTP_HDR_STAT_202, 
		EST_HTTP_HDR_EOL,
                EST_HTTP_HDR_RETRY_AFTER, 
		ctx->retry_period, 
		EST_HTTP_HDR_EOL, 
		EST_HTTP_HDR_EOL);
        hdrlen = strnlen(http_hdr, EST_HTTP_HDR_MAX);
        if (!mg_write(http_ctx, http_hdr, strnlen(http_hdr, EST_HTTP_HDR_MAX))) {
	    X509_REQ_free(csr);
            return (EST_ERR_HTTP_WRITE);
        }
    } else {
	X509_REQ_free(csr);
        return (EST_ERR_CA_ENROLL_FAIL);
    }

    X509_REQ_free(csr);
    return (EST_ERR_NONE);
}

/*
 * This function is used by the server to process and incoming
 * csr attributes request from the client.
 */
static int est_handle_csr_attrs (EST_CTX *ctx, void *http_ctx)
{
    int rv = EST_ERR_NONE;
    int pop_present;
    char *csr_data, *csr_data_pop;
    int csr_len, csr_pop_len;

    if (!ctx->server_csrattrs && !ctx->est_get_csr_cb) {
        if (!ctx->server_enable_pop) {
  	        EST_LOG_ERR("Null csr callback");
		/* Send a 204 response indicating the server doesn't have a CSR */
		est_send_http_error(ctx, http_ctx, EST_ERR_HTTP_NO_CONTENT);
		return (EST_ERR_NONE);
        } else {
	  csr_data = malloc(EST_CSRATTRS_POP_LEN + 1);
	    if (!csr_data) {
                return (EST_ERR_MALLOC);
	    }
	    strncpy(csr_data, EST_CSRATTRS_POP, EST_CSRATTRS_POP_LEN);
	    csr_data[EST_CSRATTRS_POP_LEN] = 0;
	    csr_len = EST_CSRATTRS_POP_LEN;
	    return (est_send_csrattr_data(ctx, csr_data, csr_len, http_ctx));
        }
    }

    /*
     * Invoke CA server callback to retrieve the CSR.  Callback takes priority
     * over saved values in the context.
     * Note: there is no need to authenticate the client (see sec 4.5)
     */
    if (ctx->est_get_csr_cb) {
	csr_data = (char *)ctx->est_get_csr_cb(&csr_len, ctx->ex_data);
	rv = est_asn1_parse_attributes(csr_data, csr_len, &pop_present);
	if (csr_len && (rv != EST_ERR_NONE)) {
            if (csr_data) {
                free(csr_data);
            }
	    est_send_http_error(ctx, http_ctx, EST_ERR_HTTP_NO_CONTENT);
	    return (EST_ERR_NONE);
	}

	ctx->csr_pop_present = 0;
	if (ctx->server_enable_pop) {
	    rv = est_is_challengePassword_present(csr_data, csr_len, &pop_present);
	    if (rv != EST_ERR_NONE) {
		EST_LOG_ERR("Error during PoP/sanity check");
		if (csr_data) {
		    free(csr_data);
		}
		est_send_http_error(ctx, http_ctx, EST_ERR_HTTP_NO_CONTENT);
		return (EST_ERR_NONE);
	    }
	    ctx->csr_pop_present = pop_present;

	    if (!ctx->csr_pop_present) {
		if (csr_len == 0) {
                    csr_data = malloc(EST_CSRATTRS_POP_LEN + 1);
		    if (!csr_data) {
			return (EST_ERR_MALLOC);
		    }
		    strncpy(csr_data, EST_CSRATTRS_POP, EST_CSRATTRS_POP_LEN);
		    csr_data[EST_CSRATTRS_POP_LEN] = 0;
		    csr_len = EST_CSRATTRS_POP_LEN;
		    return (est_send_csrattr_data(ctx, csr_data, csr_len, http_ctx));
		}
		rv = est_add_challengePassword(csr_data, csr_len, &csr_data_pop, &csr_pop_len);
		if (rv != EST_ERR_NONE) {
		    if (csr_data) {
		        free(csr_data);
		    }
		    EST_LOG_ERR("Error during add PoP");
		    est_send_http_error(ctx, http_ctx, EST_ERR_HTTP_NO_CONTENT);
		    return (EST_ERR_NONE);
		}
		free(csr_data);
		csr_data = csr_data_pop;
		csr_len = csr_pop_len;
	    }
	}
    } else {
        csr_data = malloc(ctx->server_csrattrs_len + 1);
	if (!csr_data) {
            return (EST_ERR_MALLOC);
        }
        strncpy(csr_data, (char *)ctx->server_csrattrs, ctx->server_csrattrs_len);
	csr_data[ctx->server_csrattrs_len] = 0;
	csr_len = ctx->server_csrattrs_len;
    }
    return (est_send_csrattr_data(ctx, csr_data, csr_len, http_ctx));
}

/*
 * This function should be called by the web server layer when
 * a HTTP request arrives on the listening port of the EST server.
 * It will determine the EST request type and dispatch the request
 * to the appropriate handler.
 *
 * Paramters:
 *      ctx:	    Pointer to EST_CTX
 *      http_ctx:   Context pointer from web server
 *      method:     The HTML method in the request, should be either "GET" or "POST"
 *	uri:	    pointer to HTTP URI
 *	body:	    pointer to full HTML body contents
 *	body_len:   length of HTML body
 *	ct:         HTML content type header
 */
int est_http_request (EST_CTX *ctx, void *http_ctx,
                      char *method, char *uri,
                      char *body, int body_len, const char *ct)
{
    SSL *ssl;
    int rc;

    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    /*
     * Verify the context is for a server, not a client
     */
    if (ctx->est_mode != EST_SERVER) {
        return (EST_ERR_BAD_MODE);
    }

    /*
     * See if this is a cacerts request
     */
    if (strncmp(uri, EST_CACERTS_URI, EST_URI_MAX_LEN) == 0) {
        /* Only GET is allowed */
        if (strncmp(method, "GET", 3)) {
            est_send_http_error(ctx, http_ctx, EST_ERR_WRONG_METHOD);
            return (EST_ERR_WRONG_METHOD);
        }

        rc = est_handle_cacerts(ctx, http_ctx);
        if (rc != EST_ERR_NONE) {
            est_send_http_error(ctx, http_ctx, rc);
            return (rc);
        }
    }

    /*
     * See if this is a simple enrollment request
     */
    if (strncmp(uri, EST_SIMPLE_ENROLL_URI, EST_URI_MAX_LEN) == 0) {
        /* Only POST is allowed */
        if (strncmp(method, "POST", 4)) {
            EST_LOG_WARN("Incoming HTTP request used wrong method\n");
            est_send_http_error(ctx, http_ctx, EST_ERR_WRONG_METHOD);
            return (EST_ERR_WRONG_METHOD);
        }
	if (!ct) {
            EST_LOG_WARN("Incoming HTTP header has no Content-Type header\n");
            est_send_http_error(ctx, http_ctx, EST_ERR_BAD_PKCS10);
	    return (EST_ERR_BAD_CONTENT_TYPE); 
	}
        /*
         * Get the SSL context, which is required for authenticating
         * the client.
         */
        ssl = (SSL*)mg_get_conn_ssl(http_ctx);
        if (!ssl) {
            est_send_http_error(ctx, http_ctx, EST_ERR_NO_SSL_CTX);
            return (EST_ERR_NO_SSL_CTX);
        }

        rc = est_handle_simple_enroll(ctx, http_ctx, ssl, ct, body, body_len, 0);
        if (rc != EST_ERR_NONE && rc != EST_ERR_AUTH_PENDING) {
            EST_LOG_WARN("Enrollment failed with rc=%d (%s)\n", 
		         rc, EST_ERR_NUM_TO_STR(rc));
	    if (rc == EST_ERR_AUTH_FAIL) {
		est_send_http_error(ctx, http_ctx, EST_ERR_AUTH_FAIL);
	    } else {
		est_send_http_error(ctx, http_ctx, EST_ERR_BAD_PKCS10);
	    }
            return rc;
        }
    }

    /*
     * See if this is a re-enrollment request
     */
    if (strncmp(uri, EST_RE_ENROLL_URI, EST_URI_MAX_LEN) == 0) {
        /* Only POST is allowed */
        if (strncmp(method, "POST", 4)) {
            EST_LOG_WARN("Incoming HTTP request used wrong method\n");
            est_send_http_error(ctx, http_ctx, EST_ERR_WRONG_METHOD);
            return (EST_ERR_WRONG_METHOD);
        }
	if (!ct) {
            EST_LOG_WARN("Incoming HTTP header has no Content-Type header\n");
            est_send_http_error(ctx, http_ctx, EST_ERR_BAD_PKCS10);
	    return (EST_ERR_BAD_CONTENT_TYPE); 
	}
        /*
         * Get the SSL context, which is required for authenticating
         * the client.
         */
        ssl = (SSL*)mg_get_conn_ssl(http_ctx);
        if (!ssl) {
            est_send_http_error(ctx, http_ctx, EST_ERR_NO_SSL_CTX);
            return (EST_ERR_NO_SSL_CTX);
        }

        rc = est_handle_simple_enroll(ctx, http_ctx, ssl, ct, body, body_len, 1);
        if (rc != EST_ERR_NONE && rc != EST_ERR_AUTH_PENDING) {
            EST_LOG_WARN("Reenroll failed with rc=%d (%s)\n", 
		         rc, EST_ERR_NUM_TO_STR(rc));
	    if (rc == EST_ERR_AUTH_FAIL) {
		est_send_http_error(ctx, http_ctx, EST_ERR_AUTH_FAIL);
	    } else {
		est_send_http_error(ctx, http_ctx, EST_ERR_BAD_PKCS10);
	    }
            return (EST_ERR_BAD_PKCS10);
        }
    }

#if 0
    /*
     * See if this is a keygen request
     * FIXME: this is currently not implemented
     */
    if (strncmp(uri, EST_KEYGEN_URI, EST_URI_MAX_LEN) == 0) {
        /* Only POST is allowed */
        if (strncmp(method, "POST", 4)) {
            EST_LOG_WARN("Incoming HTTP request used wrong method\n");
            est_send_http_error(ctx, http_ctx, EST_ERR_WRONG_METHOD);
            return (EST_ERR_WRONG_METHOD);
        }
	if (!ct) {
            EST_LOG_WARN("Incoming HTTP header has no Content-Type header\n");
	    return (EST_ERR_BAD_CONTENT_TYPE); 
	}
        if (est_handle_keygen(ctx)) {
            est_send_http_error(ctx, http_ctx, 0); //FIXME: last param should not be zero
            return (EST_ERR_HTTP_WRITE);           //FIXME: need the appropriate return code
        }
    }
#endif

    /*
     * See if this is a CSR attributes request
     */
    if (strncmp(uri, EST_CSR_ATTRS_URI, EST_URI_MAX_LEN) == 0) {
        /* Only GET is allowed */
        if (strncmp(method, "GET", 4)) {
            EST_LOG_WARN("Incoming HTTP request used wrong method\n");
            est_send_http_error(ctx, http_ctx, EST_ERR_WRONG_METHOD);
            return (EST_ERR_WRONG_METHOD);
        }

        rc = est_handle_csr_attrs(ctx, http_ctx);
	if (rc != EST_ERR_NONE) {
            est_send_http_error(ctx, http_ctx, rc); 
            return (rc);
        }
    }

    return (EST_ERR_NONE);
}


/*! @brief est_server_start() is used by an application to start
    the EST server after est_server_init() has been called and
    all the required callback functions have been provided by
    the application.   
 
    @param ctx Pointer to the EST context

    libest uses HTTP code from the Mongoose HTTP server.
    This function allows the application to start the HTTP
    services layer, which is required by EST.
 
    @return EST_ERROR.
 */
EST_ERROR est_server_start (EST_CTX *ctx)
{
    EST_MG_CONTEXT *mgctx;

    if (!ctx) {
	EST_LOG_ERR("Null context");
	return (EST_ERR_NO_CTX);
    }

    mgctx = mg_start(ctx);
    if (mgctx) {
        ctx->mg_ctx = mgctx;
        return (EST_ERR_NONE);
    } else {
        return (EST_ERR_NO_SSL_CTX);
    }
}

/*! @brief est_server_stop() is used by an application to stop
    the EST server.  This should be called prior to est_destroy().
 
    @param ctx Pointer to the EST context

    libest uses HTTP code from the Mongoose HTTP server.
    This function allows the application to stop the HTTP
    services layer.
 
    @return EST_ERROR.
 */
EST_ERROR est_server_stop (EST_CTX *ctx)
{
    EST_MG_CONTEXT *mgctx;

    if (!ctx) {
	EST_LOG_ERR("Null context");
	return (EST_ERR_NO_CTX);
    }

    mgctx = (EST_MG_CONTEXT*)ctx->mg_ctx;
    if (mgctx) {
        mg_stop(mgctx);
    }
    return (EST_ERR_NONE);
}

/*! @brief est_server_init() is used by an application to create
    a context in the EST library when operating as an EST server that
    fronts a CA.  This context is used when invoking other functions in the API.
 
    @param ca_chain     Char array containing PEM encoded CA certs & CRL entries 
    @param ca_chain_len Length of ca_chain char array 
    @param cacerts_resp_chain Char array containing PEM encoded CA certs to include
                              in the /cacerts response
    @param cacerts_resp_chain_len Length of cacerts_resp_chain char array
    @param cert_format Specifies the encoding of the local and external
                       certificate chains (PEM/DER).  
    @param http_realm Char array containing HTTP realm name for HTTP auth
    @param tls_id_cert Pointer to X509 that contains the server's certificate
                    for the TLS layer.
    @param tls_id_key Pointer to EVP_PKEY that contains the private key
                   associated with the server's certificate.

    This function allows an application to initialize an EST server context
    that is used with a CA (not an RA).
    The application must provide the trusted CA certificates to use
    for server operation using the ca_chain parameter.  This certificate
    set should include the explicit trust anchor certificate, any number
    of implicit trust anchor certificates, and any intermediate sub-CA
    certificates required to complete the chain of trust between the
    identity certificate passed into the tls_id_cert parameter and the
    root certificate for that identity certificate.  
    The CA certificates should be encoded using
    the format specified in the cert_format parameter (e.g. PEM) and
    may contain CRL entries that will be used when authenticating
    EST clients connecting to the server.  
    The applications must also provide the HTTP realm to use for 
    HTTP authentication and the server cerificate/private key to use
    for the TLS stack to identify the server.
    
    Warning: Including additional intermediate sub-CA certificates that are
             not needed to complete the chain of trust may result in a
	     potential MITM attack.  
 
    @return EST_CTX.
 */
EST_CTX * est_server_init (unsigned char *ca_chain, int ca_chain_len,
                           unsigned char *cacerts_resp_chain, int cacerts_resp_chain_len,
			   EST_CERT_FORMAT cert_format,
                           char *http_realm, 
			   X509 *tls_id_cert, EVP_PKEY *tls_id_key)
{
    EST_CTX *ctx;
    int len;

    est_log_version();

    /*
     * Sanity check the input
     */
    if (ca_chain == NULL) {
        EST_LOG_ERR("Trusted CA certificate set is empty");
        return NULL;
    }
    if (cacerts_resp_chain == NULL) {
        EST_LOG_ERR("CA certificates response set is empty");
        return NULL;
    }

    if (cert_format != EST_CERT_FORMAT_PEM) {
        EST_LOG_ERR("Only PEM encoding of certificate changes is supported.");
        return NULL;
    }

    /* 
     * Check the length value, it should match.
     * We are not using safelib here because the max
     * string length in safelib is 4096, which isn't
     * enough to hold all the CA certs 
     */
    len = strnlen((char *)ca_chain, EST_CA_MAX);
    if (len != ca_chain_len) {
	EST_LOG_ERR("Length of ca_chain doesn't match ca_chain_len");
        return NULL;
    }
    len = strnlen((char *)cacerts_resp_chain, EST_CA_MAX);
    if (len != cacerts_resp_chain_len) {
	EST_LOG_ERR("Length of cacerts_resp_chain doesn't match cacerts_resp_chain_len");
        return NULL;
    }

    if (tls_id_cert == NULL) {
        EST_LOG_ERR("TLS identity cert is empty");
        return NULL;
    }

    if (tls_id_key == NULL) {
        EST_LOG_ERR("Private key associated with TLS identity cert is empty");
        return NULL;
    }
    if (http_realm == NULL) {
        EST_LOG_ERR("EST HTTP realm is NULL");
        return NULL;
    }

    ctx = malloc(sizeof(EST_CTX));
    if (!ctx) {
        EST_LOG_ERR("malloc failed");
        return NULL;
    }
    memset(ctx, 0, sizeof(EST_CTX));
    ctx->est_mode = EST_SERVER;
    ctx->retry_period = EST_RETRY_PERIOD_DEF;
    ctx->require_http_auth = HTTP_AUTH_REQUIRED;

    /*
     * Load the CA certificates into local memory and retain
     * for future use.  This will be used for /cacerts requests.
     */
    if (est_load_ca_certs(ctx, cacerts_resp_chain, cacerts_resp_chain_len)) {
        EST_LOG_ERR("Failed to load CA certificates response buffer");
	free(ctx);
        return NULL;
    }
    if (est_load_trusted_certs(ctx, ca_chain, ca_chain_len)) {
        EST_LOG_ERR("Failed to load trusted certficate store");
	free(ctx);
        return NULL;
    }

    strncpy(ctx->realm, http_realm, MAX_REALM);
    ctx->server_cert = tls_id_cert;
    ctx->server_priv_key = tls_id_key;
    ctx->auth_mode = AUTH_BASIC;
    ctx->server_enable_pop = 1;

    /* 
     * Create a new ASN object for the id-kp-cmcRA OID.  
     * OpenSSL doesn't define this, so we need to create it
     * ourselves.
     * http://www.openssl.org/docs/crypto/OBJ_nid2obj.html
     */
    if (!o_cmcRA) {
	o_cmcRA = OBJ_txt2obj("1.3.6.1.5.5.7.3.28", 1);
	if (!o_cmcRA) {
	    EST_LOG_WARN("Failed to create OID for id-kp-cmcRA key usage checks");
	}
    }

    return (ctx);
}

/*! @brief est_server_set_auth_mode() is used by an application to configure
    the HTTP authentication method to use for validating the identity of
    and EST client.
 
    @param ctx Pointer to the EST context
    @param amode Should be either AUTH_BASIC or AUTH_DIGEST

    This function can optionally be invoked by the application to change
    the default HTTP authentication mode.  The default mode is HTTP Basic
    authentication.  An application may desire to use Digest authentication
    instead, in which case this function can be used to set that mode.  
    This function should be invoked prior to starting the EST server.  

    @return EST_ERROR.
 */
EST_ERROR est_server_set_auth_mode (EST_CTX *ctx, EST_HTTP_AUTH_MODE amode)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    switch (amode) {
    case AUTH_BASIC:
    case AUTH_DIGEST:
	if (FIPS_mode()) {
	    /*
	     * Since HTTP digest auth uses MD5, make sure we're
	     * not in FIPS mode.
	     */
	    EST_LOG_ERR("HTTP digest auth not allowed while in FIPS mode");
	    return (EST_ERR_BAD_MODE);
	}
	ctx->auth_mode = amode;
	return (EST_ERR_NONE);
	break;
    default:
        EST_LOG_ERR("Unsupported HTTP authentication mode, only Basic and Digest allowed");
	return (EST_ERR_BAD_MODE);
	break;
    }
}

/*! @brief est_set_ca_enroll_cb() is used by an application to install
    a handler for signing incoming PKCS10 requests.  
 
    @param ctx Pointer to the EST context
    @param cb Function address of the handler

    This function must be called prior to starting the EST server.  The
    callback function must match the following prototype:

        int func(unsigned char*, int, unsigned char**, int*, char*, X509*)

    This function is called by libest when a certificate request
    needs to be signed by the CA server.  The application will need
    to forward the request to the signing authority and return
    the response.  The response should be a PKCS7 signed certificate.
 
    @return EST_ERROR.
 */
EST_ERROR est_set_ca_enroll_cb (EST_CTX *ctx, int (*cb)(unsigned char *pkcs10, int p10_len,
                                                  unsigned char **pkcs7, int *pkcs7_len,
						  char *user_id, X509 *peer_cert,
						  void *ex_data))
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    ctx->est_enroll_pkcs10_cb = cb;

    return (EST_ERR_NONE);
}

/*! @brief est_set_ca_reenroll_cb() is used by an application to install
    a handler for re-enrolling certificates.  
 
    @param ctx Pointer to the EST context
    @param cb Function address of the handler

    This function must be called prior to starting the EST server.  The
    callback function must match the following prototype:

        int func(unsigned char*, int, unsigned char**, int*, char*, X509*)

    This function is called by libest when a certificate 
    needs to be renewed by the CA server.  The application will need
    to forward the request to the signing authority and return
    the response.  The response should be a PKCS7 signed certificate.
 
    @return EST_ERROR.
 */
EST_ERROR est_set_ca_reenroll_cb (EST_CTX *ctx, int (*cb)(unsigned char *pkcs10, int p10_len,
                                                  unsigned char **pkcs7, int *pkcs7_len,
						  char *user_id, X509 *peer_cert,
						  void *ex_data))
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    ctx->est_reenroll_pkcs10_cb = cb;

    return (EST_ERR_NONE);
}

/*! @brief est_set_csr_cb() is used by an application to install
    a handler for retrieving the CSR attributes from the
    CA server.  
 
    @param ctx Pointer to the EST context
    @param cb Function address of the handler

    This function must be called prior to starting the EST server.  The
    callback function must match the following prototype:

        char * func(int*)

    This function is called by libest when a CSR attributes 
    request is received.  The attributes are provided by the CA
    server and returned as a char array.
 
    @return EST_ERROR.
 */
EST_ERROR est_set_csr_cb (EST_CTX *ctx, unsigned char *(*cb)(int*csr_len, void *ex_data))
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    /*
     * Verify the context is for a server, not a client or proxy
     */
    if (ctx->est_mode != EST_SERVER) {
        return (EST_ERR_BAD_MODE);
    }

    ctx->est_get_csr_cb = cb;

    return (EST_ERR_NONE);
}

/*! @brief est_set_http_auth_cb() is used by an application to install
    a handler for authenticating EST clients.
 
    @param ctx Pointer to the EST context
    @param cb Function address of the handler

    This function must be called prior to starting the EST server.  The
    callback function must match the following prototype:

        int func(EST_CTX *, EST_HTTP_AUTH_HDR *, X509 *)

    This function is called by libest when a performing HTTP authentication.
    libest will pass the EST_HTTP_AUTH_HDR struct to the application,
    allowing the application to hook into a Radius, AAA, or some user
    authentication database.  The X509 certificate from the TLS 
    peer (EST client) is also provided through this callback facility, allowing
    the application layer to check for specific attributes in the 
    X509 certificate such as an 802.1AR device ID.
 
    @return EST_ERROR.
 */
EST_ERROR est_set_http_auth_cb (EST_CTX *ctx, 
                                int (*cb)(EST_CTX *ctx, 
                                          EST_HTTP_AUTH_HDR *ah, 
                                          X509 *peer_cert,
					  void *ex_data))
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    ctx->est_http_auth_cb = cb;

    return (EST_ERR_NONE);
}

/*! @brief est_set_http_auth_required() is used by an application to define whether
    HTTP authentication should be required in addition to using client certificates.
 
    @param ctx Pointer to the EST context
    @param required Flag indicating that HTTP authentication is required. Set 
    to HTTP_AUTH_REQUIRED value to require HTTP auth.  Set to HTTP_AUTH_NOT_REQUIRED 
    if HTTP auth should occur only when TLS client authentication fails.
 
    @return EST_ERROR.

    The default mode is HTTP_AUTH_REQUIRED.  This means that HTTP authentication
    will be attempted even when TLS client authentication succeeds.  If HTTP
    authentication is only needed when TLS client auth fails, then set this
    to HTTP_AUTH_NOT_REQUIRED.
 */
EST_ERROR est_set_http_auth_required (EST_CTX *ctx, EST_HTTP_AUTH_REQUIRED required)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    ctx->require_http_auth = required;

    return (EST_ERR_NONE);
}

/*! @brief est_server_enable_pop() is used by an application to enable 
    the proof-of-possession check on the EST server.  This proves the 
    EST client that sent the CSR to the server is in possesion of the 
    private key that was used to sign the CSR.  This binds the TLS 
    session ID to the CSR.

    Note, if the CSR attributes configured on the server require PoP 
    checking, then there is no need to call this function to enable
    PoP.  The PoP will be enabled automatically under this scenario.
    
    Note, PoP checking is not possible when an EST proxy is used to
    between the EST client and EST server.  Since the proxy will not 
    be in possession of the private key, an EST server woul fail the
    PoP check.  However, an EST proxy can enable this feature to ensure 
    the EST client has the signing key.

    @param ctx Pointer to the EST context

    This function may be called at any time.   
 
    @return EST_ERROR.
 */
EST_ERROR est_server_enable_pop (EST_CTX *ctx)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    ctx->server_enable_pop = 1;
    return (EST_ERR_NONE);
}

/*! @brief est_server_disable_pop() is used by an application to disable 
    the proof-of-possession check on the EST server.  Please see
    the documenation for est_server_enable_pop() for more information
    on the proof-of-possession check.

    @param ctx Pointer to the EST context

    This function may be called at any time.   
 
    @return EST_ERROR.
 */
EST_ERROR est_server_disable_pop (EST_CTX *ctx)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    ctx->server_enable_pop = 0;
    return (EST_ERR_NONE);
}

/*! @brief est_server_set_retry_period() is used by an application to  
    change the default retry-after period sent to the EST client when
    the CA server is not configured for auto-enroll.  This retry-after
    value notifies the client about how long it should wait before
    attempting the enroll operation again to see if the CA has 
    approved the original CSR. 
 
    @param ctx Pointer to the EST context
    @param seconds Number of seconds the server will use in the
           retry-after response.

    This function may be called at any time after a context has
    been created.   
 
    @return EST_ERROR.
 */
EST_ERROR est_server_set_retry_period (EST_CTX *ctx, int seconds)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }

    if (seconds > EST_RETRY_PERIOD_MAX) {
	EST_LOG_ERR("Maximum retry-after period is %d seconds",
		EST_RETRY_PERIOD_MAX);
        return (EST_ERR_INVALID_PARAMETERS);
    }

    if (seconds < EST_RETRY_PERIOD_MIN) {
	EST_LOG_ERR("Minimum retry-after period is %d seconds",
		EST_RETRY_PERIOD_MIN);
        return (EST_ERR_INVALID_PARAMETERS);
    }

    ctx->retry_period = seconds;
    return (EST_ERR_NONE);
}

/*! @brief est_server_set_ecdhe_curve() is used by an application to 
    specify the ECC curve that should be used for ephemeral diffie-hellman
    keys during the TLS handshake.  Ephemeral diffie-hellman is enabled
    by libest and provides better forward secrecy.  If the curve
    is not specified by the application using this function, then
    the prime256v1 curve is used as the default curve.  
 
    @param ctx Pointer to the EST context
    @param nid OpenSSL NID value for the desired curve

    This function must be called prior to starting the EST server.  
    The NID values are defined in <openssl/obj_mac.h>.  Typical NID 
    values provided to this function would include:
	
	NID_X9_62_prime192v1
	NID_X9_62_prime256v1
	NID_secp384r1
	NID_secp521r1
 
    @return EST_ERROR.
 */
EST_ERROR est_server_set_ecdhe_curve (EST_CTX *ctx, int nid)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }
    if (nid <= 0) {
	EST_LOG_ERR("Invalid NID value");
        return (EST_ERR_INVALID_PARAMETERS);
    }

    ctx->ecdhe_nid = nid;
    return (EST_ERR_NONE);
}

/*! @brief est_server_set_dh_parms() is used by an application to 
    specify the Diffie-Hellman parameters to be used for single
    use DH key generation during the TLS handshake.  If these 
    parameters are not used, then single-use DH key generation
    is not enabled.  This should be enabled to improve the 
    forward secrecy of the TLS handshake operation.  
    
    The DH parameters provided through this API should not be
    hard-coded in the application.  The parameters should be
    generated at the time of product installation.  Reusing the
    parameters across multiple installations of the product
    results in a vulnerable product.  
 
    @param ctx Pointer to the EST context
    @param parms Pointer to OpenSSL DH parameters

    This function must be called prior to starting the EST server.  
 
    @return EST_ERROR.
 */
EST_ERROR est_server_set_dh_parms (EST_CTX *ctx, DH *parms)
{
    if (!ctx) {
	EST_LOG_ERR("Null context");
        return (EST_ERR_NO_CTX);
    }
    if (!parms) {
        EST_LOG_ERR("Null DH parameters");
        return (EST_ERR_INVALID_PARAMETERS);
    }
    ctx->dh_tmp = DHparams_dup(parms);
    return (EST_ERR_NONE);
}

/*! @brief est_server_init_csrattrs() is used by an application to 
    initialize a fixed set of CSR attributes.  These attributes will
    be used by libest in response to a client CSR attributes
    request.  The attributes must be an ASN.1 base64 encoded character
    string.

    @param ctx Pointer to the EST context
    @param csrattrs Pointer CSR attributes in ASN.1 base64 encoded format,
                    a NULL pointer clears the attributes and length.
    @param csrattrs_len Length of the CSR attributes character string

    The est_get_csr_cb callback function maintains precendence over this
    method for CSR attributes. If est_get_csr_cb is initialized by the
    application it will be used.  If not, then libest will use the
    attributes initialized here.

    This function should be called prior to starting the EST server.  
    PoP configuration(est_server_enable_pop or est_server_disable_pop)
    should be called prior to this function.
    
    @return EST_ERROR.
 */
EST_ERROR est_server_init_csrattrs (EST_CTX *ctx, char *csrattrs, int csrattrs_len)
{
    int csrattrs_pop_len, pop_present, rv;
    char *csrattrs_data_pop = NULL;

    if (ctx == NULL) {
        return (EST_ERR_NO_CTX);
    }

    /*
     * Verify the context is for a server, not a client or proxy
     */
    if (ctx->est_mode != EST_SERVER) {
        return (EST_ERR_BAD_MODE);
    }

    EST_LOG_INFO("Attributes pointer is %d, len=%d", 
		 ctx->server_csrattrs, ctx->server_csrattrs_len);

    /* Free old version if previously initialized */
    if (ctx->server_csrattrs != NULL) {
        free(ctx->server_csrattrs);
        ctx->server_csrattrs = NULL;
        ctx->server_csrattrs_len = 0;
    }

    /* caller just wanted to clear it, so return */
    if (csrattrs == NULL) {
	return (EST_ERR_NONE);
    }

    /*
     * In order to run Client negative unit testing the parameter, 
     * PoP and parse checks all need to be disabled via #define
     * in a couple of places here.
     */

    /* 
     * check smallest possible base64 case here for now 
     * and sanity test will check min/max value for ASN.1 data
     */
    if (csrattrs_len < MIN_CSRATTRS) {
        return (EST_ERR_INVALID_PARAMETERS);
    }

    /* assume PoP not in CSR attributes */
    ctx->csr_pop_present = 0;
    if (ctx->server_enable_pop) {
        rv = est_is_challengePassword_present(csrattrs, csrattrs_len, &pop_present);
	if (rv != EST_ERR_NONE) {
	    EST_LOG_ERR("Error during PoP/sanity check");
	    return (EST_ERR_INVALID_PARAMETERS);
	}
	ctx->csr_pop_present = pop_present;

	if (!ctx->csr_pop_present) {
	    rv = est_add_challengePassword(csrattrs, csrattrs_len, 
					   &csrattrs_data_pop, &csrattrs_pop_len);
	    if (rv != EST_ERR_NONE) {
		EST_LOG_ERR("Error during add PoP");
		return (EST_ERR_INVALID_PARAMETERS);
	    }
	    csrattrs = csrattrs_data_pop;
	    csrattrs_len = csrattrs_pop_len;
	}
    } else {
        rv = est_asn1_parse_attributes(csrattrs, csrattrs_len, &pop_present);
	if (rv != EST_ERR_NONE) {
	    EST_LOG_ERR("Corrupt CSR Attributes");
	    return (EST_ERR_INVALID_PARAMETERS);
	}
    }    

    ctx->server_csrattrs = malloc(csrattrs_len + 1);
    if (!ctx->server_csrattrs) {
        if (csrattrs_data_pop) {
            free(csrattrs_data_pop);
	}
        return (EST_ERR_MALLOC);
    }
    ctx->server_csrattrs_len = csrattrs_len;

    strncpy((char *)ctx->server_csrattrs, csrattrs, csrattrs_len);
    ctx->server_csrattrs[csrattrs_len] = 0;
    if (csrattrs_data_pop) {
      free(csrattrs_data_pop);
    }
    EST_LOG_INFO("Attributes pointer is %d, len=%d", ctx->server_csrattrs, 
		 ctx->server_csrattrs_len);
    return (EST_ERR_NONE);
}

