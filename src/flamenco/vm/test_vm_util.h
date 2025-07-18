#ifndef HEADER_fd_src_flamenco_vm_test_vm_util_h
#define HEADER_fd_src_flamenco_vm_test_vm_util_h

#include "fd_vm.h"
#include "../runtime/context/fd_exec_instr_ctx.h"
#include "../../util/valloc/fd_valloc.h"

#define TEST_VM_REJECT_CALLX_R10_FEATURE_PREFIX (0x7e787d5c6d662d23)

#define TEST_VM_DEFAULT_SBPF_VERSION FD_SBPF_V0

fd_exec_instr_ctx_t *
test_vm_minimal_exec_instr_ctx( fd_valloc_t          valloc,
                                fd_exec_slot_ctx_t * slot_ctx );

void
test_vm_exec_instr_ctx_delete( fd_exec_instr_ctx_t * ctx,
                               fd_valloc_t           valloc );

void
test_vm_clear_txn_ctx_err( fd_exec_txn_ctx_t * txn_ctx );

#endif
