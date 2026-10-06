#ifndef MIDIHUB_NETMIDI2_H
#define MIDIHUB_NETMIDI2_H

#include <stddef.h>
#include <stdint.h>

/* Network MIDI 2.0 (UDP), MMA/AMEI M2-124-UM v1.0. A UDP packet is the
 * signature "MIDI" followed by Command Packets: a 32-bit header (command
 * code, payload length in words, two command specific bytes) and the
 * payload. Integers are big-endian. No OS calls occur here: the caller
 * owns the socket, addresses, mDNS and the clock. */

#define MH_NM2_SIGNATURE 0x4d494449u
#define MH_NM2_MAX_PACKET 1400      /* avoid IP fragmentation */
#define MH_NM2_MAX_UMP_WORDS 64     /* per UMP Data command */
#define MH_NM2_NAME_MAX 98
#define MH_NM2_PRODUCT_ID_MAX 42

enum mh_nm2_command {
    MH_NM2_INVITATION = 0x01,
    MH_NM2_INVITATION_AUTH = 0x02,
    MH_NM2_INVITATION_USER_AUTH = 0x03,
    MH_NM2_INVITATION_ACCEPTED = 0x10,
    MH_NM2_INVITATION_PENDING = 0x11,
    MH_NM2_INVITATION_AUTH_REQUIRED = 0x12,
    MH_NM2_INVITATION_USER_AUTH_REQUIRED = 0x13,
    MH_NM2_PING = 0x20,
    MH_NM2_PING_REPLY = 0x21,
    MH_NM2_RETRANSMIT_REQUEST = 0x80,
    MH_NM2_RETRANSMIT_ERROR = 0x81,
    MH_NM2_SESSION_RESET = 0x82,
    MH_NM2_SESSION_RESET_REPLY = 0x83,
    MH_NM2_NAK = 0x8f,
    MH_NM2_BYE = 0xf0,
    MH_NM2_BYE_REPLY = 0xf1,
    MH_NM2_UMP_DATA = 0xff
};

enum mh_nm2_bye_reason {
    MH_NM2_BYE_UNKNOWN = 0x00,
    MH_NM2_BYE_USER_TERMINATED = 0x01,
    MH_NM2_BYE_POWER_DOWN = 0x02,
    MH_NM2_BYE_TOO_MANY_MISSING = 0x03,
    MH_NM2_BYE_TIMEOUT = 0x04,
    MH_NM2_BYE_NOT_ESTABLISHED = 0x05,
    MH_NM2_BYE_NO_PENDING_SESSION = 0x06,
    MH_NM2_BYE_PROTOCOL_ERROR = 0x07,
    MH_NM2_BYE_TOO_MANY_SESSIONS = 0x40,
    MH_NM2_BYE_AUTH_NOT_SUPPORTED = 0x45, /* No Matching Authentication Method */
    MH_NM2_BYE_INVITATION_CANCELED = 0x80
};

enum mh_nm2_nak_reason {
    MH_NM2_NAK_OTHER = 0x00,
    MH_NM2_NAK_NOT_SUPPORTED = 0x01,
    MH_NM2_NAK_NOT_EXPECTED = 0x02,
    MH_NM2_NAK_MALFORMED = 0x03,
    MH_NM2_NAK_BAD_PING_REPLY = 0x20
};

/* One parsed Command Packet. payload points into the UDP packet. */
struct mh_nm2_command_packet {
    uint8_t code;
    uint8_t csd1;
    uint8_t csd2;
    const uint8_t *payload;
    size_t payload_words;
};

/* Splits a UDP payload into its commands. Returns the number found, or -1
 * when the signature is wrong or a command runs past the packet; commands
 * before the damage are still reported through count. */
int mh_nm2_parse(const uint8_t *packet, size_t length,
                 struct mh_nm2_command_packet *commands, size_t capacity);

/* Builds a UDP payload. Every add returns 0, or -1 when it does not fit. */
struct mh_nm2_writer {
    uint8_t *data;
    size_t capacity;
    size_t length;
};

int mh_nm2_writer_init(struct mh_nm2_writer *writer, uint8_t *data, size_t capacity);
int mh_nm2_add(struct mh_nm2_writer *writer, uint8_t code, uint8_t csd1, uint8_t csd2,
               const uint8_t *payload, size_t payload_bytes);
