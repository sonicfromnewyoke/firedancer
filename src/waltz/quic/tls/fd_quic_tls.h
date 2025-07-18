#ifndef HEADER_fd_src_waltz_quic_tls_fd_quic_tls_h
#define HEADER_fd_src_waltz_quic_tls_fd_quic_tls_h

#include "../fd_quic_common.h"
#include "../fd_quic_enum.h"
#include "../../tls/fd_tls.h"
#include "../templ/fd_quic_transport_params.h"

/* QUIC-TLS

   This defines an API for QUIC-TLS

   General operation:
     // set up a quic-tls config object
     fd_quic_tls_cfg_t quic_tls_cfg = {
       .secret_cb             = my_secret_cb,        // callback for communicating secrets

       .handshake_complete_cb = my_hs_complete,      // called when handshake is complete

       .max_concur_handshakes = 1234,                // number of handshakes this object can
                                                     // manage concurrently
       };

     // create a quic-tls object to manage handshakes:
     fd_quic_tls_t * quic_tls = fd_quic_tls_new( quic_tls_cfg );

     // delete a quic-tls object when it's not needed anymore
     fd_quic_delete( quic_tls );

     // create a client or a server handshake object
     //   call upon a new connection to manage the connection TLS handshake
     fd_quic_tls_hs_t * hs = fd_quic_tls_hs_new( quic_tls, conn_id, conn_id_sz, is_server, transport_params, now );

     // delete a handshake object
     //   NULL is allowed here
     fd_quic_tls_hs_delete( hs );

     // call fd_quic_tls_provide_data whenever the peer sends TLS
     // handshake data.  The peer bootstraps the conversation with a
     // zero byte input.

*/

/* each TLS handshake requires a number of fd_quic_tls_hs_data structures */
#define FD_QUIC_TLS_HS_DATA_CNT 16u

/* alignment of hs_data
   must be a power of 2 */
#define FD_QUIC_TLS_HS_DATA_ALIGN 32u

/* number of bytes allocated for queued handshake data
   must be a multiple of FD_QUIC_TLS_HS_DATA_ALIGN */
#define FD_QUIC_TLS_HS_DATA_SZ  (2048UL)

/* callback function prototypes */

typedef void
(* fd_quic_tls_cb_secret_t)( fd_quic_tls_hs_t *           hs,
                             void *                       context,
                             fd_quic_tls_secret_t const * secret );

typedef void
(* fd_quic_tls_cb_handshake_complete_t)( fd_quic_tls_hs_t * hs,
                                         void *             context  );

typedef void
(* fd_quic_tls_cb_peer_params_t)( void *        context,
                                  uchar const * quic_tp,
                                  ulong         quic_tp_sz );

struct fd_quic_tls_secret {
  uint  enc_level;
  uchar read_secret [ FD_QUIC_SECRET_SZ ];
  uchar write_secret[ FD_QUIC_SECRET_SZ ];
};

struct fd_quic_tls_cfg {
  // callbacks ../crypto/fd_quic_crypto_suites
  fd_quic_tls_cb_secret_t              secret_cb;
  fd_quic_tls_cb_handshake_complete_t  handshake_complete_cb;
  fd_quic_tls_cb_peer_params_t         peer_params_cb;

  ulong          max_concur_handshakes;

  /* Signing callback for TLS 1.3 CertificateVerify. Context of the
     signer must outlive the tls object. */
  fd_tls_sign_t signer;

  /* Ed25519 public key */
  uchar const * cert_public_key;
};

/* structure for organising handshake data */
struct fd_quic_tls_hs_data {
  uchar const * data;
  uint          data_sz;
  uint          free_data_sz; /* internal use */
  uint          offset;
  uint          enc_level;

  /* internal use */
  ushort      next_idx; /* next in linked list, ~0 for end */
};

struct fd_quic_tls {
  /* callbacks */
  fd_quic_tls_cb_secret_t              secret_cb;
  fd_quic_tls_cb_handshake_complete_t  handshake_complete_cb;
  fd_quic_tls_cb_peer_params_t         peer_params_cb;

  /* ssl related */
  fd_tls_t tls;
};

#define FD_QUIC_TLS_HS_DATA_UNUSED ((ushort)~0u)

struct fd_quic_tls_hs {
  /* TLS handshake handles are deliberately placed at the start.
     Allows for type punning between fd_quic_tls_hs_t and
     fd_tls_estate_{srv,cli}_t.  DO NOT MOVE.
     Type of handshake object depends on is_server. */
  fd_tls_estate_t hs;

  fd_quic_tls_t * quic_tls;

