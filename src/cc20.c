/**
 * (C) 2007-20 - ntop.org and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not see see <http://www.gnu.org/licenses/>
 *
 */


#include "cc20.h"


#if defined (HAVE_OPENSSL_1_1) // openSSL 1.1 ---------------------------------------------


/* get any erorr message out of openssl
   taken from https://en.wikibooks.org/wiki/OpenSSL/Error_handling */
static char *openssl_err_as_string (void) {
  BIO *bio = BIO_new (BIO_s_mem ());
  ERR_print_errors (bio);
  char *buf = NULL;
  size_t len = BIO_get_mem_data (bio, &buf);
  char *ret = (char *) calloc (1, 1 + len);

  if(ret)
    memcpy (ret, buf, len);

  BIO_free (bio);
  return ret;
}


// encryption == decryption
int cc20_crypt (unsigned char *out, const unsigned char *in, size_t in_len,
                const unsigned char *iv, cc20_context_t *ctx) {

  int evp_len;
  int evp_ciphertext_len;

  if(1 == EVP_EncryptInit_ex(ctx->ctx, ctx->cipher, NULL, ctx->key, iv)) {
    if(1 == EVP_CIPHER_CTX_set_padding(ctx->ctx, 0)) {
      if(1 == EVP_EncryptUpdate(ctx->ctx, out, &evp_len, in, in_len)) {
        evp_ciphertext_len = evp_len;
        if(1 == EVP_EncryptFinal_ex(ctx->ctx, out + evp_len, &evp_len)) {
          evp_ciphertext_len += evp_len;
          if(evp_ciphertext_len != in_len)
            traceEvent(TRACE_ERROR, "cc20_crypt openssl encryption: encrypted %u bytes where %u were expected",
                                    evp_ciphertext_len, in_len);
        } else
          traceEvent(TRACE_ERROR, "cc20_crypt openssl final encryption: %s",
                                  openssl_err_as_string());
      } else
        traceEvent(TRACE_ERROR, "cc20_encrypt openssl encrpytion: %s",
                                openssl_err_as_string());
    } else
      traceEvent(TRACE_ERROR, "cc20_encrypt openssl padding setup: %s",
                              openssl_err_as_string());
  } else
    traceEvent(TRACE_ERROR, "cc20_encrypt openssl init: %s",
                            openssl_err_as_string());

  EVP_CIPHER_CTX_reset(ctx->ctx);

  return 0;
}


#else // plain C --------------------------------------------------------------------------


// taken (and modified) from https://github.com/Ginurx/chacha20-c (public domain)


static void chacha20_init_block(cc20_context_t *ctx, const uint8_t nonce[]) {

  const uint8_t *magic_constant = (uint8_t*)"expand 32-byte k";

  memcpy(&(ctx->state[ 0]), magic_constant, 16);
  memcpy(&(ctx->state[ 4]), ctx->key, CC20_KEY_BYTES);
  memcpy(&(ctx->state[12]), nonce, CC20_IV_SIZE);
}


#define ROL32(x,r) (((x)<<(r))|((x)>>(32-(r))))
#define CHACHA20_QUARTERROUND(x, a, b, c, d)     \
    x[a] += x[b]; x[d] = ROL32(x[d] ^ x[a], 16); \
    x[c] += x[d]; x[b] = ROL32(x[b] ^ x[c], 12); \
    x[a] += x[b]; x[d] = ROL32(x[d] ^ x[a],  8); \
    x[c] += x[d]; x[b] = ROL32(x[b] ^ x[c],  7)
#define CHACHA20_DOUBLE_ROUND(s)                           \
    /* odd round */                                        \
    CHACHA20_QUARTERROUND(s, 0, 4,  8, 12); \
    CHACHA20_QUARTERROUND(s, 1, 5,  9, 13); \
    CHACHA20_QUARTERROUND(s, 2, 6, 10, 14); \
    CHACHA20_QUARTERROUND(s, 3, 7, 11, 15); \
    /* even round */                                       \
    CHACHA20_QUARTERROUND(s, 0, 5, 10, 15); \
    CHACHA20_QUARTERROUND(s, 1, 6, 11, 12); \
    CHACHA20_QUARTERROUND(s, 2, 7,  8, 13); \
    CHACHA20_QUARTERROUND(s, 3, 4,  9, 14)

