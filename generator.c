/* Layer2 contract generator
 *
 * The generator supposed to be run off-chain.
 * generator dynamic linking with the layer2 contract code,
 * and provides layer2 syscalls.
 *
 * A program should be able to generate a post state after run the generator,
 * and should be able to use the states to construct a transaction that satifies
 * the validator.
 */
#include "ckb_syscalls.h"
#include "common.h"
#include "gw_def.h"
#include "gw_dlfcn.h"
#include "polyjuice.h"

/* syscalls */
#define GW_SYS_STORE 3051
#define GW_SYS_LOAD 3052
#define GW_SYS_SET_RETURN_DATA 3061
/* internal syscall only for generator */
#define GW_SYS_LOAD_CALLCONTEXT 4051
#define GW_SYS_LOAD_BLOCKINFO 4052
#define GW_SYS_LOAD_PROGRAM_AS_DATA 4061
#define GW_SYS_LOAD_PROGRAM_AS_CODE 4062

/* 128KB */
#define CALL_CONTEXT_LEN 131072
#define BLOCK_INFO_LEN 128

int sys_load(void *ctx, const uint8_t key[GW_KEY_BYTES],
             uint8_t value[GW_VALUE_BYTES]) {
  gw_context_t *gw_ctx = (gw_context_t *)ctx;
  if (gw_ctx == NULL) {
    return GW_ERROR_INVALID_CONTEXT;
  }
  uint8_t raw_key[GW_KEY_BYTES];
  gw_build_account_key(gw_ctx->call_context.to_id, key, raw_key);
  return syscall(GW_SYS_LOAD, raw_key, value, 0, 0, 0, 0);
}
int sys_store(void *ctx, const uint8_t key[GW_KEY_BYTES],
              const uint8_t value[GW_VALUE_BYTES]) {
  gw_context_t *gw_ctx = (gw_context_t *)ctx;
  if (gw_ctx == NULL) {
    return GW_ERROR_INVALID_CONTEXT;
  }
  uint8_t raw_key[GW_KEY_BYTES];
  gw_build_account_key(gw_ctx->call_context.to_id, key, raw_key);
  return syscall(GW_SYS_STORE, raw_key, value, 0, 0, 0, 0);
}

/* set call return data */
int sys_set_program_return_data(void *ctx, uint8_t *data, uint32_t len) {
  gw_context_t *gw_ctx = (gw_context_t *)ctx;
  if (gw_ctx == NULL || gw_ctx->sys_context == NULL) {
    return GW_ERROR_INVALID_CONTEXT;
  }
  if (len > GW_MAX_RETURN_DATA_SIZE) {
    return GW_ERROR_INVALID_DATA;
  }
  gw_call_receipt_t *receipt = (gw_call_receipt_t *)gw_ctx->sys_context;
  receipt->return_data_len = len;
  memcpy(receipt->return_data, data, len);
  return 0;
}

/* set program return data */
int _set_program_return_data(uint8_t *data, uint32_t len) {
  return syscall(GW_SYS_SET_RETURN_DATA, data, len, 0, 0, 0, 0);
}

int _sys_load_call_context(void *addr, uint64_t *len) {
  volatile uint64_t inner_len = *len;
  int ret = syscall(GW_SYS_LOAD_CALLCONTEXT, addr, &inner_len, 0, 0, 0, 0);
  *len = inner_len;
  return ret;
}

int _sys_load_block_info(void *addr, uint64_t *len) {
  volatile uint64_t inner_len = *len;
  int ret = syscall(GW_SYS_LOAD_BLOCKINFO, addr, &inner_len, 0, 0, 0, 0);
  *len = inner_len;
  return ret;
}

int _sys_load_program_as_data(void *addr, uint64_t *len, size_t offset,
                              uint64_t id) {
  volatile uint64_t inner_len = *len;
  int ret =
      syscall(GW_SYS_LOAD_PROGRAM_AS_DATA, addr, &inner_len, offset, id, 0, 0);
  *len = inner_len;
  return ret;
}

int _sys_load_program_as_code(void *addr, uint64_t memory_size,
                              uint64_t content_offset, uint64_t content_size,
                              uint64_t id) {
  return syscall(GW_SYS_LOAD_PROGRAM_AS_CODE, addr, memory_size, content_offset,
                 content_size, id, 0);
}

int invoke_contract_func(gw_context_t *ctx, void *handle) {
  if (ctx == NULL) {
    return GW_ERROR_INVALID_CONTEXT;
  }
  uint8_t call_type = ctx->call_context.call_type;
  const char *gw_func_name = GW_HANDLE_MESSAGE_FUNC;
  if (call_type != GW_CALL_TYPE_HANDLE_MESSAGE) {
    gw_func_name = GW_CONSTRUCT_FUNC;
  }

  gw_contract_fn contract_func;
  *(void **)(&contract_func) = ckb_dlsym(handle, gw_func_name);
  if (contract_func == NULL) {
    return GW_ERROR_DYNAMIC_LINKING;
  }

  /* run contract */
  int ret = contract_func(ctx);

  if (ret != 0) {
    return ret;
  }
  return 0;
}

int invoke_polyjuice_contract_func(gw_context_t *ctx) {
  if (ctx->call_context.call_type == GW_CALL_TYPE_CONSTRUCT) {
    return gw_construct(ctx);
  } else if (ctx->call_context.call_type == GW_CALL_TYPE_HANDLE_MESSAGE) {
    return gw_handle_message(ctx);
  } else {
    /* ERROR: invalid call_type */
    return -1;
  }
}

