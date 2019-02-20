

#ifndef EC_H
#define EC_H

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>
#include <isa-l.h>

typedef struct _ec_prealloc {
  uint8_t *encode_matrix;
  uint8_t *encode_g_tbls;

} EC_Prealloc_t;

typedef struct _ec_const {
    
} EC_Const_t;

EC_Prealloc_t *ec_alloc(uint8_t n, uint8_t k);
void ec_dealloc(EC_Prealloc_t *ec_prealloc);
int encode_aux(uint8_t n, uint8_t k, uint8_t **src_and_parity, uint64_t buf_len, EC_Prealloc_t *ec_prealloc);
int decode_aux(uint8_t n, uint8_t k, uint8_t **src_and_parity, uint64_t buf_len, bool *available_info, EC_Prealloc_t *ec_prealloc);

#endif