static void chacha20_block_next(cc20_context_t *ctx) {

  uint32_t *counter = ctx->state + 12;
  uint32_t c;

  ctx->keystream32[ 0] = ctx->state[ 0];
  ctx->keystream32[ 1] = ctx->state[ 1];
  ctx->keystream32[ 2] = ctx->state[ 2];
  ctx->keystream32[ 3] = ctx->state[ 3];
  ctx->keystream32[ 4] = ctx->state[ 4];
  ctx->keystream32[ 5] = ctx->state[ 5];
  ctx->keystream32[ 6] = ctx->state[ 6];
  ctx->keystream32[ 7] = ctx->state[ 7];
  ctx->keystream32[ 8] = ctx->state[ 8];
  ctx->keystream32[ 9] = ctx->state[ 9];
  ctx->keystream32[10] = ctx->state[10];
  ctx->keystream32[11] = ctx->state[11];
  ctx->keystream32[12] = ctx->state[12];
  ctx->keystream32[13] = ctx->state[13];
  ctx->keystream32[14] = ctx->state[14];
  ctx->keystream32[15] = ctx->state[15];

  // 10 double rounds
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);
  CHACHA20_DOUBLE_ROUND(ctx->keystream32);

  ctx->keystream32[ 0] += ctx->state[ 0];
  ctx->keystream32[ 1] += ctx->state[ 1];
  ctx->keystream32[ 2] += ctx->state[ 2];
  ctx->keystream32[ 3] += ctx->state[ 3];
  ctx->keystream32[ 4] += ctx->state[ 4];
  ctx->keystream32[ 5] += ctx->state[ 5];
  ctx->keystream32[ 6] += ctx->state[ 6];
  ctx->keystream32[ 7] += ctx->state[ 7];
  ctx->keystream32[ 8] += ctx->state[ 8];
  ctx->keystream32[ 9] += ctx->state[ 9];
  ctx->keystream32[10] += ctx->state[10];
  ctx->keystream32[11] += ctx->state[11];
  ctx->keystream32[12] += ctx->state[12];
  ctx->keystream32[13] += ctx->state[13];
  ctx->keystream32[14] += ctx->state[14];
  ctx->keystream32[15] += ctx->state[15];

  // increment counter, make sure it is and stays little endian in memory
  c = le32toh(counter[0]);
  counter[0] = htole32(++c);
  if(0 == counter[0]) {
    // wrap around occured, increment higher 32 bits of counter
    // unlikely with 1,500 byte sized packets
    c = le32toh(counter[1]);
    counter[1] = htole32(++c);
    if(0 == counter[1]) {
      // very unlikely
      c = le32toh(counter[2]);
      counter[2] = htole32(++c);
      if(0 == counter[2]) {
        // extremely unlikely
        c = le32toh(counter[3]);
        counter[3] = htole32(++c);
      }
    }
  }
}


static void chacha20_init_context(cc20_context_t *ctx, const uint8_t *nonce) {

  chacha20_init_block(ctx, nonce);
}


int cc20_crypt (unsigned char *out, const unsigned char *in, size_t in_len,
                const unsigned char *iv, cc20_context_t *ctx) {

  uint8_t   *keystream8 = (uint8_t*)ctx->keystream32;
  uint32_t * in_p       = (uint32_t*)in;
  uint32_t * out_p      = (uint32_t*)out;
  size_t   tmp_len      = in_len;

  chacha20_init_context(ctx, iv);

  while(in_len >= 64) {

    chacha20_block_next(ctx);

    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 0]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 1]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 2]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 3]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 4]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 5]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 6]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 7]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 8]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[ 9]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[10]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[11]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[12]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[13]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[14]; in_p++; out_p++;
    *(uint32_t*)out_p = *(uint32_t*)in_p ^ ctx->keystream32[15]; in_p++; out_p++;
    in_len -= 64;
  }

  if(in_len > 0) {

    chacha20_block_next(ctx);

    tmp_len -= in_len;
    while(in_len > 0) {
      out[tmp_len] = in[tmp_len] ^ keystream8[tmp_len%64];
      tmp_len++;
      in_len--;
    }
  }
}


#endif // openSSL 1.1, plain C ------------------------------------------------------------


int cc20_init (const unsigned char *key, cc20_context_t **ctx) {

 // allocate context...
  *ctx = (cc20_context_t*) calloc(1, sizeof(cc20_context_t));
  if (!(*ctx))
    return -1;
#if defined (HAVE_OPENSSL_1_1)
  if(!((*ctx)->ctx = EVP_CIPHER_CTX_new())) {
    traceEvent(TRACE_ERROR, "cc20_init openssl's evp_* encryption context creation failed: %s",
                            openssl_err_as_string());
    return -1;
  }

  (*ctx)->cipher = EVP_chacha20();
#endif
  memcpy((*ctx)->key, key, CC20_KEY_BYTES);

  return 0;
}


int cc20_deinit (cc20_context_t *ctx) {

#if defined (HAVE_OPENSSL_1_1)
  if (ctx->ctx) EVP_CIPHER_CTX_free(ctx->ctx);
#endif
  return 0;
}
