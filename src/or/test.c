/* Copyright 2001-2004 Roger Dingledine.
 * Copyright 2004-2007 Roger Dingledine, Nick Mathewson. */
/* See LICENSE for licensing information */
/* $Id$ */
const char test_c_id[] =
  "$Id$";

const char tor_svn_revision[] = "";

/**
 * \file test.c
 * \brief Unit tests for many pieces of the lower level Tor modules.
 **/

#include "orconfig.h"
#include <stdio.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef MS_WINDOWS
/* For mkdir() */
#include <direct.h>
#else
#include <dirent.h>
#endif

/* These macros pull in declarations for some functions and structures that
 * are typically file-private. */
#define CONFIG_PRIVATE
#define CONTROL_PRIVATE
#define CRYPTO_PRIVATE
#define DIRSERV_PRIVATE
#define DIRVOTE_PRIVATE
#define MEMPOOL_PRIVATE
#define ROUTER_PRIVATE

#include "or.h"
#include "test.h"
#include "torgzip.h"
#include "mempool.h"

int have_failed = 0;

static char temp_dir[256];

static void
setup_directory(void)
{
  static int is_setup = 0;
  int r;
  if (is_setup) return;

#ifdef MS_WINDOWS
  // XXXX
  tor_snprintf(temp_dir, sizeof(temp_dir),
               "c:\\windows\\temp\\tor_test_%d", (int)getpid());
  r = mkdir(temp_dir);
#else
  tor_snprintf(temp_dir, sizeof(temp_dir), "/tmp/tor_test_%d", (int) getpid());
  r = mkdir(temp_dir, 0700);
#endif
  if (r) {
    fprintf(stderr, "Can't create directory %s:", temp_dir);
    perror("");
    exit(1);
  }
  is_setup = 1;
}

static const char *
get_fname(const char *name)
{
  static char buf[1024];
  setup_directory();
  tor_snprintf(buf,sizeof(buf),"%s/%s",temp_dir,name);
  return buf;
}

static void
remove_directory(void)
{
  smartlist_t *elements = tor_listdir(temp_dir);
  if (elements) {
    SMARTLIST_FOREACH(elements, const char *, cp,
       {
         size_t len = strlen(cp)+strlen(temp_dir)+16;
         char *tmp = tor_malloc(len);
         tor_snprintf(tmp, len, "%s"PATH_SEPARATOR"%s", temp_dir, cp);
         unlink(tmp);
         tor_free(tmp);
       });
    SMARTLIST_FOREACH(elements, char *, cp, tor_free(cp));
    smartlist_free(elements);
  }
  rmdir(temp_dir);
}

static crypto_pk_env_t *
pk_generate(int idx)
{
  static crypto_pk_env_t *pregen[5] = {NULL, NULL, NULL, NULL, NULL};
  tor_assert(idx < (int)(sizeof(pregen)/sizeof(pregen[0])));
  if (! pregen[idx]) {
    pregen[idx] = crypto_new_pk_env();
    tor_assert(!crypto_pk_generate_key(pregen[idx]));
  }
  return crypto_pk_dup_key(pregen[idx]);
}

static void
test_buffers(void)
{
  char str[256];
  char str2[256];

  buf_t *buf, *buf2;

  int j;
  size_t r;

  /****
   * buf_new
   ****/
  if (!(buf = buf_new()))
    test_fail();

  test_eq(buf_capacity(buf), 4096);
  test_eq(buf_datalen(buf), 0);

  /****
   * General pointer frobbing
   */
  for (j=0;j<256;++j) {
    str[j] = (char)j;
  }
  write_to_buf(str, 256, buf);
  write_to_buf(str, 256, buf);
  test_eq(buf_datalen(buf), 512);
  fetch_from_buf(str2, 200, buf);
  test_memeq(str, str2, 200);
  test_eq(buf_datalen(buf), 312);
  memset(str2, 0, sizeof(str2));

  fetch_from_buf(str2, 256, buf);
  test_memeq(str+200, str2, 56);
  test_memeq(str, str2+56, 200);
  test_eq(buf_datalen(buf), 56);
  memset(str2, 0, sizeof(str2));
  /* Okay, now we should be 512 bytes into the 4096-byte buffer.  If we add
   * another 3584 bytes, we hit the end. */
  for (j=0;j<15;++j) {
    write_to_buf(str, 256, buf);
  }
  assert_buf_ok(buf);
  test_eq(buf_datalen(buf), 3896);
  fetch_from_buf(str2, 56, buf);
  test_eq(buf_datalen(buf), 3840);
  test_memeq(str+200, str2, 56);
  for (j=0;j<15;++j) {
    memset(str2, 0, sizeof(str2));
    fetch_from_buf(str2, 256, buf);
    test_memeq(str, str2, 256);
  }
  test_eq(buf_datalen(buf), 0);
  buf_free(buf);

  /* Okay, now make sure growing can work. */
  buf = buf_new_with_capacity(16);
  test_eq(buf_capacity(buf), 16);
  write_to_buf(str+1, 255, buf);
  test_eq(buf_capacity(buf), 256);
  fetch_from_buf(str2, 254, buf);
  test_memeq(str+1, str2, 254);
  test_eq(buf_capacity(buf), 256);
  assert_buf_ok(buf);
  write_to_buf(str, 32, buf);
  test_eq(buf_capacity(buf), 256);
  assert_buf_ok(buf);
  write_to_buf(str, 256, buf);
  assert_buf_ok(buf);
  test_eq(buf_capacity(buf), 512);
  test_eq(buf_datalen(buf), 33+256);
  fetch_from_buf(str2, 33, buf);
  test_eq(*str2, str[255]);

  test_memeq(str2+1, str, 32);
  test_eq(buf_capacity(buf), 512);
  test_eq(buf_datalen(buf), 256);
  fetch_from_buf(str2, 256, buf);
  test_memeq(str, str2, 256);

  /* now try shrinking: case 1. */
  buf_free(buf);
  buf = buf_new_with_capacity(33668);
  for (j=0;j<67;++j) {
    write_to_buf(str,255, buf);
  }
  test_eq(buf_capacity(buf), 33668);
  test_eq(buf_datalen(buf), 17085);
  for (j=0; j < 40; ++j) {
    fetch_from_buf(str2, 255,buf);
    test_memeq(str2, str, 255);
  }

  /* now try shrinking: case 2. */
  buf_free(buf);
  buf = buf_new_with_capacity(33668);
  for (j=0;j<67;++j) {
    write_to_buf(str,255, buf);
  }
  for (j=0; j < 20; ++j) {
    fetch_from_buf(str2, 255,buf);
    test_memeq(str2, str, 255);
  }
  for (j=0;j<80;++j) {
    write_to_buf(str,255, buf);
  }
  test_eq(buf_capacity(buf),33668);
  for (j=0; j < 120; ++j) {
    fetch_from_buf(str2, 255,buf);
    test_memeq(str2, str, 255);
  }

  /* Move from buf to buf. */
  buf_free(buf);
  buf = buf_new_with_capacity(4096);
  buf2 = buf_new_with_capacity(4096);
  for (j=0;j<100;++j)
    write_to_buf(str, 255, buf);
  test_eq(buf_datalen(buf), 25500);
  for (j=0;j<100;++j) {
    r = 10;
    move_buf_to_buf(buf2, buf, &r);
    test_eq(r, 0);
  }
  test_eq(buf_datalen(buf), 24500);
  test_eq(buf_datalen(buf2), 1000);
  for (j=0;j<3;++j) {
    fetch_from_buf(str2, 255, buf2);
    test_memeq(str2, str, 255);
  }
  r = 8192; /*big move*/
  move_buf_to_buf(buf2, buf, &r);
  test_eq(r, 0);
  r = 30000; /* incomplete move */
  move_buf_to_buf(buf2, buf, &r);
  test_eq(r, 13692);
  for (j=0;j<97;++j) {
    fetch_from_buf(str2, 255, buf2);
    test_memeq(str2, str, 255);
  }
  buf_free(buf);
  buf_free(buf2);

#if 0
  {
  int s;
  int eof;
  int i;
  buf_t *buf2;
  /****
   * read_to_buf
   ****/
  s = open(get_fname("data"), O_WRONLY|O_CREAT|O_TRUNC, 0600);
  write(s, str, 256);
  close(s);

  s = open(get_fname("data"), O_RDONLY, 0);
  eof = 0;
  errno = 0; /* XXXX */
  i = read_to_buf(s, 10, buf, &eof);
  printf("%s\n", strerror(errno));
  test_eq(i, 10);
  test_eq(eof, 0);
  test_eq(buf_capacity(buf), 4096);
  test_eq(buf_datalen(buf), 10);

  test_memeq(str, (char*)_buf_peek_raw_buffer(buf), 10);

  /* Test reading 0 bytes. */
  i = read_to_buf(s, 0, buf, &eof);
  test_eq(buf_capacity(buf), 512*1024);
  test_eq(buf_datalen(buf), 10);
  test_eq(eof, 0);
  test_eq(i, 0);

  /* Now test when buffer is filled exactly. */
  buf2 = buf_new_with_capacity(6);
  i = read_to_buf(s, 6, buf2, &eof);
  test_eq(buf_capacity(buf2), 6);
  test_eq(buf_datalen(buf2), 6);
  test_eq(eof, 0);
  test_eq(i, 6);
  test_memeq(str+10, (char*)_buf_peek_raw_buffer(buf2), 6);
  buf_free(buf2);

  /* Now test when buffer is filled with more data to read. */
  buf2 = buf_new_with_capacity(32);
  i = read_to_buf(s, 128, buf2, &eof);
  test_eq(buf_capacity(buf2), 128);
  test_eq(buf_datalen(buf2), 32);
  test_eq(eof, 0);
  test_eq(i, 32);
  buf_free(buf2);

  /* Now read to eof. */
  test_assert(buf_capacity(buf) > 256);
  i = read_to_buf(s, 1024, buf, &eof);
  test_eq(i, (256-32-10-6));
  test_eq(buf_capacity(buf), MAX_BUF_SIZE);
  test_eq(buf_datalen(buf), 256-6-32);
  test_memeq(str, (char*)_buf_peek_raw_buffer(buf), 10); /* XXX Check rest. */
  test_eq(eof, 0);

  i = read_to_buf(s, 1024, buf, &eof);
  test_eq(i, 0);
  test_eq(buf_capacity(buf), MAX_BUF_SIZE);
  test_eq(buf_datalen(buf), 256-6-32);
  test_eq(eof, 1);
  }
#endif
}

static void
test_crypto_dh(void)
{
  crypto_dh_env_t *dh1, *dh2;
  char p1[DH_BYTES];
  char p2[DH_BYTES];
  char s1[DH_BYTES];
  char s2[DH_BYTES];
  int s1len, s2len;

  dh1 = crypto_dh_new();
  dh2 = crypto_dh_new();
  test_eq(crypto_dh_get_bytes(dh1), DH_BYTES);
  test_eq(crypto_dh_get_bytes(dh2), DH_BYTES);

  memset(p1, 0, DH_BYTES);
  memset(p2, 0, DH_BYTES);
  test_memeq(p1, p2, DH_BYTES);
  test_assert(! crypto_dh_get_public(dh1, p1, DH_BYTES));
  test_memneq(p1, p2, DH_BYTES);
  test_assert(! crypto_dh_get_public(dh2, p2, DH_BYTES));
  test_memneq(p1, p2, DH_BYTES);

  memset(s1, 0, DH_BYTES);
  memset(s2, 0xFF, DH_BYTES);
  s1len = crypto_dh_compute_secret(dh1, p2, DH_BYTES, s1, 50);
  s2len = crypto_dh_compute_secret(dh2, p1, DH_BYTES, s2, 50);
  test_assert(s1len > 0);
  test_eq(s1len, s2len);
  test_memeq(s1, s2, s1len);

  crypto_dh_free(dh1);
  crypto_dh_free(dh2);
}

static void
test_crypto(void)
{
  crypto_cipher_env_t *env1, *env2;
  crypto_pk_env_t *pk1, *pk2;
  char *data1, *data2, *data3, *cp;
  int i, j, p, len, idx;
  size_t size;

  data1 = tor_malloc(1024);
  data2 = tor_malloc(1024);
  data3 = tor_malloc(1024);
  test_assert(data1 && data2 && data3);

  /* Try out RNG. */
  test_assert(! crypto_seed_rng());
  crypto_rand(data1, 100);
  crypto_rand(data2, 100);
  test_memneq(data1,data2,100);

  /* Now, test encryption and decryption with stream cipher. */
  data1[0]='\0';
  for (i = 1023; i>0; i -= 35)
    strncat(data1, "Now is the time for all good onions", i);

  memset(data2, 0, 1024);
  memset(data3, 0, 1024);
  env1 = crypto_new_cipher_env();
  test_neq(env1, 0);
  env2 = crypto_new_cipher_env();
  test_neq(env2, 0);
  j = crypto_cipher_generate_key(env1);
  crypto_cipher_set_key(env2, crypto_cipher_get_key(env1));
  crypto_cipher_encrypt_init_cipher(env1);
  crypto_cipher_decrypt_init_cipher(env2);

  /* Try encrypting 512 chars. */
  crypto_cipher_encrypt(env1, data2, data1, 512);
  crypto_cipher_decrypt(env2, data3, data2, 512);
  test_memeq(data1, data3, 512);
  test_memneq(data1, data2, 512);

  /* Now encrypt 1 at a time, and get 1 at a time. */
  for (j = 512; j < 560; ++j) {
    crypto_cipher_encrypt(env1, data2+j, data1+j, 1);
  }
  for (j = 512; j < 560; ++j) {
    crypto_cipher_decrypt(env2, data3+j, data2+j, 1);
  }
  test_memeq(data1, data3, 560);
  /* Now encrypt 3 at a time, and get 5 at a time. */
  for (j = 560; j < 1024-5; j += 3) {
    crypto_cipher_encrypt(env1, data2+j, data1+j, 3);
  }
  for (j = 560; j < 1024-5; j += 5) {
    crypto_cipher_decrypt(env2, data3+j, data2+j, 5);
  }
  test_memeq(data1, data3, 1024-5);
  /* Now make sure that when we encrypt with different chunk sizes, we get
     the same results. */
  crypto_free_cipher_env(env2);

  memset(data3, 0, 1024);
  env2 = crypto_new_cipher_env();
  test_neq(env2, 0);
  crypto_cipher_set_key(env2, crypto_cipher_get_key(env1));
  crypto_cipher_encrypt_init_cipher(env2);
  for (j = 0; j < 1024-16; j += 17) {
    crypto_cipher_encrypt(env2, data3+j, data1+j, 17);
  }
  for (j= 0; j < 1024-16; ++j) {
    if (data2[j] != data3[j]) {
      printf("%d:  %d\t%d\n", j, (int) data2[j], (int) data3[j]);
    }
  }
  test_memeq(data2, data3, 1024-16);
  crypto_free_cipher_env(env1);
  crypto_free_cipher_env(env2);

  /* Test vectors for stream ciphers. */
  /* XXXX Look up some test vectors for the ciphers and make sure we match. */

  /* Test SHA-1 with a test vector from the specification. */
  i = crypto_digest(data1, "abc", 3);
  test_memeq(data1,
             "\xA9\x99\x3E\x36\x47\x06\x81\x6A\xBA\x3E\x25\x71\x78"
             "\x50\xC2\x6C\x9C\xD0\xD8\x9D", 20);

  /* Public-key ciphers */
  pk1 = pk_generate(0);
  pk2 = crypto_new_pk_env();
  test_assert(pk1 && pk2);
  test_assert(! crypto_pk_write_public_key_to_string(pk1, &cp, &size));
  test_assert(! crypto_pk_read_public_key_from_string(pk2, cp, size));
  test_eq(0, crypto_pk_cmp_keys(pk1, pk2));
  tor_free(cp);

  test_eq(128, crypto_pk_keysize(pk1));
  test_eq(128, crypto_pk_keysize(pk2));

  test_eq(128, crypto_pk_public_encrypt(pk2, data1, "Hello whirled.", 15,
                                        PK_PKCS1_OAEP_PADDING));
  test_eq(128, crypto_pk_public_encrypt(pk1, data2, "Hello whirled.", 15,
                                        PK_PKCS1_OAEP_PADDING));
  /* oaep padding should make encryption not match */
  test_memneq(data1, data2, 128);
  test_eq(15, crypto_pk_private_decrypt(pk1, data3, data1, 128,
                                        PK_PKCS1_OAEP_PADDING,1));
  test_streq(data3, "Hello whirled.");
  memset(data3, 0, 1024);
  test_eq(15, crypto_pk_private_decrypt(pk1, data3, data2, 128,
                                        PK_PKCS1_OAEP_PADDING,1));
  test_streq(data3, "Hello whirled.");
  /* Can't decrypt with public key. */
  test_eq(-1, crypto_pk_private_decrypt(pk2, data3, data2, 128,
                                        PK_PKCS1_OAEP_PADDING,1));
  /* Try again with bad padding */
  memcpy(data2+1, "XYZZY", 5);  /* This has fails ~ once-in-2^40 */
  test_eq(-1, crypto_pk_private_decrypt(pk1, data3, data2, 128,
                                        PK_PKCS1_OAEP_PADDING,1));

  /* File operations: save and load private key */
  test_assert(! crypto_pk_write_private_key_to_filename(pk1,
                                                        get_fname("pkey1")));

  test_assert(! crypto_pk_read_private_key_from_filename(pk2,
                                                         get_fname("pkey1")));
  test_eq(15, crypto_pk_private_decrypt(pk2, data3, data1, 128,
                                        PK_PKCS1_OAEP_PADDING,1));

  /* Now try signing. */
  strlcpy(data1, "Ossifrage", 1024);
  test_eq(128, crypto_pk_private_sign(pk1, data2, data1, 10));
  test_eq(10, crypto_pk_public_checksig(pk1, data3, data2, 128));
  test_streq(data3, "Ossifrage");
  /* Try signing digests. */
  test_eq(128, crypto_pk_private_sign_digest(pk1, data2, data1, 10));
  test_eq(20, crypto_pk_public_checksig(pk1, data3, data2, 128));
  test_eq(0, crypto_pk_public_checksig_digest(pk1, data1, 10, data2, 128));
  test_eq(-1, crypto_pk_public_checksig_digest(pk1, data1, 11, data2, 128));
  /*XXXX test failed signing*/

  /* Try encoding */
  crypto_free_pk_env(pk2);
  pk2 = NULL;
  i = crypto_pk_asn1_encode(pk1, data1, 1024);
  test_assert(i>0);
  pk2 = crypto_pk_asn1_decode(data1, i);
  test_assert(crypto_pk_cmp_keys(pk1,pk2) == 0);

  /* Try with hybrid encryption wrappers. */
  crypto_rand(data1, 1024);
  for (i = 0; i < 3; ++i) {
    for (j = 85; j < 140; ++j) {
      memset(data2,0,1024);
      memset(data3,0,1024);
      if (i == 0 && j < 129)
        continue;
      p = (i==0)?PK_NO_PADDING:
        (i==1)?PK_PKCS1_PADDING:PK_PKCS1_OAEP_PADDING;
      len = crypto_pk_public_hybrid_encrypt(pk1,data2,data1,j,p,0);
      test_assert(len>=0);
      len = crypto_pk_private_hybrid_decrypt(pk1,data3,data2,len,p,1);
      test_eq(len,j);
      test_memeq(data1,data3,j);
    }
  }
  crypto_free_pk_env(pk1);
  crypto_free_pk_env(pk2);

  /* Base64 tests */
  memset(data1, 6, 1024);
  for (idx = 0; idx < 10; ++idx) {
    i = base64_encode(data2, 1024, data1, idx);
    j = base64_decode(data3, 1024, data2, i);
    test_eq(j,idx);
    test_memeq(data3, data1, idx);
  }

  strlcpy(data1, "Test string that contains 35 chars.", 1024);
  strlcat(data1, " 2nd string that contains 35 chars.", 1024);

  i = base64_encode(data2, 1024, data1, 71);
  j = base64_decode(data3, 1024, data2, i);
  test_eq(j, 71);
  test_streq(data3, data1);
  test_assert(data2[i] == '\0');

  crypto_rand(data1, DIGEST_LEN);
  memset(data2, 100, 1024);
  digest_to_base64(data2, data1);
  test_eq(BASE64_DIGEST_LEN, strlen(data2));
  test_eq(100, data2[BASE64_DIGEST_LEN+2]);
  memset(data3, 99, 1024);
  test_eq(digest_from_base64(data3, data2), 0);
  test_memeq(data1, data3, DIGEST_LEN);
  test_eq(99, data3[DIGEST_LEN+1]);

  /* Base32 tests */
  strlcpy(data1, "5chrs", 1024);
  /* bit pattern is:  [35 63 68 72 73] ->
   *        [00110101 01100011 01101000 01110010 01110011]
   * By 5s: [00110 10101 10001 10110 10000 11100 10011 10011]
   */
  base32_encode(data2, 9, data1, 5);
  test_streq(data2, "gvrwq4tt");

  strlcpy(data1, "\xFF\xF5\x6D\x44\xAE\x0D\x5C\xC9\x62\xC4", 1024);
  base32_encode(data2, 30, data1, 10);
  test_streq(data2, "772w2rfobvomsywe");

  /* Base16 tests */
  strlcpy(data1, "6chrs\xff", 1024);
  base16_encode(data2, 13, data1, 6);
  test_streq(data2, "3663687273FF");

  strlcpy(data1, "f0d678affc000100", 1024);
  i = base16_decode(data2, 8, data1, 16);
  test_eq(i,0);
  test_memeq(data2, "\xf0\xd6\x78\xaf\xfc\x00\x01\x00",8);

  /* now try some failing base16 decodes */
  test_eq(-1, base16_decode(data2, 8, data1, 15)); /* odd input len */
  test_eq(-1, base16_decode(data2, 7, data1, 16)); /* dest too short */
  strlcpy(data1, "f0dz!8affc000100", 1024);
  test_eq(-1, base16_decode(data2, 8, data1, 16));

  tor_free(data1);
  tor_free(data2);
  tor_free(data3);
}

