/* test_xdp_ebpf: Exercises unit test invocations of ebpf_xdp_flow via
   bpf(2) syscall in BPF_PROG_TEST_RUN mode. */

#if !defined(__linux__)
#error "test_xdp_ebpf requires Linux operating system with XDP support"
#endif

#define _DEFAULT_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../../util/fd_util.h"
#include "../ebpf/fd_linux_bpf.h"
#include "fd_xdp1.h"

/* Test support *******************************************************/

int prog_fd     = -1; /* BPF program */
int xsks_fd     = -1; /* Queue-to-XSK map */
int xsk_fd      = -1; /* AF_XDP socket */

/* Test harness *******************************************************/

typedef struct { ulong k; int v; } fd_udp_dst_kv_t;

struct fd_xdp_redirect_test {
  /* Input ****************/
  char const * name;

  uchar const * packet;    /* Packet content (starting at Ethernet) */
  ulong const * packet_sz; /* Pointer to size */

  /* Output ***************/
  uint xdp_action;
};
typedef struct fd_xdp_redirect_test fd_xdp_redirect_test_t;

static int
fd_bpf_map_clear( int map_fd ) {
  ulong key = 0UL;

  for(;;) {
    ulong next_key;
    if( FD_UNLIKELY( 0!=fd_bpf_map_get_next_key( map_fd, &key, &next_key ) ) ) {
      if( FD_LIKELY( errno==ENOENT ) ) break;
      FD_LOG_ERR(( "bpf_map_get_next_key(%d,%#lx,%p) failed (%i-%s)",
                   map_fd, key, (void *)&next_key, errno, fd_io_strerror( errno ) ));
    }

    if( FD_UNLIKELY( 0!=fd_bpf_map_delete_elem( map_fd, &next_key ) ) )
      FD_LOG_ERR(( "bpf_map_delete_elem(%d,%#lx) failed (%i-%s)", map_fd, next_key, errno, fd_io_strerror( errno ) ));

    key = next_key;
  }

  return 0;
}
static void
fd_run_xdp_redirect_test( fd_xdp_redirect_test_t const * test ) {
  fd_bpf_map_clear( xsks_fd );

# define FD_XDP_TEST(c) do { if( FD_UNLIKELY( !(c) ) ) FD_LOG_ERR(( "FAIL (%s): %s", test->name, #c )); } while(0)

  /* Hook up to XSK */
  int rx_queue = 0;
  FD_TEST( 0==fd_bpf_map_update_elem( xsks_fd, &rx_queue, &xsk_fd, 0UL ) );

  union bpf_attr attr = {
    .test = {
      .prog_fd      = (uint)prog_fd,
      .data_in      = (ulong)test->packet,
      .data_size_in = (uint)*test->packet_sz
    }
  };
  FD_XDP_TEST( 0==bpf( BPF_PROG_TEST_RUN, &attr, sizeof(union bpf_attr) ) );

  FD_LOG_INFO(( "bpf test %s returned %#x", test->name, attr.test.retval ));

  FD_XDP_TEST( attr.test.retval == test->xdp_action );

# undef FD_XDP_TEST
}

/* Test runs **********************************************************/

FD_IMPORT_BINARY( tcp_syn,         "src/waltz/xdp/fixtures/tcp_syn.bin"         );
FD_IMPORT_BINARY( tcp_ack,         "src/waltz/xdp/fixtures/tcp_ack.bin"         );
FD_IMPORT_BINARY( tcp_syn_ack,     "src/waltz/xdp/fixtures/tcp_syn_ack.bin"     );
FD_IMPORT_BINARY( arp_request,     "src/waltz/xdp/fixtures/arp_request.bin"     );
FD_IMPORT_BINARY( arp_reply,       "src/waltz/xdp/fixtures/arp_reply.bin"       );
FD_IMPORT_BINARY( icmp_echo_reply, "src/waltz/xdp/fixtures/icmp_echo_reply.bin" );
FD_IMPORT_BINARY( icmp_echo,       "src/waltz/xdp/fixtures/icmp_echo.bin"       );
FD_IMPORT_BINARY( dns_query_a,     "src/waltz/xdp/fixtures/dns_query_a.bin"     );
FD_IMPORT_BINARY( tcp_rst,         "src/waltz/xdp/fixtures/tcp_rst.bin"         );