int sys_call(void *ctx, uint32_t to_id, uint8_t *args, uint32_t args_len,
             gw_call_receipt_t *receipt) {
  if (ctx == NULL) {
    return GW_ERROR_INVALID_CONTEXT;
  }
  int ret;
  gw_context_t *gw_ctx = (gw_context_t *)ctx;
  /* prepare context */
  uint32_t from_id = gw_ctx->call_context.to_id;
  gw_context_t sub_gw_ctx;
  ret = gw_create_sub_context(gw_ctx, &sub_gw_ctx, from_id, to_id, args,
                              args_len);
  if (ret != 0) {
    return ret;
  }
  receipt->return_data_len = 0;
  sub_gw_ctx.sys_context = receipt;

  uint8_t script_hash[32];
  ret = gw_ctx->sys_get_script_hash_by_account_id(ctx, to_id, script_hash);
  if (ret != 0) {
    return ret;
  }
  if (is_polyjuice_script_hash(script_hash)) {
    ret = gw_handle_message(&sub_gw_ctx);
    if (ret != 0) {
      return ret;
    }
  } else {
    /* load code_hash */
    void *handle = NULL;
    uint64_t consumed_size = 0;

    uint64_t buffer_size =
      gw_ctx->code_buffer_len - gw_ctx->code_buffer_used_size;
    ret =
      ckb_dlopen(to_id, gw_ctx->code_buffer + gw_ctx->code_buffer_used_size,
                 buffer_size, &handle, &consumed_size);
    if (ret != 0) {
      return ret;
    }
    if (consumed_size > buffer_size) {
      return GW_ERROR_INVALID_DATA;
    }
    gw_ctx->code_buffer_used_size += consumed_size;

    /* Run contract */
    ret = invoke_contract_func(&sub_gw_ctx, handle);
    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

int main() {
  ckb_debug("BEGIN generator.c");
  int ret;
  uint8_t code_buffer[CODE_SIZE] __attribute__((aligned(RISCV_PGSIZE)));

  /* prepare context */
  gw_call_receipt_t receipt;
  gw_context_t context;
  context.sys_context = &receipt;
  context.sys_load = sys_load;
  context.sys_store = sys_store;
  context.sys_set_program_return_data = sys_set_program_return_data;
  context.sys_call = sys_call;

  uint8_t call_context[CALL_CONTEXT_LEN];
  uint64_t len = CALL_CONTEXT_LEN;
  ckb_debug("BEGIN _sys_load_call_context()");
  ret = _sys_load_call_context(call_context, &len);
  ckb_debug("END _sys_load_call_context()");
  if (ret != 0) {
    return ret;
  }
  if (len > CALL_CONTEXT_LEN) {
    return GW_ERROR_INVALID_DATA;
  }

  mol_seg_t call_context_seg;
  call_context_seg.ptr = call_context;
  call_context_seg.size = len;
  ckb_debug("BEGIN gw_parse_call_context()");
  ret = gw_parse_call_context(&context.call_context, &call_context_seg);
  ckb_debug("END gw_parse_call_context()");
  if (ret != 0) {
    return ret;
  }

  uint8_t block_info[BLOCK_INFO_LEN];
  len = BLOCK_INFO_LEN;
  ckb_debug("BEGIN _sys_load_block_info()");
  ret = _sys_load_block_info(block_info, &len);
  ckb_debug("END _sys_load_block_info()");
  if (ret != 0) {
    return ret;
  }
  if (len > BLOCK_INFO_LEN) {
    return GW_ERROR_INVALID_DATA;
  }

  mol_seg_t block_info_seg;
  block_info_seg.ptr = block_info;
  block_info_seg.size = len;
  ckb_debug("BEGIN gw_parse_call_context()");
  ret = gw_parse_block_info(&context.block_info, &block_info_seg);
  ckb_debug("END gw_parse_call_context()");
  if (ret != 0) {
    return ret;
  }

  /* load layer2 contract */
  uint32_t id = context.call_context.to_id;
  uint8_t script_hash[32];
  ret = context.sys_get_script_hash_by_account_id((void *)(&context), id, script_hash);
  if (ret != 0) {
    return ret;
  }
  if (is_polyjuice_script_hash(script_hash)) {
    ret = invoke_polyjuice_contract_func(&context);
    if (ret != 0) {
      return ret;
    }
  } else {
    void *handle = NULL;
    uint64_t consumed_size = 0;
    ckb_debug("BEGIN ckb_dlopen()");
    ret = ckb_dlopen(id, code_buffer, CODE_SIZE, &handle, &consumed_size);
    ckb_debug("END ckb_dlopen()");
    if (ret != 0) {
      return ret;
    }

    if (consumed_size > CODE_SIZE) {
      return GW_ERROR_DYNAMIC_LINKING;
    }

    context.code_buffer = code_buffer;
    context.code_buffer_len = CODE_SIZE;
    context.code_buffer_used_size = consumed_size;

    /* run contract */
    ckb_debug("BEGIN invoke_contract_func()");
    ret = invoke_contract_func(&context, handle);
    ckb_debug("END invoke_contract_func()");
    if (ret != 0) {
      return ret;
    }
  }

  /* Return data from receipt */
  ret = _set_program_return_data(receipt.return_data, receipt.return_data_len);
  if (ret != 0) {
    return ret;
  }

  ckb_debug("END generator.c");
  return 0;
}
