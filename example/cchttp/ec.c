

#include "Protocol.h"
#include "ec.h"

extern TSTextLogObject protocol_plugin_log;
extern int txn_data_ind;
extern EcPeer ec_peers[MAX_EC_NODES];
extern int EC_n, EC_k, EC_x;
extern int n_peers;
extern int current_node_EC_index;
extern int ssn_data_ind, txn_data_ind;

EC_Prealloc_t *
ec_alloc(uint8_t n, uint8_t k)
{
  EC_Prealloc_t *ec_prealloc = (EC_Prealloc_t *)malloc(sizeof(EC_Prealloc_t));

  ec_prealloc->encode_matrix = (uint8_t *)malloc(n * k);
  gf_gen_cauchy1_matrix(ec_prealloc->encode_matrix, n, k);
  ec_prealloc->encode_g_tbls = (uint8_t *)malloc(k * (n - k) * 32);
  ec_init_tables(k, n - k, &(ec_prealloc->encode_matrix[k * k]), ec_prealloc->encode_g_tbls);

  return ec_prealloc;
}

void
ec_dealloc(EC_Prealloc_t *ec_prealloc)
{
  free(ec_prealloc->encode_matrix);
  free(ec_prealloc->encode_g_tbls);
  free(ec_prealloc);
}

TSReturnCode
encode_aux(uint8_t n, uint8_t k, uint8_t **src_and_parity, uint64_t buf_len, EC_Prealloc_t *ec_prealloc)
{
  // Generate EC parity blocks fron sources
  ec_encode_data(buf_len, k, n - k, ec_prealloc->encode_g_tbls, src_and_parity, &src_and_parity[k]);

  return TS_SUCCESS;
}

TSReturnCode
decode_aux(uint8_t n, uint8_t k, uint8_t **src_and_parity, uint64_t buf_len, bool *available_info, EC_Prealloc_t *ec_prealloc)
{
  uint8_t i, j, p, r, s;
  uint8_t n_src_missing = 0, n_parity_missing = 0;

  uint8_t *decode_index  = (uint8_t *)malloc(sizeof(uint8_t) * k);
  uint8_t **recover_srcs = (uint8_t **)malloc(sizeof(uint8_t *) * k);
  uint8_t **recover_outp = (uint8_t **)malloc(sizeof(uint8_t *) * (n - k));

  // Allocate buffers for recovered data
  for (i = 0; i < n - k; i++) {
    if (NULL == (recover_outp[i] = (uint8_t *)malloc(buf_len))) {
      printf("alloc error: Fail\n");
      return -1;
    }
  }

  uint8_t *decode_matrix = (uint8_t *)malloc(n * k);
  uint8_t *invert_matrix = (uint8_t *)malloc(n * k);
  uint8_t *temp_matrix   = (uint8_t *)malloc(n * k);

  uint8_t *g_tbls = (uint8_t *)malloc(k * (n - k) * 32);
  // gf_gen_cauchy1_matrix(ec_prealloc->encode_matrix, n, k);
  // ec_init_tables(k, n-k, &encode_matrix[k * k], g_tbls);

  if (ec_prealloc->encode_matrix == NULL || decode_matrix == NULL || invert_matrix == NULL || temp_matrix == NULL ||
      g_tbls == NULL) {
    printf("Test failure! Error with malloc\n");
    return -1;
  }

  uint8_t *missing_list = (uint8_t *)malloc(sizeof(uint8_t) * (n - k));

  // count the number of src and parity err
  for (i = 0, j = 0; i < n; i++) {
    if (!available_info[i]) {
      missing_list[j++] = i;
      if (i < k)
        n_src_missing++;
      else
        n_parity_missing++;
    }
  }

  // cal index of the new array
  for (i = 0, r = 0; i < k; i++, r++) {
    while (!available_info[r])
      // ignore the one that are err
      r++;
    for (j = 0; j < k; j++)
      temp_matrix[i * k + j] = ec_prealloc->encode_matrix[r * k + j];
    // decode_index stores the index of corret source and parity, it has k elements
    decode_index[i] = r;
  }

  // Invert matrix to get recovery matrix
  if (gf_invert_matrix(temp_matrix, invert_matrix, k) < 0)
    return -1;

  // Get decode matrix with only wanted recovery rows, only p*k are used
  for (i = 0; i < n - k; i++) {
    if (missing_list[i] < k) // A src err
      for (j = 0; j < k; j++) {
        decode_matrix[k * i + j] = invert_matrix[k * missing_list[i] + j];
      }
    else if (missing_list[i] >= k) {
      for (p = 0; p < k; p++) {
        s = 0;
        for (j = 0; j < k; j++)
          s ^= gf_mul(invert_matrix[j * k + p], ec_prealloc->encode_matrix[k * missing_list[i] + j]);
        decode_matrix[k * i + p] = s;
      }
    } else {
      printf("error\n");
      return -1;
    }
  }

  // Pack recovery array pointers as list of valid fragments
  for (i = 0; i < k; i++)
    recover_srcs[i] = src_and_parity[decode_index[i]];

  // Recover data
  ec_init_tables(k, n - k, decode_matrix, g_tbls);
  ec_encode_data(buf_len, k, n - k, g_tbls, recover_srcs, recover_outp);

  // Check that recovered buffers are the same as original
  for (i = 0; i < n - k; i++) {
    // printf(" %d", missing_list[i]);
    if (memcmp(recover_outp[i], src_and_parity[missing_list[i]], buf_len)) {
      printf(" Fail erasure recovery %d, missing %d\n", i, missing_list[i]);
      return -1;
    }
  }
  printf("data verified\n");
  return TS_SUCCESS;
}