static void
test_crypto_s2k(void)
{
  char buf[29];
  char buf2[29];
  char *buf3;
  int i;

  memset(buf, 0, sizeof(buf));
  memset(buf2, 0, sizeof(buf2));
  buf3 = tor_malloc(65536);
  memset(buf3, 0, 65536);

  secret_to_key(buf+9, 20, "", 0, buf);
  crypto_digest(buf2+9, buf3, 1024);
  test_memeq(buf, buf2, 29);

  memcpy(buf,"vrbacrda",8);
  memcpy(buf2,"vrbacrda",8);
  buf[8] = 96;
  buf2[8] = 96;
  secret_to_key(buf+9, 20, "12345678", 8, buf);
  for (i = 0; i < 65536; i += 16) {
    memcpy(buf3+i, "vrbacrda12345678", 16);
  }
  crypto_digest(buf2+9, buf3, 65536);
  test_memeq(buf, buf2, 29);
}

static int
_compare_strs(const void **a, const void **b)
{
  const char *s1 = *a, *s2 = *b;
  return strcmp(s1, s2);
}

static int
_compare_without_first_ch(const void *a, const void **b)
{
  const char *s1 = a, *s2 = *b;
  return strcasecmp(s1+1, s2);
}

static void
test_util(void)
{
  struct timeval start, end;
  struct tm a_time;
  char timestr[RFC1123_TIME_LEN+1];
  char buf[1024];
  time_t t_res;
  int i;
  uint32_t u32;
  uint16_t u16;
  char *cp, *k, *v;

  start.tv_sec = 5;
  start.tv_usec = 5000;

  end.tv_sec = 5;
  end.tv_usec = 5000;

  test_eq(0L, tv_udiff(&start, &end));

  end.tv_usec = 7000;

  test_assert(tv_cmp(&start, &end)<0);
  test_assert(tv_cmp(&end, &start)>0);
  test_assert(tv_cmp(&end, &end)==0);

  test_eq(2000L, tv_udiff(&start, &end));

  end.tv_sec = 6;

  test_eq(1002000L, tv_udiff(&start, &end));

  end.tv_usec = 0;

  test_eq(995000L, tv_udiff(&start, &end));

  end.tv_sec = 4;

  test_eq(-1005000L, tv_udiff(&start, &end));

  tv_addms(&end, 5090);
  test_eq(end.tv_sec, 9);
  test_eq(end.tv_usec, 90000);

  end.tv_usec = 999990;
  start.tv_sec = 1;
  start.tv_usec = 500;
  tv_add(&start, &end);
  test_eq(start.tv_sec, 11);
  test_eq(start.tv_usec, 490);

  /* The test values here are confirmed to be correct on a platform
   * with a working timegm. */
  a_time.tm_year = 2003-1900;
  a_time.tm_mon = 7;
  a_time.tm_mday = 30;
  a_time.tm_hour = 6;
  a_time.tm_min = 14;
  a_time.tm_sec = 55;
  test_eq((time_t) 1062224095UL, tor_timegm(&a_time));
  a_time.tm_year = 2004-1900; /* Try a leap year, after feb. */
  test_eq((time_t) 1093846495UL, tor_timegm(&a_time));
  a_time.tm_mon = 1;          /* Try a leap year, in feb. */
  a_time.tm_mday = 10;
  test_eq((time_t) 1076393695UL, tor_timegm(&a_time));

  format_rfc1123_time(timestr, 0);
  test_streq("Thu, 01 Jan 1970 00:00:00 GMT", timestr);
  format_rfc1123_time(timestr, (time_t)1091580502UL);
  test_streq("Wed, 04 Aug 2004 00:48:22 GMT", timestr);

  t_res = 0;
  i = parse_rfc1123_time(timestr, &t_res);
  test_eq(i,0);
  test_eq(t_res, (time_t)1091580502UL);
  test_eq(-1, parse_rfc1123_time("Wed, zz Aug 2004 99-99x99 GMT", &t_res));
  tor_gettimeofday(&start);

  /* Test tor_strstrip() */
  strlcpy(buf, "Testing 1 2 3", sizeof(buf));
  test_eq(0, tor_strstrip(buf, ",!"));
  test_streq(buf, "Testing 1 2 3");
  strlcpy(buf, "!Testing 1 2 3?", sizeof(buf));
  test_eq(5, tor_strstrip(buf, "!? "));
  test_streq(buf, "Testing123");

  /* Test tor_strpartition() */
  test_assert(! tor_strpartition(buf, sizeof(buf), "abcdefghi", "##", 3));
  test_streq(buf, "abc##def##ghi");

  /* Test parse_addr_port */
  cp = NULL; u32 = 3; u16 = 3;
  test_assert(!parse_addr_port(LOG_WARN, "1.2.3.4", &cp, &u32, &u16));
  test_streq(cp, "1.2.3.4");
  test_eq(u32, 0x01020304u);
  test_eq(u16, 0);
  tor_free(cp);
  test_assert(!parse_addr_port(LOG_WARN, "4.3.2.1:99", &cp, &u32, &u16));
  test_streq(cp, "4.3.2.1");
  test_eq(u32, 0x04030201u);
  test_eq(u16, 99);
  tor_free(cp);
  test_assert(!parse_addr_port(LOG_WARN, "nonexistent.address:4040",
                               &cp, NULL, &u16));
  test_streq(cp, "nonexistent.address");
  test_eq(u16, 4040);
  tor_free(cp);
  test_assert(!parse_addr_port(LOG_WARN, "localhost:9999", &cp, &u32, &u16));
  test_streq(cp, "localhost");
  test_eq(u32, 0x7f000001u);
  test_eq(u16, 9999);
  tor_free(cp);
  u32 = 3;
  test_assert(!parse_addr_port(LOG_WARN, "localhost", NULL, &u32, &u16));
  test_eq(cp, NULL);
  test_eq(u32, 0x7f000001u);
  test_eq(u16, 0);
  tor_free(cp);
  test_eq(0, addr_mask_get_bits(0x0u));
  test_eq(32, addr_mask_get_bits(0xFFFFFFFFu));
  test_eq(16, addr_mask_get_bits(0xFFFF0000u));
  test_eq(31, addr_mask_get_bits(0xFFFFFFFEu));
  test_eq(1, addr_mask_get_bits(0x80000000u));

  /* Test tor_parse_long. */
  test_eq(10L, tor_parse_long("10",10,0,100,NULL,NULL));
  test_eq(0L, tor_parse_long("10",10,50,100,NULL,NULL));
  test_eq(-50L, tor_parse_long("-50",10,-100,100,NULL,NULL));

  /* Test tor_parse_ulong */
  test_eq(10UL, tor_parse_ulong("10",10,0,100,NULL,NULL));
  test_eq(0UL, tor_parse_ulong("10",10,50,100,NULL,NULL));

  /* Test tor_parse_uint64. */
  test_assert(U64_LITERAL(10) == tor_parse_uint64("10 x",10,0,100, &i, &cp));
  test_assert(i == 1);
  test_streq(cp, " x");
  test_assert(U64_LITERAL(12345678901) ==
              tor_parse_uint64("12345678901",10,0,UINT64_MAX, &i, &cp));
  test_assert(i == 1);
  test_streq(cp, "");
  test_assert(U64_LITERAL(0) ==
              tor_parse_uint64("12345678901",10,500,INT32_MAX, &i, &cp));
  test_assert(i == 0);

  /* Test printf with uint64 */
  tor_snprintf(buf, sizeof(buf), "x!"U64_FORMAT"!x",
               U64_PRINTF_ARG(U64_LITERAL(12345678901)));
  test_streq(buf, "x!12345678901!x");

  /* Test parse_line_from_str */
  strlcpy(buf, "k v\n" " key    value with spaces   \n" "keykey val\n"
          "k2\n"
          "k3 \n" "\n" "   \n" "#comment\n"
          "k4#a\n" "k5#abc\n" "k6 val #with comment\n", sizeof(buf));
  cp = buf;

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "k");
  test_streq(v, "v");
  test_assert(!strcmpstart(cp, " key    value with"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "key");
  test_streq(v, "value with spaces");
  test_assert(!strcmpstart(cp, "keykey"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "keykey");
  test_streq(v, "val");
  test_assert(!strcmpstart(cp, "k2\n"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "k2");
  test_streq(v, "");
  test_assert(!strcmpstart(cp, "k3 \n"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "k3");
  test_streq(v, "");
  test_assert(!strcmpstart(cp, "\n   \n"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "k4");
  test_streq(v, "");
  test_assert(!strcmpstart(cp, "k5#abc"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "k5");
  test_streq(v, "");
  test_assert(!strcmpstart(cp, "k6"));

  cp = parse_line_from_str(cp, &k, &v);
  test_streq(k, "k6");
  test_streq(v, "val");
  test_streq(cp, "");

  /* Test for strcmpstart and strcmpend. */
  test_assert(strcmpstart("abcdef", "abcdef")==0);
  test_assert(strcmpstart("abcdef", "abc")==0);
  test_assert(strcmpstart("abcdef", "abd")<0);
  test_assert(strcmpstart("abcdef", "abb")>0);
  test_assert(strcmpstart("ab", "abb")<0);

  test_assert(strcmpend("abcdef", "abcdef")==0);
  test_assert(strcmpend("abcdef", "def")==0);
  test_assert(strcmpend("abcdef", "deg")<0);
  test_assert(strcmpend("abcdef", "dee")>0);
  test_assert(strcmpend("ab", "abb")<0);

  test_assert(strcasecmpend("AbcDEF", "abcdef")==0);
  test_assert(strcasecmpend("abcdef", "dEF")==0);
  test_assert(strcasecmpend("abcDEf", "deg")<0);
  test_assert(strcasecmpend("abcdef", "DEE")>0);
  test_assert(strcasecmpend("ab", "abB")<0);

  /* Test mem_is_zero */
  memset(buf,0,128);
  buf[128] = 'x';
  test_assert(tor_digest_is_zero(buf));
  test_assert(tor_mem_is_zero(buf, 10));
  test_assert(tor_mem_is_zero(buf, 20));
  test_assert(tor_mem_is_zero(buf, 128));
  test_assert(!tor_mem_is_zero(buf, 129));
  buf[60] = (char)255;
  test_assert(!tor_mem_is_zero(buf, 128));
  buf[0] = (char)1;
  test_assert(!tor_mem_is_zero(buf, 10));

  /* Test inet_ntop */
  {
    char tmpbuf[TOR_ADDR_BUF_LEN];
    const char *ip = "176.192.208.224";
    struct in_addr in;
    tor_inet_pton(AF_INET, ip, &in);
    tor_inet_ntop(AF_INET, &in, tmpbuf, sizeof(tmpbuf));
    test_streq(tmpbuf, ip);
  }

  /* Test 'escaped' */
  test_streq("\"\"", escaped(""));
  test_streq("\"abcd\"", escaped("abcd"));
  test_streq("\"\\\\\\n\\r\\t\\\"\\'\"", escaped("\\\n\r\t\"\'"));
  test_streq("\"z\\001abc\\277d\"", escaped("z\001abc\277d"));
  test_assert(NULL == escaped(NULL));

  /* Test strndup and memdup */
  {
    const char *s = "abcdefghijklmnopqrstuvwxyz";
    cp = tor_strndup(s, 30);
    test_streq(cp, s); /* same string, */
    test_neq(cp, s); /* but different pointers. */
    tor_free(cp);

    cp = tor_strndup(s, 5);
    test_streq(cp, "abcde");
    tor_free(cp);

    s = "a\0b\0c\0d\0e\0";
    cp = tor_memdup(s,10);
    test_memeq(cp, s, 10); /* same ram, */
    test_neq(cp, s); /* but different pointers. */
    tor_free(cp);
  }

  /* Test str-foo functions */
  cp = tor_strdup("abcdef");
  test_assert(tor_strisnonupper(cp));
  cp[3] = 'D';
  test_assert(!tor_strisnonupper(cp));
  tor_strupper(cp);
  test_streq(cp, "ABCDEF");
  test_assert(tor_strisprint(cp));
  cp[3] = 3;
  test_assert(!tor_strisprint(cp));
  tor_free(cp);

  /* Test eat_whitespace. */
  {
    const char *s = "  \n a";
    test_eq_ptr(eat_whitespace(s), s+4);
    s = "abcd";
    test_eq_ptr(eat_whitespace(s), s);
    s = "#xyz\nab";
    test_eq_ptr(eat_whitespace(s), s+5);
  }

  /* Test memmem */
  {
    const char *haystack = "abcde";
    tor_assert(!tor_memmem(haystack, 5, "ef", 2));
    test_eq_ptr(tor_memmem(haystack, 5, "cd", 2), haystack + 2);
    test_eq_ptr(tor_memmem(haystack, 5, "cde", 3), haystack + 2);
    haystack = "ababcad";
    test_eq_ptr(tor_memmem(haystack, 7, "abc", 3), haystack + 2);
  }

  /* Test wrap_string */
  {
    smartlist_t *sl = smartlist_create();
    wrap_string(sl, "This is a test of string wrapping functionality: woot.",
                10, "", "");
    cp = smartlist_join_strings(sl, "", 0, NULL);
    test_streq(cp,
            "This is a\ntest of\nstring\nwrapping\nfunctional\nity: woot.\n");
    tor_free(cp);
    SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
    smartlist_clear(sl);

    wrap_string(sl, "This is a test of string wrapping functionality: woot.",
                16, "### ", "# ");
    cp = smartlist_join_strings(sl, "", 0, NULL);
    test_streq(cp,
             "### This is a\n# test of string\n# wrapping\n# functionality:\n"
             "# woot.\n");

    tor_free(cp);
    SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
    smartlist_clear(sl);
  }

  /* now make sure time works. */
  tor_gettimeofday(&end);
  /* We might've timewarped a little. */
  test_assert(tv_udiff(&start, &end) >= -5000);

  /* Test tor_log2(). */
  test_eq(tor_log2(64), 6);
  test_eq(tor_log2(65), 6);
  test_eq(tor_log2(63), 5);
  test_eq(tor_log2(1), 0);
  test_eq(tor_log2(2), 1);
  test_eq(tor_log2(3), 1);
  test_eq(tor_log2(4), 2);
  test_eq(tor_log2(5), 2);
  test_eq(tor_log2(U64_LITERAL(40000000000000000)), 55);
  test_eq(tor_log2(UINT64_MAX), 63);

  /* Test round_to_power_of_2 */
  test_eq(round_to_power_of_2(120), 128);
  test_eq(round_to_power_of_2(128), 128);
  test_eq(round_to_power_of_2(130), 128);
  test_eq(round_to_power_of_2(U64_LITERAL(40000000000000000)),
          U64_LITERAL(1)<<55);
  test_eq(round_to_power_of_2(0), 2);
}

static void
_test_eq_ip6(struct in6_addr *a, struct in6_addr *b, const char *e1,
             const char *e2, int line)
{
  int i;
  int ok = 1;
  for (i = 0; i < 16; ++i) {
    if (a->s6_addr[i] != b->s6_addr[i]) {
      ok = 0;
      break;
    }
  }
  if (ok) {
    printf("."); fflush(stdout);
  } else {
    char buf1[128], *cp1;
    char buf2[128], *cp2;
    have_failed = 1;
    cp1 = buf1; cp2 = buf2;
    for (i=0; i<16; ++i) {
      tor_snprintf(cp1, sizeof(buf1)-(cp1-buf1), "%02x", a->s6_addr[i]);
      tor_snprintf(cp2, sizeof(buf2)-(cp2-buf2), "%02x", b->s6_addr[i]);
      cp1 += 2; cp2 += 2;
      if ((i%2)==1 && i != 15) {
        *cp1++ = ':';
        *cp2++ = ':';
      }
    }
    *cp1 = *cp2 = '\0';
    printf("Line %d: assertion failed: (%s == %s)\n"
           "      %s != %s\n", line, e1, e2, buf1, buf2);
    fflush(stdout);
  }
}
#define test_eq_ip6(a,b) _test_eq_ip6((a),(b),#a,#b,__LINE__)

#define test_pton6_same(a,b) STMT_BEGIN          \
    r = tor_inet_pton(AF_INET6, a, &a1);         \
    test_assert(r==1);                           \
    r = tor_inet_pton(AF_INET6, b, &a2);         \
    test_assert(r==1);                           \
    test_eq_ip6(&a1,&a2);                        \
  STMT_END

#define test_pton6_bad(a)                       \
  test_eq(0, tor_inet_pton(AF_INET6, a, &a1))

#define test_ntop6_reduces(a,b) STMT_BEGIN                              \
    r = tor_inet_pton(AF_INET6, a, &a1);                                \
    test_assert(r==1);                                                  \
    test_streq(tor_inet_ntop(AF_INET6, &a1, buf, sizeof(buf)), b);      \
    r = tor_inet_pton(AF_INET6, b, &a2);                                \
    test_assert(r==1);                                                  \
    test_eq_ip6(&a1, &a2);                                              \
  STMT_END

/*XXXX020 make this macro give useful output on failure, and follow the
 * conventions of the other test macros.  */
#define test_internal_ip(a,b) STMT_BEGIN             \
    r = tor_inet_pton(AF_INET6, a, &t1.sa6.sin6_addr); \
    test_assert(r==1);                               \
    t1.sa6.sin6_family = AF_INET6;                   \
    r = tor_addr_is_internal(&t1, b);                \
    test_assert(r==1);                               \
  STMT_END

/*XXXX020 make this macro give useful output on failure, and follow the
 * conventions of the other test macros.  */
#define test_external_ip(a,b) STMT_BEGIN             \
    r = tor_inet_pton(AF_INET6, a, &t1.sa6.sin6_addr); \
    test_assert(r==1);                               \
    t1.sa6.sin6_family = AF_INET6;                   \
    r = tor_addr_is_internal(&t1, b);                \
    test_assert(r==0);                               \
  STMT_END

/*XXXX020 make this macro give useful output on failure, and follow the
 * conventions of the other test macros.  */
#define test_addr_convert6(a,b) STMT_BEGIN           \
    tor_inet_pton(AF_INET6, a, &t1.sa6.sin6_addr);   \
    tor_inet_pton(AF_INET6, b, &t2.sa6.sin6_addr);   \
    t1.sa6.sin6_family = AF_INET6;                   \
    t2.sa6.sin6_family = AF_INET6;                   \
  STMT_END

/*XXXX020 make this macro give useful output on failure, and follow the
 * conventions of the other test macros. */
#define test_addr_parse(xx) STMT_BEGIN                                \
    r=tor_addr_parse_mask_ports(xx, &t1, &mask, &port1, &port2);      \
    t2.sa6.sin6_family = AF_INET6;                                    \
    p1=tor_inet_ntop(AF_INET6, &t1.sa6.sin6_addr, bug, sizeof(bug));  \
  STMT_END

/*XXXX020 make this macro give useful output on failure, and follow the
 * conventions of the other test macros. */
#define test_addr_parse_check(ip1, ip2, ip3, ip4, mm, pt1, pt2) STMT_BEGIN  \
    test_assert(r>=0);                                     \
    test_eq(htonl(ip1), IN6_ADDRESS32(&t1)[0]);            \
    test_eq(htonl(ip2), IN6_ADDRESS32(&t1)[1]);            \
    test_eq(htonl(ip3), IN6_ADDRESS32(&t1)[2]);            \
    test_eq(htonl(ip4), IN6_ADDRESS32(&t1)[3]);            \
    test_eq(mask, mm);                                     \
    test_eq(port1, pt1);                                   \
    test_eq(port2, pt2);                                   \
  STMT_END

static void
test_util_ip6_helpers(void)
{
  char buf[TOR_ADDR_BUF_LEN], bug[TOR_ADDR_BUF_LEN];
  struct in6_addr a1, a2;
  tor_addr_t t1, t2;
  int r, i;
  uint16_t port1, port2;
  maskbits_t mask;
  const char *p1;

  //  struct in_addr b1, b2;
  /* Test tor_inet_ntop and tor_inet_pton: IPv6 */

  /* === Test pton: valid af_inet6 */
  /* Simple, valid parsing. */
  r = tor_inet_pton(AF_INET6,
                    "0102:0304:0506:0708:090A:0B0C:0D0E:0F10", &a1);
  test_assert(r==1);
  for (i=0;i<16;++i) { test_eq(i+1, (int)a1.s6_addr[i]); }
  /* ipv4 ending. */
  test_pton6_same("0102:0304:0506:0708:090A:0B0C:0D0E:0F10",
                  "0102:0304:0506:0708:090A:0B0C:13.14.15.16");
  /* shortened words. */
  test_pton6_same("0001:0099:BEEF:0000:0123:FFFF:0001:0001",
                  "1:99:BEEF:0:0123:FFFF:1:1");
  /* zeros at the beginning */
  test_pton6_same("0000:0000:0000:0000:0009:C0A8:0001:0001",
                  "::9:c0a8:1:1");
  test_pton6_same("0000:0000:0000:0000:0009:C0A8:0001:0001",
                  "::9:c0a8:0.1.0.1");
  /* zeros in the middle. */
  test_pton6_same("fe80:0000:0000:0000:0202:1111:0001:0001",
                  "fe80::202:1111:1:1");
  /* zeros at the end. */
  test_pton6_same("1000:0001:0000:0007:0000:0000:0000:0000",
                  "1000:1:0:7::");

  /* === Test ntop: af_inet6 */
  test_ntop6_reduces("0:0:0:0:0:0:0:0", "::");

  test_ntop6_reduces("0001:0099:BEEF:0006:0123:FFFF:0001:0001",
                     "1:99:beef:6:123:ffff:1:1");

  //test_ntop6_reduces("0:0:0:0:0:0:c0a8:0101", "::192.168.1.1");
  test_ntop6_reduces("0:0:0:0:0:ffff:c0a8:0101", "::ffff:192.168.1.1");
  test_ntop6_reduces("002:0:0000:0:3::4", "2::3:0:0:4");
  test_ntop6_reduces("0:0::1:0:3", "::1:0:3");
  test_ntop6_reduces("008:0::0", "8::");
  test_ntop6_reduces("0:0:0:0:0:ffff::1", "::ffff:0.0.0.1");
  test_ntop6_reduces("abcd:0:0:0:0:0:7f00::", "abcd::7f00:0");
  test_ntop6_reduces("0000:0000:0000:0000:0009:C0A8:0001:0001",
                     "::9:c0a8:1:1");
  test_ntop6_reduces("fe80:0000:0000:0000:0202:1111:0001:0001",
                     "fe80::202:1111:1:1");
  test_ntop6_reduces("1000:0001:0000:0007:0000:0000:0000:0000",
                     "1000:1:0:7::");

  /* === Test pton: invalid in6. */
  test_pton6_bad("foobar.");
  test_pton6_bad("55555::");
  test_pton6_bad("9:-60::");
  test_pton6_bad("1:2:33333:4:0002:3::");
  //test_pton6_bad("1:2:3333:4:00002:3::");// BAD, but glibc doesn't say so.
  test_pton6_bad("1:2:3333:4:fish:3::");
  test_pton6_bad("1:2:3:4:5:6:7:8:9");
  test_pton6_bad("1:2:3:4:5:6:7");
  test_pton6_bad("1:2:3:4:5:6:1.2.3.4.5");
  test_pton6_bad("1:2:3:4:5:6:1.2.3");
  test_pton6_bad("::1.2.3");
  test_pton6_bad("::1.2.3.4.5");
  test_pton6_bad("99");
  test_pton6_bad("");
  test_pton6_bad("1::2::3:4");
  test_pton6_bad("a:::b:c");
  test_pton6_bad(":::a:b:c");
  test_pton6_bad("a:b:c:::");

  /* test internal checking */
  test_external_ip("fbff:ffff::2:7", 0);
  test_internal_ip("fc01::2:7", 0);
  test_internal_ip("fdff:ffff::f:f", 0);
  test_external_ip("fe00::3:f", 0);

  test_external_ip("fe7f:ffff::2:7", 0);
  test_internal_ip("fe80::2:7", 0);
  test_internal_ip("febf:ffff::f:f", 0);

  test_internal_ip("fec0::2:7:7", 0);
  test_internal_ip("feff:ffff::e:7:7", 0);
  test_external_ip("ff00::e:7:7", 0);

  test_internal_ip("::", 0);
  test_internal_ip("::1", 0);
  test_internal_ip("::1", 1);
  test_internal_ip("::", 0);
  test_external_ip("::", 1);
  test_external_ip("::2", 0);
  test_external_ip("2001::", 0);
  test_external_ip("ffff::", 0);

  test_external_ip("::ffff:0.0.0.0", 1);
  test_internal_ip("::ffff:0.0.0.0", 0);
  test_internal_ip("::ffff:0.255.255.255", 0);
  test_external_ip("::ffff:1.0.0.0", 0);

  test_external_ip("::ffff:9.255.255.255", 0);
  test_internal_ip("::ffff:10.0.0.0", 0);
  test_internal_ip("::ffff:10.255.255.255", 0);
  test_external_ip("::ffff:11.0.0.0", 0);

  test_external_ip("::ffff:126.255.255.255", 0);
  test_internal_ip("::ffff:127.0.0.0", 0);
  test_internal_ip("::ffff:127.255.255.255", 0);
  test_external_ip("::ffff:128.0.0.0", 0);

  test_external_ip("::ffff:172.15.255.255", 0);
  test_internal_ip("::ffff:172.16.0.0", 0);
  test_internal_ip("::ffff:172.31.255.255", 0);
  test_external_ip("::ffff:172.32.0.0", 0);

  test_external_ip("::ffff:192.167.255.255", 0);
  test_internal_ip("::ffff:192.168.0.0", 0);
  test_internal_ip("::ffff:192.168.255.255", 0);
  test_external_ip("::ffff:192.169.0.0", 0);

  test_external_ip("::ffff:169.253.255.255", 0);
  test_internal_ip("::ffff:169.254.0.0", 0);
  test_internal_ip("::ffff:169.254.255.255", 0);
  test_external_ip("::ffff:169.255.0.0", 0);

  /* tor_addr_compare(tor_addr_t x2) */
  test_addr_convert6("ffff::", "ffff::0");
  test_assert(tor_addr_compare(&t1, &t2) == 0);
  test_addr_convert6("0::3:2:1", "0::ffff:0.3.2.1");
  test_assert(tor_addr_compare(&t1, &t2) > 0);
  test_addr_convert6("0::2:2:1", "0::ffff:0.3.2.1");
  test_assert(tor_addr_compare(&t1, &t2) > 0);
  test_addr_convert6("0::ffff:0.3.2.1", "0::0:0:0");
  test_assert(tor_addr_compare(&t1, &t2) < 0);
  test_addr_convert6("0::ffff:5.2.2.1", "::ffff:6.0.0.0");
  test_assert(tor_addr_compare(&t1, &t2) < 0); /* XXXX wrong. */
  tor_addr_parse_mask_ports("[::ffff:2.3.4.5]", &t1, NULL, NULL, NULL);
  tor_addr_parse_mask_ports("2.3.4.5", &t2, NULL, NULL, NULL);
  test_assert(tor_addr_compare(&t1, &t2) == 0);
  tor_addr_parse_mask_ports("[::ffff:2.3.4.4]", &t1, NULL, NULL, NULL);
  tor_addr_parse_mask_ports("2.3.4.5", &t2, NULL, NULL, NULL);
  test_assert(tor_addr_compare(&t1, &t2) < 0);

  /* XXXX020 test compare_masked */

  /* test tor_addr_parse_mask_ports */
  test_addr_parse("[::f]/17:47-95");
  test_addr_parse_check(0, 0, 0, 0x0000000f, 17, 47, 95);
  //test_addr_parse("[::fefe:4.1.1.7/120]:999-1000");
  //test_addr_parse_check("::fefe:401:107", 120, 999, 1000);
  test_addr_parse("[::ffff:4.1.1.7]/120:443");
  test_addr_parse_check(0, 0, 0x0000ffff, 0x04010107, 120, 443, 443);
  test_addr_parse("[abcd:2::44a:0]:2-65000");
  test_addr_parse_check(0xabcd0002, 0, 0, 0x044a0000, 128, 2, 65000);

  r=tor_addr_parse_mask_ports("[fefef::]/112", &t1, NULL, NULL, NULL);
  test_assert(r == -1);
  r=tor_addr_parse_mask_ports("efef::/112", &t1, NULL, NULL, NULL);
  test_assert(r == -1);
  r=tor_addr_parse_mask_ports("[f:f:f:f:f:f:f:f::]", &t1, NULL, NULL, NULL);
  test_assert(r == -1);
  r=tor_addr_parse_mask_ports("[::f:f:f:f:f:f:f:f]", &t1, NULL, NULL, NULL);
  test_assert(r == -1);
  r=tor_addr_parse_mask_ports("[f:f:f:f:f:f:f:f:f]", &t1, NULL, NULL, NULL);
  test_assert(r == -1);
  /* Test for V4-mapped address with mask < 96.  (arguably not valid) */
  r=tor_addr_parse_mask_ports("[::ffff:1.1.2.2/33]", &t1, &mask, NULL, NULL);
  test_assert(r == -1);
  r=tor_addr_parse_mask_ports("1.1.2.2/33", &t1, &mask, NULL, NULL);
  test_assert(r == -1);
  r=tor_addr_parse_mask_ports("1.1.2.2/31", &t1, &mask, NULL, NULL);
  test_assert(r == AF_INET);
  r=tor_addr_parse_mask_ports("[efef::]/112", &t1, &mask, &port1, &port2);
  test_assert(r == AF_INET6);
  test_assert(port1 == 1);
  test_assert(port2 == 65535);

  /* make sure inet address lengths >= max */
  test_assert(INET_NTOA_BUF_LEN >= sizeof("255.255.255.255"));
  test_assert(TOR_ADDR_BUF_LEN >=
              sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"));

  test_assert(sizeof(tor_addr_t) >= sizeof(struct sockaddr_in6));

  /* get interface addresses */
  r = get_interface_address6(0, AF_INET, &t1);
  i = get_interface_address6(0, AF_INET6, &t2);
#if 0
  tor_inet_ntop(AF_INET, &t1.sa.sin_addr, buf, sizeof(buf));
  printf("\nv4 address: %s  (family=%i)", buf, IN_FAMILY(&t1));
  tor_inet_ntop(AF_INET6, &t2.sa6.sin6_addr, buf, sizeof(buf));
  printf("\nv6 address: %s  (family=%i)", buf, IN_FAMILY(&t2));
#endif
}

static void
test_util_smartlist(void)
{
  smartlist_t *sl;
  char *cp;

  /* XXXX test sort_digests, uniq_strings, uniq_digests */

  /* Test smartlist add, del_keeporder, insert, get. */
  sl = smartlist_create();
  smartlist_add(sl, (void*)1);
  smartlist_add(sl, (void*)2);
  smartlist_add(sl, (void*)3);
  smartlist_add(sl, (void*)4);
  smartlist_del_keeporder(sl, 1);
  smartlist_insert(sl, 1, (void*)22);
  smartlist_insert(sl, 0, (void*)0);
  smartlist_insert(sl, 5, (void*)555);
  test_eq_ptr((void*)0,   smartlist_get(sl,0));
  test_eq_ptr((void*)1,   smartlist_get(sl,1));
  test_eq_ptr((void*)22,  smartlist_get(sl,2));
  test_eq_ptr((void*)3,   smartlist_get(sl,3));
  test_eq_ptr((void*)4,   smartlist_get(sl,4));
  test_eq_ptr((void*)555, smartlist_get(sl,5));
  /* Try deleting in the middle. */
  smartlist_del(sl, 1);
  test_eq_ptr((void*)555, smartlist_get(sl, 1));
  /* Try deleting at the end. */
  smartlist_del(sl, 4);
  test_eq(4, smartlist_len(sl));

  /* test isin. */
  test_assert(smartlist_isin(sl, (void*)3));
  test_assert(!smartlist_isin(sl, (void*)99));

  /* Test split and join */
  smartlist_clear(sl);
  test_eq(0, smartlist_len(sl));
  smartlist_split_string(sl, "abc", ":", 0, 0);
  test_eq(1, smartlist_len(sl));
  test_streq("abc", smartlist_get(sl, 0));
  smartlist_split_string(sl, "a::bc::", "::", 0, 0);
  test_eq(4, smartlist_len(sl));
  test_streq("a", smartlist_get(sl, 1));
  test_streq("bc", smartlist_get(sl, 2));
  test_streq("", smartlist_get(sl, 3));
  cp = smartlist_join_strings(sl, "", 0, NULL);
  test_streq(cp, "abcabc");
  tor_free(cp);
  cp = smartlist_join_strings(sl, "!", 0, NULL);
  test_streq(cp, "abc!a!bc!");
  tor_free(cp);
  cp = smartlist_join_strings(sl, "XY", 0, NULL);
  test_streq(cp, "abcXYaXYbcXY");
  tor_free(cp);
  cp = smartlist_join_strings(sl, "XY", 1, NULL);
  test_streq(cp, "abcXYaXYbcXYXY");
  tor_free(cp);
  cp = smartlist_join_strings(sl, "", 1, NULL);
  test_streq(cp, "abcabc");
  tor_free(cp);

  smartlist_split_string(sl, "/def/  /ghijk", "/", 0, 0);
  test_eq(8, smartlist_len(sl));
  test_streq("", smartlist_get(sl, 4));
  test_streq("def", smartlist_get(sl, 5));
  test_streq("  ", smartlist_get(sl, 6));
  test_streq("ghijk", smartlist_get(sl, 7));
  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  smartlist_split_string(sl, "a,bbd,cdef", ",", SPLIT_SKIP_SPACE, 0);
  test_eq(3, smartlist_len(sl));
  test_streq("a", smartlist_get(sl,0));
  test_streq("bbd", smartlist_get(sl,1));
  test_streq("cdef", smartlist_get(sl,2));
  smartlist_split_string(sl, " z <> zhasd <>  <> bnud<>   ", "<>",
                         SPLIT_SKIP_SPACE, 0);
  test_eq(8, smartlist_len(sl));
  test_streq("z", smartlist_get(sl,3));
  test_streq("zhasd", smartlist_get(sl,4));
  test_streq("", smartlist_get(sl,5));
  test_streq("bnud", smartlist_get(sl,6));
  test_streq("", smartlist_get(sl,7));

  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  smartlist_split_string(sl, " ab\tc \td ef  ", NULL,
                         SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 0);
  test_eq(4, smartlist_len(sl));
  test_streq("ab", smartlist_get(sl,0));
  test_streq("c", smartlist_get(sl,1));
  test_streq("d", smartlist_get(sl,2));
  test_streq("ef", smartlist_get(sl,3));
  smartlist_split_string(sl, "ghi\tj", NULL,
                         SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 0);
  test_eq(6, smartlist_len(sl));
  test_streq("ghi", smartlist_get(sl,4));
  test_streq("j", smartlist_get(sl,5));

  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  cp = smartlist_join_strings(sl, "XY", 0, NULL);
  test_streq(cp, "");
  tor_free(cp);
  cp = smartlist_join_strings(sl, "XY", 1, NULL);
  test_streq(cp, "XY");
  tor_free(cp);

  smartlist_split_string(sl, " z <> zhasd <>  <> bnud<>   ", "<>",
                         SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 0);
  test_eq(3, smartlist_len(sl));
  test_streq("z", smartlist_get(sl, 0));
  test_streq("zhasd", smartlist_get(sl, 1));
  test_streq("bnud", smartlist_get(sl, 2));
  smartlist_split_string(sl, " z <> zhasd <>  <> bnud<>   ", "<>",
                         SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 2);
  test_eq(5, smartlist_len(sl));
  test_streq("z", smartlist_get(sl, 3));
  test_streq("zhasd <>  <> bnud<>", smartlist_get(sl, 4));
  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  smartlist_split_string(sl, "abcd\n", "\n",
                         SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 0);
  test_eq(1, smartlist_len(sl));
  test_streq("abcd", smartlist_get(sl, 0));
  smartlist_split_string(sl, "efgh", "\n",
                         SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 0);
  test_eq(2, smartlist_len(sl));
  test_streq("efgh", smartlist_get(sl, 1));

  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  /* Test swapping, shuffling, and sorting. */
  smartlist_split_string(sl, "the,onion,router,by,arma,and,nickm", ",", 0, 0);
  test_eq(7, smartlist_len(sl));
  smartlist_sort(sl, _compare_strs);
  cp = smartlist_join_strings(sl, ",", 0, NULL);
  test_streq(cp,"and,arma,by,nickm,onion,router,the");
  tor_free(cp);
  smartlist_swap(sl, 1, 5);
  cp = smartlist_join_strings(sl, ",", 0, NULL);
  test_streq(cp,"and,router,by,nickm,onion,arma,the");
  tor_free(cp);
  smartlist_shuffle(sl);
  test_eq(7, smartlist_len(sl));
  test_assert(smartlist_string_isin(sl, "and"));
  test_assert(smartlist_string_isin(sl, "router"));
  test_assert(smartlist_string_isin(sl, "by"));
  test_assert(smartlist_string_isin(sl, "nickm"));
  test_assert(smartlist_string_isin(sl, "onion"));
  test_assert(smartlist_string_isin(sl, "arma"));
  test_assert(smartlist_string_isin(sl, "the"));

  /* Test bsearch. */
  smartlist_sort(sl, _compare_strs);
  test_streq("nickm", smartlist_bsearch(sl, "zNicKM",
                                        _compare_without_first_ch));
  test_streq("and", smartlist_bsearch(sl, " AND", _compare_without_first_ch));
  test_eq_ptr(NULL, smartlist_bsearch(sl, " ANz", _compare_without_first_ch));

  /* Test reverse() and pop_last() */
  smartlist_reverse(sl);
  cp = smartlist_join_strings(sl, ",", 0, NULL);
  test_streq(cp,"the,router,onion,nickm,by,arma,and");
  tor_free(cp);
  cp = smartlist_pop_last(sl);
  test_streq(cp, "and");
  tor_free(cp);
  test_eq(smartlist_len(sl), 6);
  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  /* Test uniq() */
  smartlist_split_string(sl,
                     "50,noon,radar,a,man,a,plan,a,canal,panama,radar,noon,50",
                     ",", 0, 0);
  smartlist_sort(sl, _compare_strs);
  smartlist_uniq(sl, _compare_strs, _tor_free);
  cp = smartlist_join_strings(sl, ",", 0, NULL);
  test_streq(cp, "50,a,canal,man,noon,panama,plan,radar");
  tor_free(cp);

  /* Test string_isin and isin_case and num_isin */
  test_assert(smartlist_string_isin(sl, "noon"));
  test_assert(!smartlist_string_isin(sl, "noonoon"));
  test_assert(smartlist_string_isin_case(sl, "nOOn"));
  test_assert(!smartlist_string_isin_case(sl, "nooNooN"));
  test_assert(smartlist_string_num_isin(sl, 50));
  test_assert(!smartlist_string_num_isin(sl, 60));
  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  /* Test string_remove and remove and join_strings2 */
  smartlist_split_string(sl,
                    "Some say the Earth will end in ice and some in fire",
                    " ", 0, 0);
  cp = smartlist_get(sl, 4);
  test_streq(cp, "will");
  smartlist_add(sl, cp);
  smartlist_remove(sl, cp);
  cp = smartlist_join_strings(sl, ",", 0, NULL);
  test_streq(cp, "Some,say,the,Earth,fire,end,in,ice,and,some,in");
  tor_free(cp);
  smartlist_string_remove(sl, "in");
  cp = smartlist_join_strings2(sl, "+XX", 1, 0, NULL);
  test_streq(cp, "Some+say+the+Earth+fire+end+some+ice+and");
  tor_free(cp);

  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  smartlist_clear(sl);

  {
    smartlist_t *ints = smartlist_create();
    smartlist_t *odds = smartlist_create();
    smartlist_t *evens = smartlist_create();
    smartlist_t *primes = smartlist_create();
    int i;
    for (i=1; i < 10; i += 2)
      smartlist_add(odds, (void*)(uintptr_t)i);
    for (i=0; i < 10; i += 2)
      smartlist_add(evens, (void*)(uintptr_t)i);

    /* add_all */
    smartlist_add_all(ints, odds);
    smartlist_add_all(ints, evens);
    test_eq(smartlist_len(ints), 10);

    smartlist_add(primes, (void*)2);
    smartlist_add(primes, (void*)3);
    smartlist_add(primes, (void*)5);
    smartlist_add(primes, (void*)7);

    /* overlap */
    test_assert(smartlist_overlap(ints, odds));
    test_assert(smartlist_overlap(odds, primes));
    test_assert(smartlist_overlap(evens, primes));
    test_assert(!smartlist_overlap(odds, evens));

    /* intersect */
    smartlist_add_all(sl, odds);
    smartlist_intersect(sl, primes);
    test_eq(smartlist_len(sl), 3);
    test_assert(smartlist_isin(sl, (void*)3));
    test_assert(smartlist_isin(sl, (void*)5));
    test_assert(smartlist_isin(sl, (void*)7));

    /* subtract */
    smartlist_add_all(sl, primes);
    smartlist_subtract(sl, odds);
    test_eq(smartlist_len(sl), 1);
    test_assert(smartlist_isin(sl, (void*)2));

    smartlist_free(odds);
    smartlist_free(evens);
    smartlist_free(ints);
    smartlist_free(primes);
    smartlist_clear(sl);
  }

  smartlist_free(sl);
}

static void
test_util_bitarray(void)
{
  bitarray_t *ba;
  int i, j, ok=1;

  ba = bitarray_init_zero(1);
  test_assert(! bitarray_is_set(ba, 0));
  bitarray_set(ba, 0);
  test_assert(bitarray_is_set(ba, 0));
  bitarray_clear(ba, 0);
  test_assert(! bitarray_is_set(ba, 0));
  bitarray_free(ba);

  ba = bitarray_init_zero(1023);
  for (i = 1; i < 64; ) {
    for (j = 0; j < 1023; ++j) {
      if (j % i)
        bitarray_set(ba, j);
      else
        bitarray_clear(ba, j);
    }
    for (j = 0; j < 1023; ++j) {
      if (!bool_eq(bitarray_is_set(ba, j), j%i))
        ok = 0;
    }
    test_assert(ok);
    if (i < 7)
      ++i;
    else if (i == 28)
      i = 32;
    else
      i += 7;
  }
  bitarray_free(ba);
}

/* stop threads running at once. */
static tor_mutex_t *_thread_test_mutex = NULL;
/* make sure that threads have to run at the same time. */
static tor_mutex_t *_thread_test_start1 = NULL;
static tor_mutex_t *_thread_test_start2 = NULL;
static strmap_t *_thread_test_strmap = NULL;

static void _thread_test_func(void* _s) ATTR_NORETURN;

static int t1_count = 0;
static int t2_count = 0;

static void
_thread_test_func(void* _s)
{
  char *s = _s;
  int i, *count;
  tor_mutex_t *m;
  char buf[64];
  char *cp;
  if (!strcmp(s, "thread 1")) {
    m = _thread_test_start1;
    count = &t1_count;
  } else {
    m = _thread_test_start2;
    count = &t2_count;
  }
  tor_mutex_acquire(m);

  tor_snprintf(buf, sizeof(buf), "%lu", tor_get_thread_id());
  cp = tor_strdup(buf);

  for (i=0; i<10000; ++i) {
    tor_mutex_acquire(_thread_test_mutex);
    strmap_set(_thread_test_strmap, "last to run", cp);
    ++*count;
    tor_mutex_release(_thread_test_mutex);
  }
  tor_mutex_acquire(_thread_test_mutex);
  strmap_set(_thread_test_strmap, s, tor_strdup(buf));
  tor_mutex_release(_thread_test_mutex);

  tor_mutex_release(m);

  spawn_exit();
}

static void
test_util_threads(void)
{
  char *s1, *s2;
  int done = 0, timedout = 0;
  time_t started;
#ifndef TOR_IS_MULTITHREADED
  /* Skip this test if we aren't threading. We should be threading most
   * everywhere by now. */
  if (1)
    return;
#endif
  _thread_test_mutex = tor_mutex_new();
  _thread_test_start1 = tor_mutex_new();
  _thread_test_start2 = tor_mutex_new();
  _thread_test_strmap = strmap_new();
  s1 = tor_strdup("thread 1");
  s2 = tor_strdup("thread 2");
  tor_mutex_acquire(_thread_test_start1);
  tor_mutex_acquire(_thread_test_start2);
  spawn_func(_thread_test_func, s1);
  spawn_func(_thread_test_func, s2);
  tor_mutex_release(_thread_test_start2);
  tor_mutex_release(_thread_test_start1);
  started = time(NULL);
  while (!done) {
    tor_mutex_acquire(_thread_test_mutex);
    strmap_assert_ok(_thread_test_strmap);
    if (strmap_get(_thread_test_strmap, "thread 1") &&
        strmap_get(_thread_test_strmap, "thread 2")) {
      done = 1;
    } else if (time(NULL) > started + 25) {
      timedout = done = 1;
    }
    tor_mutex_release(_thread_test_mutex);
  }
  tor_mutex_free(_thread_test_mutex);

  if (timedout) {
    printf("\nTimed out: %d %d", t1_count, t2_count);
    test_assert(strmap_get(_thread_test_strmap, "thread 1"));
    test_assert(strmap_get(_thread_test_strmap, "thread 2"));
    test_assert(!timedout);
  }

  /* different thread IDs. */
  test_assert(strcmp(strmap_get(_thread_test_strmap, "thread 1"),
                     strmap_get(_thread_test_strmap, "thread 2")));
  test_assert(!strcmp(strmap_get(_thread_test_strmap, "thread 1"),
                      strmap_get(_thread_test_strmap, "last to run")) ||
              !strcmp(strmap_get(_thread_test_strmap, "thread 2"),
                      strmap_get(_thread_test_strmap, "last to run")));

  strmap_free(_thread_test_strmap, _tor_free);

  tor_free(s1);
  tor_free(s2);
}

static int
_compare_strings_for_pqueue(const void *s1, const void *s2)
{
  return strcmp((const char*)s1, (const char*)s2);
}

static void
test_util_pqueue(void)
{
  smartlist_t *sl;
  int (*cmp)(const void *, const void*);
#define OK() smartlist_pqueue_assert_ok(sl, cmp)

  cmp = _compare_strings_for_pqueue;

  sl = smartlist_create();
  smartlist_pqueue_add(sl, cmp, (char*)"cows");
  smartlist_pqueue_add(sl, cmp, (char*)"zebras");
  smartlist_pqueue_add(sl, cmp, (char*)"fish");
  smartlist_pqueue_add(sl, cmp, (char*)"frogs");
  smartlist_pqueue_add(sl, cmp, (char*)"apples");
  smartlist_pqueue_add(sl, cmp, (char*)"squid");
  smartlist_pqueue_add(sl, cmp, (char*)"daschunds");
  smartlist_pqueue_add(sl, cmp, (char*)"eggplants");
  smartlist_pqueue_add(sl, cmp, (char*)"weissbier");
  smartlist_pqueue_add(sl, cmp, (char*)"lobsters");
  smartlist_pqueue_add(sl, cmp, (char*)"roquefort");

  OK();

  test_eq(smartlist_len(sl), 11);
  test_streq(smartlist_get(sl, 0), "apples");
  test_streq(smartlist_pqueue_pop(sl, cmp), "apples");
  test_eq(smartlist_len(sl), 10);
  OK();
  test_streq(smartlist_pqueue_pop(sl, cmp), "cows");
  test_streq(smartlist_pqueue_pop(sl, cmp), "daschunds");
  smartlist_pqueue_add(sl, cmp, (char*)"chinchillas");
  OK();
  smartlist_pqueue_add(sl, cmp, (char*)"fireflies");
  OK();
  test_streq(smartlist_pqueue_pop(sl, cmp), "chinchillas");
  test_streq(smartlist_pqueue_pop(sl, cmp), "eggplants");
  test_streq(smartlist_pqueue_pop(sl, cmp), "fireflies");
  OK();
  test_streq(smartlist_pqueue_pop(sl, cmp), "fish");
  test_streq(smartlist_pqueue_pop(sl, cmp), "frogs");
  test_streq(smartlist_pqueue_pop(sl, cmp), "lobsters");
  test_streq(smartlist_pqueue_pop(sl, cmp), "roquefort");
  OK();
  test_eq(smartlist_len(sl), 3);
  test_streq(smartlist_pqueue_pop(sl, cmp), "squid");
  test_streq(smartlist_pqueue_pop(sl, cmp), "weissbier");
  test_streq(smartlist_pqueue_pop(sl, cmp), "zebras");
  test_eq(smartlist_len(sl), 0);
  OK();
#undef OK
  smartlist_free(sl);
}

static void
test_util_gzip(void)
{
  char *buf1, *buf2=NULL, *buf3=NULL, *cp1, *cp2;
  const char *ccp2;
  size_t len1, len2;
  tor_zlib_state_t *state;

  buf1 = tor_strdup("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAZAAAAAAAAAAAAAAAAAAAZ");
  test_assert(detect_compression_method(buf1, strlen(buf1)) == UNKNOWN_METHOD);
  if (is_gzip_supported()) {
    test_assert(!tor_gzip_compress(&buf2, &len1, buf1, strlen(buf1)+1,
                                   GZIP_METHOD));
    test_assert(buf2);
    test_assert(!memcmp(buf2, "\037\213", 2)); /* Gzip magic. */
    test_assert(detect_compression_method(buf2, len1) == GZIP_METHOD);

    test_assert(!tor_gzip_uncompress(&buf3, &len2, buf2, len1,
                                     GZIP_METHOD, 1, LOG_INFO));
    test_assert(buf3);
    test_streq(buf1,buf3);

    tor_free(buf2);
    tor_free(buf3);
  }

  test_assert(!tor_gzip_compress(&buf2, &len1, buf1, strlen(buf1)+1,
                                 ZLIB_METHOD));
  test_assert(buf2);
  test_assert(!memcmp(buf2, "\x78\xDA", 2)); /* deflate magic. */
  test_assert(detect_compression_method(buf2, len1) == ZLIB_METHOD);

  test_assert(!tor_gzip_uncompress(&buf3, &len2, buf2, len1,
                                   ZLIB_METHOD, 1, LOG_INFO));
  test_assert(buf3);
  test_streq(buf1,buf3);

  /* Check whether we can uncompress concatenated, compresed strings. */
  tor_free(buf3);
  buf2 = tor_realloc(buf2, len1*2);
  memcpy(buf2+len1, buf2, len1);
  test_assert(!tor_gzip_uncompress(&buf3, &len2, buf2, len1*2,
                                   ZLIB_METHOD, 1, LOG_INFO));
  test_eq(len2, (strlen(buf1)+1)*2);
  test_memeq(buf3,
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAZAAAAAAAAAAAAAAAAAAAZ\0"
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAZAAAAAAAAAAAAAAAAAAAZ\0",
             (strlen(buf1)+1)*2);

  tor_free(buf1);
  tor_free(buf2);
  tor_free(buf3);

  /* Check whether we can uncompress partial strings. */
  buf1 =
    tor_strdup("String with low redundancy that won't be compressed much.");
  test_assert(!tor_gzip_compress(&buf2, &len1, buf1, strlen(buf1)+1,
                                 ZLIB_METHOD));
  tor_assert(len1>16);
  /* when we allow an uncomplete string, we should succeed.*/
  tor_assert(!tor_gzip_uncompress(&buf3, &len2, buf2, len1-16,
                                  ZLIB_METHOD, 0, LOG_INFO));
  buf3[len2]='\0';
  tor_assert(len2 > 5);
  tor_assert(!strcmpstart(buf1, buf3));

  /* when we demand a complete string, this must fail. */
  tor_free(buf3);
  tor_assert(tor_gzip_uncompress(&buf3, &len2, buf2, len1-16,
                                 ZLIB_METHOD, 1, LOG_INFO));
  tor_assert(!buf3);

  /* Now, try streaming compression. */
  tor_free(buf1);
  tor_free(buf2);
  tor_free(buf3);
  state = tor_zlib_new(1, ZLIB_METHOD);
  tor_assert(state);
  cp1 = buf1 = tor_malloc(1024);
  len1 = 1024;
  ccp2 = "ABCDEFGHIJABCDEFGHIJ";
  len2 = 21;
  test_assert(tor_zlib_process(state, &cp1, &len1, &ccp2, &len2, 0)
              == TOR_ZLIB_OK);
  test_eq(len2, 0); /* Make sure we compressed it all. */
  test_assert(cp1 > buf1);

  len2 = 0;
  cp2 = cp1;
  test_assert(tor_zlib_process(state, &cp1, &len1, &ccp2, &len2, 1)
              == TOR_ZLIB_DONE);
  test_eq(len2, 0);
  test_assert(cp1 > cp2); /* Make sure we really added something. */

  tor_assert(!tor_gzip_uncompress(&buf3, &len2, buf1, 1024-len1,
                                  ZLIB_METHOD, 1, LOG_WARN));
  test_streq(buf3, "ABCDEFGHIJABCDEFGHIJ"); /*Make sure it compressed right.*/
  tor_free(buf3);

  tor_zlib_free(state);

  tor_free(buf2);
  tor_free(buf3);
  tor_free(buf1);
}

static void
test_util_strmap(void)
{
  strmap_t *map;
  strmap_iter_t *iter;
  const char *k;
  void *v;
  char *visited;
  smartlist_t *found_keys;

  map = strmap_new();
  v = strmap_set(map, "K1", (void*)99);
  test_eq(v, NULL);
  v = strmap_set(map, "K2", (void*)101);
  test_eq(v, NULL);
  v = strmap_set(map, "K1", (void*)100);
  test_eq(v, (void*)99);
  test_eq_ptr(strmap_get(map,"K1"), (void*)100);
  test_eq_ptr(strmap_get(map,"K2"), (void*)101);
  test_eq_ptr(strmap_get(map,"K-not-there"), NULL);
  strmap_assert_ok(map);

  v = strmap_remove(map,"K2");
  strmap_assert_ok(map);
  test_eq_ptr(v, (void*)101);
  test_eq_ptr(strmap_get(map,"K2"), NULL);
  test_eq_ptr(strmap_remove(map,"K2"), NULL);

  strmap_set(map, "K2", (void*)101);
  strmap_set(map, "K3", (void*)102);
  strmap_set(map, "K4", (void*)103);
  strmap_assert_ok(map);
  strmap_set(map, "K5", (void*)104);
  strmap_set(map, "K6", (void*)105);
  strmap_assert_ok(map);

  /* Test iterator. */
  iter = strmap_iter_init(map);
  found_keys = smartlist_create();
  while (!strmap_iter_done(iter)) {
    strmap_iter_get(iter,&k,&v);
    smartlist_add(found_keys, tor_strdup(k));
    test_eq_ptr(v, strmap_get(map, k));

    if (!strcmp(k, "K2")) {
      iter = strmap_iter_next_rmv(map,iter);
    } else {
      iter = strmap_iter_next(map,iter);
    }
  }

  /* Make sure we removed K2, but not the others. */
  test_eq_ptr(strmap_get(map, "K2"), NULL);
  test_eq_ptr(strmap_get(map, "K5"), (void*)104);
  /* Make sure we visited everyone once */
  smartlist_sort_strings(found_keys);
  visited = smartlist_join_strings(found_keys, ":", 0, NULL);
  test_streq(visited, "K1:K2:K3:K4:K5:K6");
  tor_free(visited);
  SMARTLIST_FOREACH(found_keys, char *, cp, tor_free(cp));
  smartlist_free(found_keys);

  strmap_assert_ok(map);
  /* Clean up after ourselves. */
  strmap_free(map, NULL);

  /* Now try some lc functions. */
  map = strmap_new();
  strmap_set_lc(map,"Ab.C", (void*)1);
  test_eq_ptr(strmap_get(map,"ab.c"), (void*)1);
  strmap_assert_ok(map);
  test_eq_ptr(strmap_get_lc(map,"AB.C"), (void*)1);
  test_eq_ptr(strmap_get(map,"AB.C"), NULL);
  test_eq_ptr(strmap_remove_lc(map,"aB.C"), (void*)1);
  strmap_assert_ok(map);
  test_eq_ptr(strmap_get_lc(map,"AB.C"), NULL);
  strmap_free(map,NULL);
}

static void
test_util_mmap(void)
{
  char *fname1 = tor_strdup(get_fname("mapped_1"));
  char *fname2 = tor_strdup(get_fname("mapped_2"));
  char *fname3 = tor_strdup(get_fname("mapped_3"));
  const size_t buflen = 17000;
  char *buf = tor_malloc(17000);
  tor_mmap_t *mapping;

  crypto_rand(buf, buflen);

  write_str_to_file(fname1, "Short file.", 1);
  write_bytes_to_file(fname2, buf, buflen, 1);
  write_bytes_to_file(fname3, buf, 16384, 1);

  mapping = tor_mmap_file(fname1);
  test_assert(mapping);
  test_eq(mapping->size, strlen("Short file."));
  test_streq(mapping->data, "Short file.");
#ifdef MS_WINDOWS
  tor_munmap_file(mapping);
  test_assert(unlink(fname1) == 0);
#else
  /* make sure we can unlink. */
  test_assert(unlink(fname1) == 0);
  test_streq(mapping->data, "Short file.");
  tor_munmap_file(mapping);
#endif

  /* Make sure that we fail to map a no-longer-existent file. */
  mapping = tor_mmap_file(fname1);
  test_assert(mapping == NULL);

  /* Now try a big file that stretches across a few pages and isn't aligned */
  mapping = tor_mmap_file(fname2);
  test_assert(mapping);
  test_eq(mapping->size, buflen);
  test_memeq(mapping->data, buf, buflen);
  tor_munmap_file(mapping);

  /* Now try a big aligned file. */
  mapping = tor_mmap_file(fname3);
  test_assert(mapping);
  test_eq(mapping->size, 16384);
  test_memeq(mapping->data, buf, 16384);
  tor_munmap_file(mapping);

  /* fname1 got unlinked above */
  unlink(fname2);
  unlink(fname3);

  tor_free(fname1);
  tor_free(fname2);
  tor_free(fname3);
  tor_free(buf);
}

static void
test_util_control_formats(void)
{
  char *out;
  const char *inp =
    "..This is a test\r\nof the emergency \nbroadcast\r\n..system.\r\nZ.\r\n";
  size_t sz;

  sz = read_escaped_data(inp, strlen(inp), &out);
  test_streq(out,
             ".This is a test\nof the emergency \nbroadcast\n.system.\nZ.\n");
  test_eq(sz, strlen(out));

  tor_free(out);
}

static void
test_onion_handshake(void)
{
  /* client-side */
  crypto_dh_env_t *c_dh = NULL;
  char c_buf[ONIONSKIN_CHALLENGE_LEN];
  char c_keys[40];

  /* server-side */
  char s_buf[ONIONSKIN_REPLY_LEN];
  char s_keys[40];

  /* shared */
  crypto_pk_env_t *pk = NULL;

  pk = pk_generate(0);

  /* client handshake 1. */
  memset(c_buf, 0, ONIONSKIN_CHALLENGE_LEN);
  test_assert(! onion_skin_create(pk, &c_dh, c_buf));

  /* server handshake */
  memset(s_buf, 0, ONIONSKIN_REPLY_LEN);
  memset(s_keys, 0, 40);
  test_assert(! onion_skin_server_handshake(c_buf, pk, NULL,
                                            s_buf, s_keys, 40));

  /* client handshake 2 */
  memset(c_keys, 0, 40);
  test_assert(! onion_skin_client_handshake(c_dh, s_buf, c_keys, 40));

  crypto_dh_free(c_dh);

  if (memcmp(c_keys, s_keys, 40)) {
    puts("Aiiiie");
    exit(1);
  }
  test_memeq(c_keys, s_keys, 40);
  memset(s_buf, 0, 40);
  test_memneq(c_keys, s_buf, 40);
  crypto_free_pk_env(pk);
}

extern smartlist_t *fingerprint_list;

static void
test_dir_format(void)
{
  char buf[8192], buf2[8192];
  char platform[256];
  char fingerprint[FINGERPRINT_LEN+1];
  char *pk1_str = NULL, *pk2_str = NULL, *pk3_str = NULL, *cp;
  size_t pk1_str_len, pk2_str_len, pk3_str_len;
  routerinfo_t r1, r2;
  crypto_pk_env_t *pk1 = NULL, *pk2 = NULL, *pk3 = NULL;
  routerinfo_t *rp1 = NULL, *rp2 = NULL;
  addr_policy_t ex1, ex2;
  routerlist_t *dir1 = NULL, *dir2 = NULL;
  tor_version_t ver1;

  pk1 = pk_generate(0);
  pk2 = pk_generate(1);
  pk3 = pk_generate(2);

  test_assert( is_legal_nickname("a"));
  test_assert(!is_legal_nickname(""));
  test_assert(!is_legal_nickname("abcdefghijklmnopqrst")); /* 20 chars */
  test_assert(!is_legal_nickname("hyphen-")); /* bad char */
  test_assert( is_legal_nickname("abcdefghijklmnopqrs")); /* 19 chars */
  test_assert(!is_legal_nickname("$AAAAAAAA01234AAAAAAAAAAAAAAAAAAAAAAAAAAA"));
  /* valid */
  test_assert( is_legal_nickname_or_hexdigest(
                                 "$AAAAAAAA01234AAAAAAAAAAAAAAAAAAAAAAAAAAA"));
  test_assert( is_legal_nickname_or_hexdigest(
                         "$AAAAAAAA01234AAAAAAAAAAAAAAAAAAAAAAAAAAA=fred"));
  test_assert( is_legal_nickname_or_hexdigest(
                         "$AAAAAAAA01234AAAAAAAAAAAAAAAAAAAAAAAAAAA~fred"));
  /* too short */
  test_assert(!is_legal_nickname_or_hexdigest(
                                 "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
  /* illegal char */
  test_assert(!is_legal_nickname_or_hexdigest(
                                 "$AAAAAAzAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
  /* hex part too long */
  test_assert(!is_legal_nickname_or_hexdigest(
                         "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
  test_assert(!is_legal_nickname_or_hexdigest(
                         "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=fred"));
  /* Bad nickname */
  test_assert(!is_legal_nickname_or_hexdigest(
                         "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
  test_assert(!is_legal_nickname_or_hexdigest(
                         "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA~"));
  test_assert(!is_legal_nickname_or_hexdigest(
                       "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA~hyphen-"));
  test_assert(!is_legal_nickname_or_hexdigest(
                       "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA~"
                       "abcdefghijklmnoppqrst"));
  /* Bad extra char. */
  test_assert(!is_legal_nickname_or_hexdigest(
                         "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA!"));
  test_assert(is_legal_nickname_or_hexdigest("xyzzy"));
  test_assert(is_legal_nickname_or_hexdigest("abcdefghijklmnopqrs"));
  test_assert(!is_legal_nickname_or_hexdigest("abcdefghijklmnopqrst"));

  get_platform_str(platform, sizeof(platform));
  memset(&r1,0,sizeof(r1));
  memset(&r2,0,sizeof(r2));
  r1.address = tor_strdup("18.244.0.1");
  r1.addr = 0xc0a80001u; /* 192.168.0.1 */
  r1.cache_info.published_on = 0;
  r1.or_port = 9000;
  r1.dir_port = 9003;
  r1.onion_pkey = pk1;
  r1.identity_pkey = pk2;
  r1.bandwidthrate = 1000;
  r1.bandwidthburst = 5000;
  r1.bandwidthcapacity = 10000;
  r1.exit_policy = NULL;
  r1.nickname = tor_strdup("Magri");
  r1.platform = tor_strdup(platform);

  ex1.policy_type = ADDR_POLICY_ACCEPT;
  ex1.string = NULL;
  ex1.addr = 0;
  ex1.maskbits = 0;
  ex1.prt_min = ex1.prt_max = 80;
  ex1.next = &ex2;
  ex2.policy_type = ADDR_POLICY_REJECT;
  ex2.addr = 18 << 24;
  ex2.maskbits = 8;
  ex2.prt_min = ex2.prt_max = 24;
  ex2.next = NULL;
  r2.address = tor_strdup("1.1.1.1");
  r2.addr = 0x0a030201u; /* 10.3.2.1 */
  r2.platform = tor_strdup(platform);
  r2.cache_info.published_on = 5;
  r2.or_port = 9005;
  r2.dir_port = 0;
  r2.onion_pkey = pk2;
  r2.identity_pkey = pk1;
  r2.bandwidthrate = r2.bandwidthburst = r2.bandwidthcapacity = 3000;
  r2.exit_policy = &ex1;
  r2.nickname = tor_strdup("Fred");

  test_assert(!crypto_pk_write_public_key_to_string(pk1, &pk1_str,
                                                    &pk1_str_len));
  test_assert(!crypto_pk_write_public_key_to_string(pk2 , &pk2_str,
                                                    &pk2_str_len));
  test_assert(!crypto_pk_write_public_key_to_string(pk3 , &pk3_str,
                                                    &pk3_str_len));

  memset(buf, 0, 2048);
  test_assert(router_dump_router_to_string(buf, 2048, &r1, pk2)>0);

  strlcpy(buf2, "router Magri 18.244.0.1 9000 0 9003\n"
          "platform Tor "VERSION" on ", sizeof(buf2));
  strlcat(buf2, get_uname(), sizeof(buf2));
  strlcat(buf2, "\n"
          "published 1970-01-01 00:00:00\n"
          "opt fingerprint ", sizeof(buf2));
  test_assert(!crypto_pk_get_fingerprint(pk2, fingerprint, 1));
  strlcat(buf2, fingerprint, sizeof(buf2));
  strlcat(buf2, "\nuptime 0\n"
  /* XXX the "0" above is hardcoded, but even if we made it reflect
   * uptime, that still wouldn't make it right, because the two
   * descriptors might be made on different seconds... hm. */
         "bandwidth 1000 5000 10000\n"
          "opt extra-info-digest 0000000000000000000000000000000000000000\n"
          "onion-key\n", sizeof(buf2));
  strlcat(buf2, pk1_str, sizeof(buf2));
  strlcat(buf2, "signing-key\n", sizeof(buf2));
  strlcat(buf2, pk2_str, sizeof(buf2));
  strlcat(buf2, "router-signature\n", sizeof(buf2));
  buf[strlen(buf2)] = '\0'; /* Don't compare the sig; it's never the same
                             * twice */

  test_streq(buf, buf2);

  test_assert(router_dump_router_to_string(buf, 2048, &r1, pk2)>0);
  cp = buf;
  rp1 = router_parse_entry_from_string((const char*)cp,NULL,1,0,NULL);
  test_assert(rp1);
  test_streq(rp1->address, r1.address);
  test_eq(rp1->or_port, r1.or_port);
  //test_eq(rp1->dir_port, r1.dir_port);
  test_eq(rp1->bandwidthrate, r1.bandwidthrate);
  test_eq(rp1->bandwidthburst, r1.bandwidthburst);
  test_eq(rp1->bandwidthcapacity, r1.bandwidthcapacity);
  test_assert(crypto_pk_cmp_keys(rp1->onion_pkey, pk1) == 0);
  test_assert(crypto_pk_cmp_keys(rp1->identity_pkey, pk2) == 0);
  test_assert(rp1->exit_policy == NULL);

#if 0
  /* XXX Once we have exit policies, test this again. XXX */
  strlcpy(buf2, "router tor.tor.tor 9005 0 0 3000\n", sizeof(buf2));
  strlcat(buf2, pk2_str, sizeof(buf2));
  strlcat(buf2, "signing-key\n", sizeof(buf2));
  strlcat(buf2, pk1_str, sizeof(buf2));
  strlcat(buf2, "accept *:80\nreject 18.*:24\n\n", sizeof(buf2));
  test_assert(router_dump_router_to_string(buf, 2048, &r2, pk2)>0);
  test_streq(buf, buf2);

  cp = buf;
  rp2 = router_parse_entry_from_string(&cp,1);
  test_assert(rp2);
  test_streq(rp2->address, r2.address);
  test_eq(rp2->or_port, r2.or_port);
  test_eq(rp2->dir_port, r2.dir_port);
  test_eq(rp2->bandwidth, r2.bandwidth);
  test_assert(crypto_pk_cmp_keys(rp2->onion_pkey, pk2) == 0);
  test_assert(crypto_pk_cmp_keys(rp2->identity_pkey, pk1) == 0);
  test_eq(rp2->exit_policy->policy_type, EXIT_POLICY_ACCEPT);
  test_streq(rp2->exit_policy->string, "accept *:80");
  test_streq(rp2->exit_policy->address, "*");
  test_streq(rp2->exit_policy->port, "80");
  test_eq(rp2->exit_policy->next->policy_type, EXIT_POLICY_REJECT);
  test_streq(rp2->exit_policy->next->string, "reject 18.*:24");
  test_streq(rp2->exit_policy->next->address, "18.*");
  test_streq(rp2->exit_policy->next->port, "24");
  test_assert(rp2->exit_policy->next->next == NULL);

  /* Okay, now for the directories. */
  {
    fingerprint_list = smartlist_create();
    crypto_pk_get_fingerprint(pk2, buf, 1);
    add_fingerprint_to_dir("Magri", buf, fingerprint_list);
    crypto_pk_get_fingerprint(pk1, buf, 1);
    add_fingerprint_to_dir("Fred", buf, fingerprint_list);
  }

  {
  char d[DIGEST_LEN];
  const char *m;
  /* XXXX NM re-enable. */
  /* Make sure routers aren't too far in the past any more. */
  r1.cache_info.published_on = time(NULL);
  r2.cache_info.published_on = time(NULL)-3*60*60;
  test_assert(router_dump_router_to_string(buf, 2048, &r1, pk2)>0);
  test_eq(dirserv_add_descriptor(buf,&m), 2);
  test_assert(router_dump_router_to_string(buf, 2048, &r2, pk1)>0);
  test_eq(dirserv_add_descriptor(buf,&m), 2);
  get_options()->Nickname = tor_strdup("DirServer");
  test_assert(!dirserv_dump_directory_to_string(&cp,pk3, 0));
  crypto_pk_get_digest(pk3, d);
  test_assert(!router_parse_directory(cp));
  test_eq(2, smartlist_len(dir1->routers));
  tor_free(cp);
  }
#endif
  dirserv_free_fingerprint_list();

  tor_free(pk1_str);
  tor_free(pk2_str);
  if (pk1) crypto_free_pk_env(pk1);
  if (pk2) crypto_free_pk_env(pk2);
  if (rp1) routerinfo_free(rp1);
  if (rp2) routerinfo_free(rp2);
  tor_free(dir1); /* XXXX And more !*/
  tor_free(dir2); /* And more !*/

  /* Try out version parsing functionality */
  test_eq(0, tor_version_parse("0.3.4pre2-cvs", &ver1));
  test_eq(0, ver1.major);
  test_eq(3, ver1.minor);
  test_eq(4, ver1.micro);
  test_eq(VER_PRE, ver1.status);
  test_eq(2, ver1.patchlevel);
  test_eq(0, tor_version_parse("0.3.4rc1", &ver1));
  test_eq(0, ver1.major);
  test_eq(3, ver1.minor);
  test_eq(4, ver1.micro);
  test_eq(VER_RC, ver1.status);
  test_eq(1, ver1.patchlevel);
  test_eq(0, tor_version_parse("1.3.4", &ver1));
  test_eq(1, ver1.major);
  test_eq(3, ver1.minor);
  test_eq(4, ver1.micro);
  test_eq(VER_RELEASE, ver1.status);
  test_eq(0, ver1.patchlevel);
  test_eq(0, tor_version_parse("1.3.4.999", &ver1));
  test_eq(1, ver1.major);
  test_eq(3, ver1.minor);
  test_eq(4, ver1.micro);
  test_eq(VER_RELEASE, ver1.status);
  test_eq(999, ver1.patchlevel);
  test_eq(0, tor_version_parse("0.1.2.4-alpha", &ver1));
  test_eq(0, ver1.major);
  test_eq(1, ver1.minor);
  test_eq(2, ver1.micro);
  test_eq(4, ver1.patchlevel);
  test_eq(VER_RELEASE, ver1.status);
  test_streq("alpha", ver1.status_tag);
  test_eq(0, tor_version_parse("0.1.2.4", &ver1));
  test_eq(0, ver1.major);
  test_eq(1, ver1.minor);
  test_eq(2, ver1.micro);
  test_eq(4, ver1.patchlevel);
  test_eq(VER_RELEASE, ver1.status);
  test_streq("", ver1.status_tag);

#define test_eq_vs(vs1, vs2) test_eq_type(version_status_t, "%d", (vs1), (vs2))
#define test_v_i_o(val, ver, lst) \
  test_eq_vs(val, tor_version_is_obsolete(ver, lst))

  /* make sure tor_version_is_obsolete() works */
  test_v_i_o(VS_OLD, "0.0.1", "Tor 0.0.2");
  test_v_i_o(VS_OLD, "0.0.1", "0.0.2, Tor 0.0.3");
  test_v_i_o(VS_OLD, "0.0.1", "0.0.2,Tor 0.0.3");
  test_v_i_o(VS_OLD, "0.0.1","0.0.3,BetterTor 0.0.1");
  test_v_i_o(VS_RECOMMENDED, "0.0.2", "Tor 0.0.2,Tor 0.0.3");
  test_v_i_o(VS_NEW_IN_SERIES, "0.0.2", "Tor 0.0.2pre1,Tor 0.0.3");
  test_v_i_o(VS_OLD, "0.0.2", "Tor 0.0.2.1,Tor 0.0.3");
  test_v_i_o(VS_NEW, "0.1.0", "Tor 0.0.2,Tor 0.0.3");
  test_v_i_o(VS_RECOMMENDED, "0.0.7rc2", "0.0.7,Tor 0.0.7rc2,Tor 0.0.8");
  test_v_i_o(VS_OLD, "0.0.5.0", "0.0.5.1-cvs");
  test_v_i_o(VS_NEW_IN_SERIES, "0.0.5.1-cvs", "0.0.5, 0.0.6");
  /* Not on list, but newer than any in same series. */
  test_v_i_o(VS_NEW_IN_SERIES, "0.1.0.3",
             "Tor 0.1.0.2,Tor 0.0.9.5,Tor 0.1.1.0");
  /* Series newer than any on list. */
  test_v_i_o(VS_NEW, "0.1.2.3", "Tor 0.1.0.2,Tor 0.0.9.5,Tor 0.1.1.0");
  /* Series older than any on list. */
  test_v_i_o(VS_OLD, "0.0.1.3", "Tor 0.1.0.2,Tor 0.0.9.5,Tor 0.1.1.0");
  /* Not on list, not newer than any on same series. */
  test_v_i_o(VS_UNRECOMMENDED, "0.1.0.1",
             "Tor 0.1.0.2,Tor 0.0.9.5,Tor 0.1.1.0");
  /* On list, not newer than any on same series. */
  test_v_i_o(VS_UNRECOMMENDED,
             "0.1.0.1", "Tor 0.1.0.2,Tor 0.0.9.5,Tor 0.1.1.0");
  test_eq(0, tor_version_as_new_as("Tor 0.0.5", "0.0.9pre1-cvs"));
  test_eq(1, tor_version_as_new_as(
          "Tor 0.0.8 on Darwin 64-121-192-100.c3-0."
          "sfpo-ubr1.sfrn-sfpo.ca.cable.rcn.com Power Macintosh",
          "0.0.8rc2"));
  test_eq(0, tor_version_as_new_as(
          "Tor 0.0.8 on Darwin 64-121-192-100.c3-0."
          "sfpo-ubr1.sfrn-sfpo.ca.cable.rcn.com Power Macintosh", "0.0.8.2"));

  /* Now try svn revisions. */
  test_eq(1, tor_version_as_new_as("Tor 0.2.1.0-dev (r100)",
                                   "Tor 0.2.1.0-dev (r99)"));
  test_eq(1, tor_version_as_new_as("Tor 0.2.1.0-dev (r100) on Banana Jr",
                                   "Tor 0.2.1.0-dev (r99) on Hal 9000"));
  test_eq(1, tor_version_as_new_as("Tor 0.2.1.0-dev (r100)",
                                   "Tor 0.2.1.0-dev on Colossus"));
  test_eq(0, tor_version_as_new_as("Tor 0.2.1.0-dev (r99)",
                                   "Tor 0.2.1.0-dev (r100)"));
  test_eq(0, tor_version_as_new_as("Tor 0.2.1.0-dev (r99) on MCP",
                                   "Tor 0.2.1.0-dev (r100) on AM"));
  test_eq(0, tor_version_as_new_as("Tor 0.2.1.0-dev",
                                   "Tor 0.2.1.0-dev (r99)"));
  test_eq(1, tor_version_as_new_as("Tor 0.2.1.1",
                                   "Tor 0.2.1.0-dev (r99)"));
}

extern const char AUTHORITY_CERT_1[];
extern const char AUTHORITY_SIGNKEY_1[];
extern const char AUTHORITY_CERT_2[];
extern const char AUTHORITY_SIGNKEY_2[];
extern const char AUTHORITY_CERT_3[];
extern const char AUTHORITY_SIGNKEY_3[];

static void
test_same_voter(networkstatus_voter_info_t *v1,
                networkstatus_voter_info_t *v2)
{
  test_streq(v1->nickname, v2->nickname);
  test_memeq(v1->identity_digest, v2->identity_digest, DIGEST_LEN);
  test_streq(v1->address, v2->address);
  test_eq(v1->addr, v2->addr);
  test_eq(v1->dir_port, v2->dir_port);
  test_eq(v1->or_port, v2->or_port);
  test_streq(v1->contact, v2->contact);
  test_memeq(v1->vote_digest, v2->vote_digest, DIGEST_LEN);
}

static void
test_util_order_functions(void)
{
  int lst[25], n = 0;
  //  int a=12,b=24,c=25,d=60,e=77;

#define median() median_int(lst, n)

  lst[n++] = 12;
  test_eq(12, median()); /* 12 */
  lst[n++] = 77;
  //smartlist_shuffle(sl);
  test_eq(12, median()); /* 12, 77 */
  lst[n++] = 77;
  //smartlist_shuffle(sl);
  test_eq(77, median()); /* 12, 77, 77 */
  lst[n++] = 24;
  test_eq(24, median()); /* 12,24,77,77 */
  lst[n++] = 60;
  lst[n++] = 12;
  lst[n++] = 25;
  //smartlist_shuffle(sl);
  test_eq(25, median()); /* 12,12,24,25,60,77,77 */
#undef median
}

static void
test_v3_networkstatus(void)
{
  authority_cert_t *cert1, *cert2, *cert3;
  crypto_pk_env_t *sign_skey_1, *sign_skey_2, *sign_skey_3;

  time_t now = time(NULL);
  networkstatus_voter_info_t *voter;
  networkstatus_vote_t *vote, *v1, *v2, *v3, *con;
  vote_routerstatus_t *vrs;
  routerstatus_t *rs;
  char *v1_text, *v2_text, *v3_text, *consensus_text, *cp;
  smartlist_t *votes = smartlist_create();

  /* Parse certificates and keys. */
  cert1 = authority_cert_parse_from_string(AUTHORITY_CERT_1, NULL);
  test_assert(cert1);
  cert2 = authority_cert_parse_from_string(AUTHORITY_CERT_2, NULL);
  test_assert(cert2);
  cert3 = authority_cert_parse_from_string(AUTHORITY_CERT_3, NULL);
  test_assert(cert3);
  sign_skey_1 = crypto_new_pk_env();
  sign_skey_2 = crypto_new_pk_env();
  sign_skey_3 = crypto_new_pk_env();

  test_assert(!crypto_pk_read_private_key_from_string(sign_skey_1,
                                                      AUTHORITY_SIGNKEY_1));
  test_assert(!crypto_pk_read_private_key_from_string(sign_skey_2,
                                                      AUTHORITY_SIGNKEY_2));
  test_assert(!crypto_pk_read_private_key_from_string(sign_skey_3,
                                                      AUTHORITY_SIGNKEY_3));

  test_assert(!crypto_pk_cmp_keys(sign_skey_1, cert1->signing_key));
  test_assert(!crypto_pk_cmp_keys(sign_skey_2, cert2->signing_key));

  /*
   * Set up a vote; generate it; try to parse it.
   */
  vote = tor_malloc_zero(sizeof(networkstatus_vote_t));
  vote->is_vote = 1;
  vote->published = now;
  vote->valid_after = now+1000;
  vote->fresh_until = now+2000;
  vote->valid_until = now+3000;
  vote->vote_seconds = 100;
  vote->dist_seconds = 200;
  vote->client_versions = tor_strdup("0.1.2.14,0.1.2.15");
  vote->server_versions = tor_strdup("0.1.2.14,0.1.2.15,0.1.2.16");
  vote->known_flags = smartlist_create();
  smartlist_split_string(vote->known_flags,
                     "Authority Exit Fast Guard Running Stable V2Dir Valid",
                     0, SPLIT_SKIP_SPACE|SPLIT_IGNORE_BLANK, 0);
  vote->voters = smartlist_create();
  voter = tor_malloc_zero(sizeof(networkstatus_voter_info_t));
  voter->nickname = tor_strdup("Voter1");
  voter->address = tor_strdup("1.2.3.4");
  voter->addr = 0x01020304;
  voter->dir_port = 80;
  voter->or_port = 9000;
  voter->contact = tor_strdup("voter@example.com");
  crypto_pk_get_digest(cert1->identity_key, voter->identity_digest);
  smartlist_add(vote->voters, voter);
  vote->cert = authority_cert_dup(cert1);
  vote->routerstatus_list = smartlist_create();
  /* add the first routerstatus. */
  vrs = tor_malloc_zero(sizeof(vote_routerstatus_t));
  rs = &vrs->status;
  vrs->version = tor_strdup("0.1.2.14");
  rs->published_on = now-1500;
  strlcpy(rs->nickname, "router2", sizeof(rs->nickname));
  memset(rs->identity_digest, 3, DIGEST_LEN);
  memset(rs->descriptor_digest, 78, DIGEST_LEN);
  rs->addr = 0x99008801;
  rs->or_port = 443;
  rs->dir_port = 8000;
  /* all flags cleared */
  smartlist_add(vote->routerstatus_list, vrs);
  /* add the second routerstatus. */
  vrs = tor_malloc_zero(sizeof(vote_routerstatus_t));
  rs = &vrs->status;
  vrs->version = tor_strdup("0.2.0.5");
  rs->published_on = now-1000;
  strlcpy(rs->nickname, "router1", sizeof(rs->nickname));
  memset(rs->identity_digest, 5, DIGEST_LEN);
  memset(rs->descriptor_digest, 77, DIGEST_LEN);
  rs->addr = 0x99009901;
  rs->or_port = 443;
  rs->dir_port = 0;
  rs->is_exit = rs->is_stable = rs->is_fast = rs->is_running =
    rs->is_valid = rs->is_v2_dir = rs->is_possible_guard = 1;
  smartlist_add(vote->routerstatus_list, vrs);
  /* add the third routerstatus. */
  vrs = tor_malloc_zero(sizeof(vote_routerstatus_t));
  rs = &vrs->status;
  vrs->version = tor_strdup("0.1.0.3");
  rs->published_on = now-1000;
  strlcpy(rs->nickname, "router3", sizeof(rs->nickname));
  memset(rs->identity_digest, 33, DIGEST_LEN);
  memset(rs->descriptor_digest, 78, DIGEST_LEN);
  rs->addr = 0xAA009901;
  rs->or_port = 400;
  rs->dir_port = 9999;
  rs->is_authority = rs->is_exit = rs->is_stable = rs->is_fast =
    rs->is_running = rs->is_valid = rs->is_v2_dir = rs->is_possible_guard = 1;
  smartlist_add(vote->routerstatus_list, vrs);

  /* dump the vote and try to parse it. */
  v1_text = format_networkstatus_vote(sign_skey_1, vote);
  test_assert(v1_text);
  v1 = networkstatus_parse_vote_from_string(v1_text, NULL, 1);
  test_assert(v1);

  /* Make sure the parsed thing was right. */
  test_eq(v1->is_vote, 1);
  test_eq(v1->published, vote->published);
  test_eq(v1->valid_after, vote->valid_after);
  test_eq(v1->fresh_until, vote->fresh_until);
  test_eq(v1->valid_until, vote->valid_until);
  test_eq(v1->vote_seconds, vote->vote_seconds);
  test_eq(v1->dist_seconds, vote->dist_seconds);
  test_streq(v1->client_versions, vote->client_versions);
  test_streq(v1->server_versions, vote->server_versions);
  test_assert(v1->voters && smartlist_len(v1->voters));
  voter = smartlist_get(v1->voters, 0);
  test_streq(voter->nickname, "Voter1");
  test_streq(voter->address, "1.2.3.4");
  test_eq(voter->addr, 0x01020304);
  test_eq(voter->dir_port, 80);
  test_eq(voter->or_port, 9000);
  test_streq(voter->contact, "voter@example.com");
  test_assert(v1->cert);
  test_assert(!crypto_pk_cmp_keys(sign_skey_1, v1->cert->signing_key));
  cp = smartlist_join_strings(v1->known_flags, ":", 0, NULL);
  test_streq(cp, "Authority:Exit:Fast:Guard:Running:Stable:V2Dir:Valid");
  tor_free(cp);
  test_eq(smartlist_len(v1->routerstatus_list), 3);
  /* Check the first routerstatus. */
  vrs = smartlist_get(v1->routerstatus_list, 0);
  rs = &vrs->status;
  test_streq(vrs->version, "0.1.2.14");
  test_eq(rs->published_on, now-1500);
  test_streq(rs->nickname, "router2");
  test_memeq(rs->identity_digest,
             "\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3",
             DIGEST_LEN);
  test_memeq(rs->descriptor_digest, "NNNNNNNNNNNNNNNNNNNN", DIGEST_LEN);
  test_eq(rs->addr, 0x99008801);
  test_eq(rs->or_port, 443);
  test_eq(rs->dir_port, 8000);
  test_eq(vrs->flags, U64_LITERAL(0));
  /* Check the second routerstatus. */
  vrs = smartlist_get(v1->routerstatus_list, 1);
  rs = &vrs->status;
  test_streq(vrs->version, "0.2.0.5");
  test_eq(rs->published_on, now-1000);
  test_streq(rs->nickname, "router1");
  test_memeq(rs->identity_digest,
             "\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5",
             DIGEST_LEN);
  test_memeq(rs->descriptor_digest, "MMMMMMMMMMMMMMMMMMMM", DIGEST_LEN);
  test_eq(rs->addr, 0x99009901);
  test_eq(rs->or_port, 443);
  test_eq(rs->dir_port, 0);
  test_eq(vrs->flags, U64_LITERAL(254)); // all flags except "authority."

  /* Generate second vote. It disagrees on some of the times,
   * and doesn't list versions, and knows some crazy flags */
  vote->published = now+1;
  vote->fresh_until = now+3005;
  vote->dist_seconds = 300;
  authority_cert_free(vote->cert);
  vote->cert = authority_cert_dup(cert2);
  tor_free(vote->client_versions);
  tor_free(vote->server_versions);
  voter = smartlist_get(vote->voters, 0);
  tor_free(voter->nickname);
  tor_free(voter->address);
  voter->nickname = tor_strdup("Voter2");
  voter->address = tor_strdup("2.3.4.5");
  voter->addr = 0x02030405;
  crypto_pk_get_digest(cert2->identity_key, voter->identity_digest);
  smartlist_add(vote->known_flags, tor_strdup("MadeOfCheese"));
  smartlist_add(vote->known_flags, tor_strdup("MadeOfTin"));
  smartlist_sort_strings(vote->known_flags);
  vrs = smartlist_get(vote->routerstatus_list, 2);
  smartlist_del_keeporder(vote->routerstatus_list, 2);
  tor_free(vrs->version);
  tor_free(vrs);
  vrs = smartlist_get(vote->routerstatus_list, 0);
  vrs->status.is_fast = 1;
  /* generate and parse. */
  v2_text = format_networkstatus_vote(sign_skey_2, vote);
  test_assert(v2_text);
  v2 = networkstatus_parse_vote_from_string(v2_text, NULL, 1);
  test_assert(v2);
  /* Check that flags come out right.*/
  cp = smartlist_join_strings(v2->known_flags, ":", 0, NULL);
  test_streq(cp, "Authority:Exit:Fast:Guard:MadeOfCheese:MadeOfTin:"
             "Running:Stable:V2Dir:Valid");
  tor_free(cp);
  vrs = smartlist_get(v2->routerstatus_list, 1);
  /* 1023 - authority(1) - madeofcheese(16) - madeoftin(32) */
  test_eq(vrs->flags, U64_LITERAL(974));

  /* Generate the third vote. */
  vote->published = now;
  vote->fresh_until = now+2003;
  vote->dist_seconds = 250;
  authority_cert_free(vote->cert);
  vote->cert = authority_cert_dup(cert3);
  vote->client_versions = tor_strdup("0.1.2.14,0.1.2.17");
  vote->server_versions = tor_strdup("0.1.2.10,0.1.2.15,0.1.2.16");
  voter = smartlist_get(vote->voters, 0);
  tor_free(voter->nickname);
  tor_free(voter->address);
  voter->nickname = tor_strdup("Voter3");
  voter->address = tor_strdup("3.4.5.6");
  voter->addr = 0x03040506;
  crypto_pk_get_digest(cert3->identity_key, voter->identity_digest);
  vrs = smartlist_get(vote->routerstatus_list, 0);
  smartlist_del_keeporder(vote->routerstatus_list, 0);
  tor_free(vrs->version);
  tor_free(vrs);
  vrs = smartlist_get(vote->routerstatus_list, 0);
  memset(vrs->status.descriptor_digest, (int)'Z', DIGEST_LEN);

  v3_text = format_networkstatus_vote(sign_skey_3, vote);
  test_assert(v3_text);

  v3 = networkstatus_parse_vote_from_string(v3_text, NULL, 1);
  test_assert(v3);

  /* Compute a consensus as voter 3. */
  smartlist_add(votes, v3);
  smartlist_add(votes, v1);
  smartlist_add(votes, v2);
  consensus_text = networkstatus_compute_consensus(votes, 3,
                                                   cert3->identity_key,
                                                   sign_skey_3);
  test_assert(consensus_text);
  con = networkstatus_parse_vote_from_string(consensus_text, NULL, 0);
  test_assert(con);
  //log_notice(LD_GENERAL, "<<%s>>\n<<%s>>\n<<%s>>\n",
  //           v1_text, v2_text, v3_text);

  /* Check consensus contents. */
  test_assert(!con->is_vote);
  test_eq(con->published, 0); /* this field only appears in votes. */
  test_eq(con->valid_after, now+1000);
  test_eq(con->fresh_until, now+2003); /* median */
  test_eq(con->valid_until, now+3000);
  test_eq(con->vote_seconds, 100);
  test_eq(con->dist_seconds, 250); /* median */
  test_streq(con->client_versions, "0.1.2.14");
  test_streq(con->server_versions, "0.1.2.15,0.1.2.16");
  cp = smartlist_join_strings(v2->known_flags, ":", 0, NULL);
  test_streq(cp, "Authority:Exit:Fast:Guard:MadeOfCheese:MadeOfTin:"
             "Running:Stable:V2Dir:Valid");
  tor_free(cp);
  test_eq(3, smartlist_len(con->voters));
  /* The voter id digests should be in this order. */
  test_assert(memcmp(cert2->cache_info.identity_digest,
                     cert3->cache_info.identity_digest,DIGEST_LEN)<0);
  test_assert(memcmp(cert3->cache_info.identity_digest,
                     cert1->cache_info.identity_digest,DIGEST_LEN)<0);
  test_same_voter(smartlist_get(con->voters, 0),
                  smartlist_get(v2->voters, 0));
  test_same_voter(smartlist_get(con->voters, 1),
                  smartlist_get(v3->voters, 0));
  test_same_voter(smartlist_get(con->voters, 2),
                  smartlist_get(v1->voters, 0));

  test_assert(!con->cert);
  test_eq(2, smartlist_len(con->routerstatus_list));
  /* There should be two listed routers: one with identity 3, one with
   * identity 5. */
  /* This one showed up in 2 digests. */
  rs = smartlist_get(con->routerstatus_list, 0);
  test_memeq(rs->identity_digest,
             "\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3",
             DIGEST_LEN);
  test_memeq(rs->descriptor_digest, "NNNNNNNNNNNNNNNNNNNN", DIGEST_LEN);
  test_assert(!rs->is_authority);
  test_assert(!rs->is_exit);
  test_assert(!rs->is_fast);
  test_assert(!rs->is_possible_guard);
  test_assert(!rs->is_stable);
  test_assert(!rs->is_running);
  test_assert(!rs->is_v2_dir);
  test_assert(!rs->is_valid);
  test_assert(!rs->is_named);
  /* XXXX020 check version */

  rs = smartlist_get(con->routerstatus_list, 1);
  /* This one showed up in 3 digests. Twice with ID 'M', once with 'Z'.  */
  test_memeq(rs->identity_digest,
             "\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5\x5",
             DIGEST_LEN);
  test_streq(rs->nickname, "router1");
  test_memeq(rs->descriptor_digest, "MMMMMMMMMMMMMMMMMMMM", DIGEST_LEN);
  test_eq(rs->published_on, now-1000);
  test_eq(rs->addr, 0x99009901);
  test_eq(rs->or_port, 443);
  test_eq(rs->dir_port, 0);
  test_assert(!rs->is_authority);
  test_assert(rs->is_exit);
  test_assert(rs->is_fast);
  test_assert(rs->is_possible_guard);
  test_assert(rs->is_stable);
  test_assert(rs->is_running);
  test_assert(rs->is_v2_dir);
  test_assert(rs->is_valid);
  test_assert(!rs->is_named);
  /* XXXX020 check version */

  /* Check signatures.  the first voter hasn't got one.  The second one
   * does: validate it. */
  voter = smartlist_get(con->voters, 0);
  test_assert(!voter->signature);
  test_assert(!voter->good_signature);
  test_assert(!voter->bad_signature);

  voter = smartlist_get(con->voters, 1);
  test_assert(voter->signature);
  test_assert(!voter->good_signature);
  test_assert(!voter->bad_signature);
  test_assert(!networkstatus_check_voter_signature(con,
                                               smartlist_get(con->voters, 1),
                                               cert3));
  test_assert(voter->signature);
  test_assert(voter->good_signature);
  test_assert(!voter->bad_signature);

  {
    char *consensus_text2, *consensus_text3;
    networkstatus_vote_t *con2, *con3;
    char *detached_text1, *detached_text2;
    ns_detached_signatures_t *dsig1, *dsig2;
    const char *msg=NULL;
    /* Compute the other two signed consensuses. */
    smartlist_shuffle(votes);
    consensus_text2 = networkstatus_compute_consensus(votes, 3,
                                                      cert2->identity_key,
                                                      sign_skey_2);
    smartlist_shuffle(votes);
    consensus_text3 = networkstatus_compute_consensus(votes, 3,
                                                      cert1->identity_key,
                                                      sign_skey_1);
    test_assert(consensus_text2);
    test_assert(consensus_text3);
    con2 = networkstatus_parse_vote_from_string(consensus_text2, NULL, 0);
    con3 = networkstatus_parse_vote_from_string(consensus_text3, NULL, 0);
    test_assert(con2);
    test_assert(con3);

    /* All three should have the same digest. */
    test_memeq(con->networkstatus_digest, con2->networkstatus_digest,
               DIGEST_LEN);
    test_memeq(con->networkstatus_digest, con3->networkstatus_digest,
               DIGEST_LEN);

    /* Extract a detached signature from con3. */
    detached_text1 = networkstatus_get_detached_signatures(con3);
    tor_assert(detached_text1);
    /* Try to parse it. */
    dsig1 = networkstatus_parse_detached_signatures(detached_text1, NULL);
    tor_assert(dsig1);

    /* Are parsed values as expected? */
    test_eq(dsig1->valid_after, con3->valid_after);
    test_eq(dsig1->fresh_until, con3->fresh_until);
    test_eq(dsig1->valid_until, con3->valid_until);
    test_memeq(dsig1->networkstatus_digest, con3->networkstatus_digest,
               DIGEST_LEN);
    test_eq(1, smartlist_len(dsig1->signatures));
    voter = smartlist_get(dsig1->signatures, 0);
    test_memeq(voter->identity_digest, cert1->cache_info.identity_digest,
               DIGEST_LEN);

    /* Try adding it to con2. */
    detached_text2 = networkstatus_get_detached_signatures(con2);
    test_eq(1, networkstatus_add_detached_signatures(con2, dsig1, &msg));
    tor_free(detached_text2);
    detached_text2 = networkstatus_get_detached_signatures(con2);
    //printf("\n<%s>\n", detached_text2);
    dsig2 = networkstatus_parse_detached_signatures(detached_text2, NULL);
    test_assert(dsig2);
    /*
    printf("\n");
    SMARTLIST_FOREACH(dsig2->signatures, networkstatus_voter_info_t *, vi, {
        char hd[64];
        base16_encode(hd, sizeof(hd), vi->identity_digest, DIGEST_LEN);
        printf("%s\n", hd);
      });
    */
    test_eq(2, smartlist_len(dsig2->signatures));

    /* Try adding to con2 twice; verify that nothing changes. */
    test_eq(0, networkstatus_add_detached_signatures(con2, dsig1, &msg));

    /* Add to con. */
    test_eq(2, networkstatus_add_detached_signatures(con, dsig2, &msg));
    /* Check signatures */
    test_assert(!networkstatus_check_voter_signature(con,
                                               smartlist_get(con->voters, 0),
                                               cert2));
    test_assert(!networkstatus_check_voter_signature(con,
                                               smartlist_get(con->voters, 2),
                                               cert1));

    networkstatus_vote_free(con2);
    networkstatus_vote_free(con3);
    tor_free(consensus_text2);
    tor_free(consensus_text3);
    tor_free(detached_text1);
    tor_free(detached_text2);
    ns_detached_signatures_free(dsig1);
    ns_detached_signatures_free(dsig2);
  }

  smartlist_free(votes);
  tor_free(v1_text);
  tor_free(v2_text);
  tor_free(v3_text);
  tor_free(consensus_text);
  networkstatus_vote_free(vote);
  networkstatus_vote_free(v1);
  networkstatus_vote_free(v2);
  networkstatus_vote_free(v3);
  networkstatus_vote_free(con);
  crypto_free_pk_env(sign_skey_1);
  crypto_free_pk_env(sign_skey_2);
  crypto_free_pk_env(sign_skey_3);
  authority_cert_free(cert1);
  authority_cert_free(cert2);
  authority_cert_free(cert3);
}

static void
test_policies(void)
{
  addr_policy_t *policy, *policy2;
  tor_addr_t tar;
  config_line_t line;

  policy = router_parse_addr_policy_from_string("reject 192.168.0.0/16:*",-1);
  test_eq(NULL, policy->next);
  test_eq(ADDR_POLICY_REJECT, policy->policy_type);
  tor_addr_from_ipv4(&tar, 0xc0a80000u);
  test_assert(policy->addr == 0xc0a80000u);
  test_eq(16, policy->maskbits);
  test_eq(1, policy->prt_min);
  test_eq(65535, policy->prt_max);
  test_streq("reject 192.168.0.0/16:*", policy->string);

  test_assert(ADDR_POLICY_ACCEPTED ==
          compare_addr_to_addr_policy(0x01020304u, 2, policy));
  test_assert(ADDR_POLICY_PROBABLY_ACCEPTED ==
          compare_addr_to_addr_policy(0, 2, policy));
  test_assert(ADDR_POLICY_REJECTED ==
          compare_addr_to_addr_policy(0xc0a80102, 2, policy));

  policy2 = NULL;
  test_assert(0 == policies_parse_exit_policy(NULL, &policy2, 1));
  test_assert(policy2);

  test_assert(!exit_policy_is_general_exit(policy));
  test_assert(exit_policy_is_general_exit(policy2));

  test_assert(cmp_addr_policies(policy, policy2));
  test_assert(!cmp_addr_policies(policy2, policy2));

  test_assert(!policy_is_reject_star(policy2));
  test_assert(policy_is_reject_star(policy));

  addr_policy_free(policy);
  addr_policy_free(policy2);

  /* make sure compacting logic works. */
  policy = NULL;
  line.key = (char*)"foo";
  line.value = (char*)"accept *:80,reject private:*,reject *:*";
  line.next = NULL;
  test_assert(0 == policies_parse_exit_policy(&line, &policy, 0));
  test_assert(policy);
  test_streq(policy->string, "accept *:80");
  test_streq(policy->next->string, "reject *:*");
  test_eq_ptr(policy->next->next, NULL);

  addr_policy_free(policy);
}

static void
test_rend_fns(void)
{
  char address1[] = "fooaddress.onion";
  char address2[] = "aaaaaaaaaaaaaaaa.onion";
  char address3[] = "fooaddress.exit";
  char address4[] = "tor.eff.org";
  rend_service_descriptor_t *d1, *d2;
  char *encoded;
  size_t len;
  crypto_pk_env_t *pk1, *pk2;
  time_t now;
  pk1 = pk_generate(0);
  pk2 = pk_generate(1);

  /* Test unversioned (v0) descriptor */
  d1 = tor_malloc_zero(sizeof(rend_service_descriptor_t));
  d1->pk = crypto_pk_dup_key(pk1);
  now = time(NULL);
  d1->timestamp = now;
  d1->n_intro_points = 3;
  d1->version = 0;
  d1->intro_points = tor_malloc(sizeof(char*)*3);
  d1->intro_points[0] = tor_strdup("tom");
  d1->intro_points[1] = tor_strdup("crow");
  d1->intro_points[2] = tor_strdup("joel");
  test_assert(! rend_encode_service_descriptor(d1, pk1, &encoded, &len));
  d2 = rend_parse_service_descriptor(encoded, len);
  test_assert(d2);

  test_assert(!crypto_pk_cmp_keys(d1->pk, d2->pk));
  test_eq(d2->timestamp, now);
  test_eq(d2->version, 0);
  test_eq(d2->protocols, 1<<2);
  test_eq(d2->n_intro_points, 3);
  test_streq(d2->intro_points[0], "tom");
  test_streq(d2->intro_points[1], "crow");
  test_streq(d2->intro_points[2], "joel");
  test_eq(NULL, d2->intro_point_extend_info);

  rend_service_descriptor_free(d1);
  rend_service_descriptor_free(d2);
  tor_free(encoded);

  test_assert(BAD_HOSTNAME == parse_extended_hostname(address1));
  test_assert(ONION_HOSTNAME == parse_extended_hostname(address2));
  test_assert(EXIT_HOSTNAME == parse_extended_hostname(address3));
  test_assert(NORMAL_HOSTNAME == parse_extended_hostname(address4));

  crypto_free_pk_env(pk1);
  crypto_free_pk_env(pk2);
}

static void
bench_aes(void)
{
  int len, i;
  char *b1, *b2;
  crypto_cipher_env_t *c;
  struct timeval start, end;
  const int iters = 100000;
  uint64_t nsec;
  c = crypto_new_cipher_env();
  crypto_cipher_generate_key(c);
  crypto_cipher_encrypt_init_cipher(c);
  for (len = 1; len <= 8192; len *= 2) {
    b1 = tor_malloc_zero(len);
    b2 = tor_malloc_zero(len);
    tor_gettimeofday(&start);
    for (i = 0; i < iters; ++i) {
      crypto_cipher_encrypt(c, b1, b2, len);
    }
    tor_gettimeofday(&end);
    tor_free(b1);
    tor_free(b2);
    nsec = (uint64_t) tv_udiff(&start,&end);
    nsec *= 1000;
    nsec /= (iters*len);
    printf("%d bytes: "U64_FORMAT" nsec per byte\n", len,
           U64_PRINTF_ARG(nsec));
  }
  crypto_free_cipher_env(c);
}

static void
test_util_mempool(void)
{
  mp_pool_t *pool;
  smartlist_t *allocated;
  int i;

  pool = mp_pool_new(1, 100);
  test_assert(pool->new_chunk_capacity >= 100);
  test_assert(pool->item_alloc_size >= sizeof(void*)+1);
  mp_pool_destroy(pool);

  pool = mp_pool_new(241, 2500);
  test_assert(pool->new_chunk_capacity >= 10);
  test_assert(pool->item_alloc_size >= sizeof(void*)+241);
  test_eq(pool->item_alloc_size & 0x03, 0);
  test_assert(pool->new_chunk_capacity < 60);

  allocated = smartlist_create();
  for (i = 0; i < 100000; ++i) {
    if (smartlist_len(allocated) < 20 || crypto_rand_int(2)) {
      void *m = mp_pool_get(pool);
      memset(m, 0x09, 241);
      smartlist_add(allocated, m);
      //printf("%d: %p\n", i, m);
      //mp_pool_assert_ok(pool);
    } else {
      int idx = crypto_rand_int(smartlist_len(allocated));
      void *m = smartlist_get(allocated, idx);
      //printf("%d: free %p\n", i, m);
      smartlist_del(allocated, idx);
      mp_pool_release(m);
      //mp_pool_assert_ok(pool);
    }
    if (crypto_rand_int(777)==0)
      mp_pool_clean(pool, -1);

    if (i % 777)
      mp_pool_assert_ok(pool);
  }
  SMARTLIST_FOREACH(allocated, void *, m, mp_pool_release(m));
  mp_pool_assert_ok(pool);
  mp_pool_clean(pool, 0);
  mp_pool_assert_ok(pool);
  mp_pool_destroy(pool);
  smartlist_free(allocated);
}

static void
test_util_datadir(void)
{
  char buf[1024];
  char *f;

  f = get_datadir_fname(NULL);
  test_streq(f, temp_dir);
  tor_free(f);
  f = get_datadir_fname("state");
  tor_snprintf(buf, sizeof(buf), "%s"PATH_SEPARATOR"state", temp_dir);
  test_streq(f, buf);
  tor_free(f);
  f = get_datadir_fname2("cache", "thingy");
  tor_snprintf(buf, sizeof(buf),
               "%s"PATH_SEPARATOR"cache"PATH_SEPARATOR"thingy", temp_dir);
  test_streq(f, buf);
  tor_free(f);
  f = get_datadir_fname2_suffix("cache", "thingy", ".foo");
  tor_snprintf(buf, sizeof(buf),
               "%s"PATH_SEPARATOR"cache"PATH_SEPARATOR"thingy.foo", temp_dir);
  test_streq(f, buf);
  tor_free(f);
  f = get_datadir_fname_suffix("cache", ".foo");
  tor_snprintf(buf, sizeof(buf), "%s"PATH_SEPARATOR"cache.foo",
               temp_dir);
  test_streq(f, buf);
  tor_free(f);
}

/* Test AES-CTR encryption and decryption with IV. */
static void
test_crypto_aes_iv(void)
{
  crypto_cipher_env_t *cipher;
  char *plain, *encrypted1, *encrypted2, *decrypted1, *decrypted2;
  char plain_1[1], plain_15[15], plain_16[16], plain_17[17];
  char key1[16], key2[16];
  size_t encrypted_size, decrypted_size;
  plain = tor_malloc(4095);
  encrypted1 = tor_malloc(4095 + 1 + 16);
  encrypted2 = tor_malloc(4095 + 1 + 16);
  decrypted1 = tor_malloc(4095 + 1);
  decrypted2 = tor_malloc(4095 + 1);
  crypto_rand(plain, 4095);
  crypto_rand(key1, 16);
  crypto_rand(key2, 16);
  crypto_rand(plain_1, 1);
  crypto_rand(plain_15, 15);
  crypto_rand(plain_16, 16);
  crypto_rand(plain_17, 17);
  key1[0] = key2[0] + 128; /* Make sure that contents are different. */
  /* Encrypt and decrypt with the same key. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 4095,
                                             plain, 4095);
  crypto_free_cipher_env(cipher);
  test_eq(encrypted_size, 16 + 4095);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 4095,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_eq(decrypted_size, 4095);
  test_memeq(plain, decrypted1, 4095);
  /* Encrypt a second time (with a new random initialization vector). */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted2, 16 + 4095,
                                             plain, 4095);
  crypto_free_cipher_env(cipher);
  test_eq(encrypted_size, 16 + 4095);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted2, 4095,
                                             encrypted2, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_eq(decrypted_size, 4095);
  test_memeq(plain, decrypted2, 4095);
  test_memneq(encrypted1, encrypted2, encrypted_size);
  /* Decrypt with the wrong key. */
  cipher = crypto_create_init_cipher(key2, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted2, 4095,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_memneq(plain, decrypted2, encrypted_size);
  /* Alter the initialization vector. */
  encrypted1[0] += 42;
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 4095,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_memneq(plain, decrypted2, 4095);
  /* Special length case: 1. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 1,
                                             plain_1, 1);
  crypto_free_cipher_env(cipher);
  test_eq(encrypted_size, 16 + 1);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 1,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_eq(decrypted_size, 1);
  test_memeq(plain_1, decrypted1, 1);
  /* Special length case: 15. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 15,
                                             plain_15, 15);
  crypto_free_cipher_env(cipher);
  test_eq(encrypted_size, 16 + 15);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 15,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_eq(decrypted_size, 15);
  test_memeq(plain_15, decrypted1, 15);
  /* Special length case: 16. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 16,
                                             plain_16, 16);
  crypto_free_cipher_env(cipher);
  test_eq(encrypted_size, 16 + 16);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 16,
                                             encrypted1, encrypted_size);
  test_eq(decrypted_size, 16);
  test_memeq(plain_16, decrypted1, 16);
  /* Special length case: 17. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 17,
                                             plain_17, 17);
  crypto_free_cipher_env(cipher);
  test_eq(encrypted_size, 16 + 17);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 17,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  test_eq(decrypted_size, 17);
  test_memeq(plain_17, decrypted1, 17);
  /* Free memory. */
  tor_free(plain);
  tor_free(encrypted1);
  tor_free(encrypted2);
  tor_free(decrypted1);
  tor_free(decrypted2);
}

/* Test base32 decoding. */
static void
test_crypto_base32_decode(void)
{
  char plain[60], encoded[96 + 1], decoded[60];
  int res;
  crypto_rand(plain, 60);
  /* Encode and decode a random string. */
  base32_encode(encoded, 96 + 1, plain, 60);
  res = base32_decode(decoded, 60, encoded, 96);
  test_eq(res, 0);
  test_memeq(plain, decoded, 60);
  /* Encode, uppercase, and decode a random string. */
  base32_encode(encoded, 96 + 1, plain, 60);
  tor_strupper(encoded);
  res = base32_decode(decoded, 60, encoded, 96);
  test_eq(res, 0);
  test_memeq(plain, decoded, 60);
  /* Change encoded string and decode. */
  if (encoded[0] == 'A' || encoded[0] == 'a')
    encoded[0] = 'B';
  else
    encoded[0] = 'A';
  res = base32_decode(decoded, 60, encoded, 96);
  test_eq(res, 0);
  test_memneq(plain, decoded, 60);
}

/* Test encoding and parsing of v2 rendezvous service descriptors. */
static void
test_rend_fns_v2(void)
{
  rend_service_descriptor_t *generated, *parsed;
  char service_id[DIGEST_LEN];
  char service_id_base32[REND_SERVICE_ID_LEN+1];
  const char *next_desc;
  smartlist_t *desc_strs = smartlist_create();
  smartlist_t *desc_ids = smartlist_create();
  char computed_desc_id[DIGEST_LEN];
  char parsed_desc_id[DIGEST_LEN];
  crypto_pk_env_t *pk1, *pk2;
  time_t now;
  char *intro_points_encrypted;
  size_t intro_points_size;
  size_t encoded_size;
  int i;
  pk1 = pk_generate(0);
  pk2 = pk_generate(1);
  generated = tor_malloc_zero(sizeof(rend_service_descriptor_t));
  generated->pk = crypto_pk_dup_key(pk1);
  crypto_pk_get_digest(generated->pk, service_id);
  base32_encode(service_id_base32, REND_SERVICE_ID_LEN+1, service_id, 10);
  now = time(NULL);
  generated->timestamp = now;
  generated->n_intro_points = 3;
  generated->version = 2;
  generated->protocols = 42;
  generated->intro_point_extend_info =
    tor_malloc_zero(sizeof(extend_info_t *) * generated->n_intro_points);
  generated->intro_points =
    tor_malloc_zero(sizeof(char *) * generated->n_intro_points);
  generated->intro_keys = strmap_new();
  for (i = 0; i < generated->n_intro_points; i++) {
    extend_info_t *info = tor_malloc_zero(sizeof(extend_info_t));
    crypto_pk_env_t *okey = pk_generate(2 + i);
    info->onion_key = crypto_pk_dup_key(okey);
    crypto_pk_get_digest(info->onion_key, info->identity_digest);
    //crypto_rand(info->identity_digest, DIGEST_LEN); /* Would this work? */
    info->nickname[0] = '$';
    base16_encode(info->nickname + 1, sizeof(info->nickname) - 1,
                  info->identity_digest, DIGEST_LEN);
    info->addr = crypto_rand_int(65536); /* Does not cover all IP addresses. */
    info->port = crypto_rand_int(65536);
    generated->intro_points[i] = tor_strdup(info->nickname);
    generated->intro_point_extend_info[i] = info;
    strmap_set(generated->intro_keys, info->nickname, pk2);
  }
  test_assert(rend_encode_v2_descriptors(desc_strs, desc_ids, generated, now,
                                         NULL, 0) > 0);
  test_assert(rend_compute_v2_desc_id(computed_desc_id, service_id_base32,
                                      NULL, now, 0) == 0);
  test_assert(smartlist_digest_isin(desc_ids, computed_desc_id));
  test_assert(rend_parse_v2_service_descriptor(&parsed, parsed_desc_id,
                                               &intro_points_encrypted,
                                               &intro_points_size,
                                               &encoded_size,
                                               &next_desc,
                                               desc_strs->list[0]) == 0);
  test_assert(parsed);
  test_assert(smartlist_digest_isin(desc_ids, parsed_desc_id));
  test_assert(rend_decrypt_introduction_points(parsed, NULL,
                                               intro_points_encrypted,
                                               intro_points_size) == 3);
  test_assert(!crypto_pk_cmp_keys(generated->pk, parsed->pk));
  test_eq(parsed->timestamp, now);
  test_eq(parsed->version, 2);
  test_eq(parsed->protocols, 42);
  test_eq(parsed->n_intro_points, 3);
  for (i = 0; i < parsed->n_intro_points; i++) {
    extend_info_t *par_info = parsed->intro_point_extend_info[i];
    extend_info_t *gen_info = generated->intro_point_extend_info[i];
    test_assert(!crypto_pk_cmp_keys(gen_info->onion_key, par_info->onion_key));
    test_memeq(gen_info->identity_digest, par_info->identity_digest,
               DIGEST_LEN);
    test_streq(gen_info->nickname, par_info->nickname);
    test_streq(generated->intro_points[i], parsed->intro_points[i]);
    test_eq(gen_info->addr, par_info->addr);
    test_eq(gen_info->port, par_info->port);
  }
  tor_free(intro_points_encrypted);
  /*for (i = 0; i < REND_NUMBER_OF_NON_CONSECUTIVE_REPLICAS; i++)
    tor_free(desc_strs[i]);*/
  smartlist_free(desc_strs);
  for (i = 0; i < parsed->n_intro_points; i++) {
    tor_free(parsed->intro_point_extend_info[i]->onion_key);
    tor_free(generated->intro_point_extend_info[i]->onion_key);
  }
}

#define ENT(x) { #x, test_ ## x, 0, 0 }
#define SUBENT(x,y) { #x "/" #y, test_ ## x ## _ ## y, 1, 0 }

static struct {
  const char *test_name;
  void (*test_fn)(void);
  int is_subent;
  int selected;
} test_array[] = {
  ENT(buffers),
  ENT(crypto),
  SUBENT(crypto, dh),
  SUBENT(crypto, s2k),
  SUBENT(crypto, aes_iv),
  SUBENT(crypto, base32_decode),
  ENT(util),
  SUBENT(util, ip6_helpers),
  SUBENT(util, gzip),
  SUBENT(util, datadir),
  SUBENT(util, smartlist),
  SUBENT(util, bitarray),
  SUBENT(util, mempool),
  SUBENT(util, strmap),
  SUBENT(util, control_formats),
  SUBENT(util, pqueue),
  SUBENT(util, mmap),
  SUBENT(util, threads),
  SUBENT(util, order_functions),
  ENT(onion_handshake),
  ENT(dir_format),
  ENT(v3_networkstatus),
  ENT(policies),
  ENT(rend_fns),
  SUBENT(rend_fns, v2),
  { NULL, NULL, 0, 0 },
};

static void syntax(void) ATTR_NORETURN;
static void
syntax(void)
{
  int i;
  printf("Syntax:\n"
         "  test [-v|--verbose] [--warn|--notice|--info|--debug]\n"
         "       [testname...]\n"
         "Recognized tests are:\n");
  for (i = 0; test_array[i].test_name; ++i) {
    printf("   %s\n", test_array[i].test_name);
  }

  exit(0);
}

int
main(int c, char**v)
{
  or_options_t *options = options_new();
  char *errmsg = NULL;
  int i;
  int verbose = 0, any_selected = 0;
  int loglevel = LOG_ERR;

  for (i = 1; i < c; ++i) {
    if (!strcmp(v[i], "-v") || !strcmp(v[i], "--verbose"))
      verbose++;
    else if (!strcmp(v[i], "--warn"))
      loglevel = LOG_WARN;
    else if (!strcmp(v[i], "--notice"))
      loglevel = LOG_NOTICE;
    else if (!strcmp(v[i], "--info"))
      loglevel = LOG_INFO;
    else if (!strcmp(v[i], "--debug"))
      loglevel = LOG_DEBUG;
    else if (!strcmp(v[i], "--help") || !strcmp(v[i], "-h") || v[i][0] == '-')
      syntax();
    else {
      int j, found=0;
      for (j = 0; test_array[j].test_name; ++j) {
        if (!strcmp(v[i], test_array[j].test_name) ||
            (test_array[j].is_subent &&
             !strcmpstart(test_array[j].test_name, v[i]) &&
             test_array[j].test_name[strlen(v[i])] == '/') ||
            (v[i][0] == '=' && !strcmp(v[i]+1, test_array[j].test_name))) {
          test_array[j].selected = 1;
          any_selected = 1;
          found = 1;
        }
      }
      if (!found) {
        printf("Unknown test: %s\n", v[i]);
        syntax();
      }
    }
  }

  if (!any_selected) {
    for (i = 0; test_array[i].test_name; ++i) {
      test_array[i].selected = 1;
    }
  }

  add_stream_log(loglevel, LOG_ERR, "", stdout);

  options->command = CMD_RUN_UNITTESTS;
  rep_hist_init();
  network_init();
  setup_directory();
  options_init(options);
  options->DataDirectory = tor_strdup(temp_dir);
  if (set_options(options, &errmsg) < 0) {
    printf("Failed to set initial options: %s\n", errmsg);
    tor_free(errmsg);
    return 1;
  }

  crypto_seed_rng();

  if (0) {
    bench_aes();
    return 0;
  }

  atexit(remove_directory);

  printf("Running Tor unit tests on %s\n", get_uname());

  for (i = 0; test_array[i].test_name; ++i) {
    if (!test_array[i].selected)
      continue;
    if (!test_array[i].is_subent) {
      printf("\n============================== %s\n",test_array[i].test_name);
    } else if (test_array[i].is_subent && verbose) {
      printf("\n%s", test_array[i].test_name);
    }
    test_array[i].test_fn();
  }
  puts("");

  if (have_failed)
    return 1;
  else
    return 0;
}