FD_IMPORT_BINARY( quic_initial,    "src/waltz/xdp/fixtures/quic_initial.bin"    );

fd_xdp_redirect_test_t tests[] = {
  /* Ensure that program sets XDP_PASS on common packet types that are
     not part of the Firedancer application layer. */
  #define TEST(x) .name = #x , .packet = x , .packet_sz = &x##_sz,

  { TEST( tcp_syn         ) .xdp_action = XDP_PASS },
  { TEST( tcp_ack         ) .xdp_action = XDP_PASS },
  { TEST( tcp_syn_ack     ) .xdp_action = XDP_PASS },
  { TEST( arp_request     ) .xdp_action = XDP_PASS },
  { TEST( arp_reply       ) .xdp_action = XDP_PASS },
  { TEST( icmp_echo_reply ) .xdp_action = XDP_PASS },
  { TEST( icmp_echo       ) .xdp_action = XDP_PASS },
  { TEST( dns_query_a     ) .xdp_action = XDP_PASS },
  { TEST( tcp_rst         ) .xdp_action = XDP_PASS },

  { TEST( quic_initial    ) .xdp_action = XDP_REDIRECT },

  #undef TEST
  {0}
};

int main( int     argc,
          char ** argv ) {
  fd_boot( &argc, &argv );

  /* Create maps */

  union bpf_attr attr = {
    .map_type    = BPF_MAP_TYPE_XSKMAP,
    .key_size    = 4U,
    .value_size  = 4U,
    .max_entries = 4U,
    .map_name    = "fd_xdp_xsks"
  };
  xsks_fd = (int)bpf( BPF_MAP_CREATE, &attr, sizeof(union bpf_attr) );
  if( FD_UNLIKELY( xsks_fd<0 ) ) {
    if( FD_UNLIKELY( errno==EPERM ) ) {
      FD_LOG_WARNING(( "skip: insufficient perms" ));
      fd_halt();
      return 0;
    }
    FD_LOG_WARNING(( "Failed to create XSKMAP (%i-%s)", errno, fd_io_strerror( errno ) ));
    return -1;
  }

  /* Link program */

  ushort ports[1] = {8001};
  ulong code_buf[ 512 ];
  ulong code_cnt = fd_xdp_gen_program( code_buf, xsks_fd, ports, 1UL );

  /* Load object into kernel */

  char ebpf_kern_log[ 32768UL ];
  ebpf_kern_log[0] = 0;
  attr = (union bpf_attr) {
    .prog_type = BPF_PROG_TYPE_XDP,
    .insn_cnt  = (uint)code_cnt,
    .insns     = (ulong)code_buf,
    .license   = (ulong)"Apache-2.0",
    .prog_name = "fd_redirect",
    .log_level = 6,
    .log_size  = 32768UL,
    .log_buf   = (ulong)ebpf_kern_log
  };
  prog_fd = (int)bpf( BPF_PROG_LOAD, &attr, sizeof(union bpf_attr) );
  if( FD_UNLIKELY( prog_fd<0 ) ) {
    if( errno==EPERM ) {
      FD_LOG_WARNING(( "skip: insufficient permissions to load BPF object" ));
      fd_halt();
      return 0;
    }
    FD_LOG_WARNING(( "eBPF verifier log:\n%s", ebpf_kern_log ));
    FD_LOG_ERR(( "BPF_PROG_LOAD failed (%i-%s)", errno, fd_io_strerror( errno ) ));
  }

  /* Create new AF_XDP socket. Doesn't actually have to be operational
     for bpf_redirect_map() to return XDP_REDIRECT. */
  xsk_fd = socket( AF_XDP, SOCK_RAW, 0 );
  FD_TEST( xsk_fd>=0 );

  /* Run tests */

  for( fd_xdp_redirect_test_t * t=tests; t->packet; t++ )
    fd_run_xdp_redirect_test( t );

  /* Clean up */

  close( xsk_fd );
  close( prog_fd );

  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}
