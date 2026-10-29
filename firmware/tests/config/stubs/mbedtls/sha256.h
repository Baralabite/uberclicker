#pragma once
#include <openssl/sha.h>
inline int mbedtls_sha256(const unsigned char *p,size_t n,unsigned char *out,int){SHA256(p,n,out);return 0;}