/* Invitation, Invitation Reply Accepted or Pending: name and product
 * instance id, each padded to whole words. */
int mh_nm2_add_identity(struct mh_nm2_writer *writer, uint8_t code, uint8_t csd2,
                        const char *name, const char *product_id);
int mh_nm2_add_ping(struct mh_nm2_writer *writer, uint8_t code, uint32_t id);
int mh_nm2_add_ump(struct mh_nm2_writer *writer, uint16_t sequence,
                   const uint32_t *words, size_t count);
int mh_nm2_add_nak(struct mh_nm2_writer *writer, uint8_t reason,
                   const struct mh_nm2_command_packet *original);

/* Reads the UMP Endpoint Name and Product Instance Id of an Invitation or
 * Invitation Reply into NUL-terminated buffers. Returns -1 if malformed. */
int mh_nm2_identity(const struct mh_nm2_command_packet *command,
                    char *name, size_t name_size, char *product_id, size_t id_size);

/* A session with one peer, as Host or Client. */
enum mh_nm2_state {
    MH_NM2_IDLE,
    MH_NM2_INVITING,        /* Client: Invitation sent */
    MH_NM2_ESTABLISHED,
    MH_NM2_CLOSING          /* Bye sent, waiting for Bye Reply */
};

#define MH_NM2_FEC_REPEATS 2

struct mh_nm2_sent_ump {
    uint16_t sequence;
    uint8_t words;
    uint32_t data[MH_NM2_MAX_UMP_WORDS];
};

typedef void (*mh_nm2_ump_sink)(void *context, const uint32_t *words, size_t count);

struct mh_nm2_session {
    enum mh_nm2_state state;
    int host;
    char name[MH_NM2_NAME_MAX + 1];
    char product_id[MH_NM2_PRODUCT_ID_MAX + 1];
    char peer_name[MH_NM2_NAME_MAX + 1];
    char peer_product_id[MH_NM2_PRODUCT_ID_MAX + 1];
    unsigned protocols;             /* MH_UMP_PROTOCOL_* this endpoint takes */

    uint16_t tx_sequence;
    struct mh_nm2_sent_ump sent[MH_NM2_FEC_REPEATS];
    unsigned sent_count;

    int rx_started;
    uint16_t rx_expected;
    unsigned long rx_lost;          /* UMP Data commands never received */

    uint32_t now_ms;
    uint32_t last_send_ms;          /* last UMP Data with data */
    uint32_t last_receive_ms;
    uint32_t idle_sent;             /* zero length commands since data */
    uint32_t next_idle_ms;
    uint32_t ping_id;
    uint32_t ping_sent_ms;
    unsigned pings_unanswered;
    uint32_t state_since_ms;
    unsigned retries;
    uint8_t bye_reason;             /* repeated until Bye Reply */

    mh_nm2_ump_sink ump_sink;
    void *ump_context;
};

void mh_nm2_session_init(struct mh_nm2_session *session, int host,
                         const char *name, const char *product_id,
                         mh_nm2_ump_sink sink, void *context);

/* Client: start a session. Writes the Invitation into out. */
int mh_nm2_invite(struct mh_nm2_session *session, uint32_t now_ms,
                  struct mh_nm2_writer *out);

/* Handles one UDP payload from the peer; replies, if any, are added to
 * out (initialised by the caller). Received UMPs reach the sink in order,
 * each once even with FEC; Endpoint Discovery is answered here. Returns
 * -1 when the packet was not Network MIDI 2.0. */
int mh_nm2_receive(struct mh_nm2_session *session, const uint8_t *packet,
                   size_t length, uint32_t now_ms, struct mh_nm2_writer *out);

/* Sends UMPs: a UMP Data command preceded by the previous ones for FEC.
 * Returns -1 when not established or it does not fit. */
int mh_nm2_send_ump(struct mh_nm2_session *session, const uint32_t *words,
                    size_t count, uint32_t now_ms, struct mh_nm2_writer *out);

/* Call regularly: idle (zero length) UMP Data, pings, retries and
 * timeouts. Returns 1 when out holds a packet to send. */
int mh_nm2_tick(struct mh_nm2_session *session, uint32_t now_ms,
                struct mh_nm2_writer *out);

/* Ends the session: writes a Bye. */
int mh_nm2_bye(struct mh_nm2_session *session, uint8_t reason, uint32_t now_ms,
               struct mh_nm2_writer *out);

#endif