  int             is_server;
  int             is_hs_complete;

  /* user defined context supplied in callbacks */
  void *          context;

  ulong           next;      /* alloc pool/cache dlist */
  ulong           prev;      /* cache dlist */
  ulong           birthtime; /* allocation time, used for cache eviction sanity check */

  /* handshake data
     this is data that must be sent to the peer
     it consists of an arbitrary list of tuples of:
       < "encryption level", array of bytes >
     these will be encapsulated and sent in order */
  fd_quic_tls_hs_data_t hs_data[ FD_QUIC_TLS_HS_DATA_CNT ];

  /* head of hs_data_t free list */
  ushort hs_data_free_idx;

  /* head/tail of hs_data_t pending (to be sent) */
  ushort hs_data_pend_idx[4];
  ushort hs_data_pend_end_idx[4];

  /* handshake data buffer
      allocated in arbitrary chunks
      and shared between encryption levels */
  uchar hs_data_buf[ FD_QUIC_TLS_HS_DATA_SZ ];
  uint  hs_data_buf_ptr;     /* ptr is first unused byte in hs_data_buf */
  uint  hs_data_offset[ 4 ]; /* one offset per encoding level */

  /* Handshake message receive buffer

     rx_hs_buf buffers messages of one encryption level (rx_enc_level).
     rx_off is the number of bytes processed by fd_tls.  rx_sz is the
     number of contiguous bytes received from the peer. */

  ushort rx_off;
  ushort rx_sz;
  uchar  rx_enc_level;

# define FD_QUIC_TLS_RX_DATA_SZ (2048UL)
  uchar rx_hs_buf[ FD_QUIC_TLS_RX_DATA_SZ ];

  /* TLS alert code */
  uint  alert;

  /* our own QUIC transport params */
  fd_quic_transport_params_t self_transport_params;

};

/* fd_quic_tls_new formats an unused memory region for use as an
   fd_quic_tls_t object and joins the caller to it */

fd_quic_tls_t *
fd_quic_tls_new( fd_quic_tls_t *     mem,
                 fd_quic_tls_cfg_t * cfg );

/* fd_quic_delete unformats a memory region used as an fd_quic_tls_t.
   Returns the given pointer on success and NULL if used obviously in error.
   Deletes any fd_tls resources. */

void *
fd_quic_tls_delete( fd_quic_tls_t * self );

fd_quic_tls_hs_t *
fd_quic_tls_hs_new( fd_quic_tls_hs_t * self,
                    fd_quic_tls_t *    quic_tls,
                    void *             context,
                    int                is_server,
                    fd_quic_transport_params_t const * self_transport_params,
                    ulong              now );

void
fd_quic_tls_hs_delete( fd_quic_tls_hs_t * hs );

/* fd_quic_tls_process processes any available TLS handshake messages
   from previously received CRYPTO frames.  Returns FD_QUIC_SUCCESS if
   any number of messages were processed (including no messages in there
   is not enough data).  Returns FD_QUIC_FAILED if the TLS handshake
   failed (not recoverable). */

int
fd_quic_tls_process( fd_quic_tls_hs_t * self );


/* fd_quic_tls_get_hs_data

   get oldest queued handshake data from the queue of pending data to sent to peer

   returns
     NULL    there is no data available
     hd_data   a pointer to the fd_quic_tls_hs_data_t structure at the head of the queue

   the hd_data and data therein are invalidated by the following
     fd_quic_tls_pop_hs_data
     fd_quic_tls_hs_delete

   args
     self        the handshake in question (fine if NULL)
     enc_level   a pointer for receiving the encryption level
     data        a pointer for receiving the pointer to the data buffer
     data_sz     a pointer for receiving the data size */
fd_quic_tls_hs_data_t *
fd_quic_tls_get_hs_data( fd_quic_tls_hs_t *  self, uint enc_level );


/* fd_quic_tls_get_next_hs_data

   get the next unit of handshake data from the queue

   returns NULL if no more available */
fd_quic_tls_hs_data_t *
fd_quic_tls_get_next_hs_data( fd_quic_tls_hs_t * self, fd_quic_tls_hs_data_t * hs );


/* fd_quic_tls_pop_hs_data

   remove handshake data from head of queue and free associated resources */
void
fd_quic_tls_pop_hs_data( fd_quic_tls_hs_t * self, uint enc_level );


/* fd_quic_tls_clear_hs_data

   clear all handshake data from a given encryption level. */
void
fd_quic_tls_clear_hs_data( fd_quic_tls_hs_t * self, uint enc_level );


#endif /* HEADER_fd_src_waltz_quic_tls_fd_quic_tls_h */

