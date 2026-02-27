#ifndef COMMUNICATE_H
#define COMMUNICATE_H

#include "universe.h"
#include "rng.h"

/* ---- Constants ---- */

#define MAX_MESSAGES      4096
#define MAX_MSG_CONTENT    512
#define MAX_BEACONS        256
#define MAX_RELAYS         256
#define MAX_BEACON_MSG     256
#define LIGHT_SPEED_LY_PER_TICK (1.0 / 365.0)  /* 1 ly/year, 1 tick = 1 day */

/* Base communication range in ly per tech level */
#define COMM_BASE_RANGE_LY  5.0
#define COMM_RANGE_PER_LEVEL 5.0  /* +5 ly per comm tech level */

/* Relay range extension */
#define RELAY_RANGE_LY     20.0

/* Energy costs */
#define COMM_ENERGY_TARGETED  1000.0   /* joules for targeted msg */
#define COMM_ENERGY_BROADCAST 10000.0  /* joules for broadcast */

/* ---- Message ---- */

typedef enum {
    MSG_TARGETED,     /* point-to-point */
    MSG_BROADCAST     /* all probes in range */
} msg_mode_t;

typedef enum {
    MSG_IN_TRANSIT,
    MSG_DELIVERED,
    MSG_EXPIRED,      /* target destroyed or out of range */
    MSG_RELAYED
} msg_status_t;

typedef struct {
    probe_uid_t  sender_id;
    probe_uid_t  target_id;       /* null for broadcast */
    msg_mode_t   mode;
    char         content[MAX_MSG_CONTENT];
    uint64_t     sent_tick;
    uint64_t     arrival_tick;    /* sent_tick + distance/c in ticks */
    msg_status_t status;
    double       distance_ly;     /* sender-to-target distance at send time */
} message_t;

/* ---- Beacon ---- */

typedef struct {
    probe_uid_t  owner_id;
    probe_uid_t  system_id;       /* system the beacon is placed in */
    vec3_t       position;        /* galactic coords */
    char         message[MAX_BEACON_MSG];
    uint64_t     placed_tick;
    bool         active;
} beacon_t;

/* ---- Relay Satellite ---- */

typedef struct {
    probe_uid_t  owner_id;
    probe_uid_t  system_id;
    vec3_t       position;
    uint64_t     built_tick;
    bool         active;
    double       range_ly;        /* effective relay range */
} relay_t;

/* ---- Message Queue ---- */

typedef struct {
    message_t messages[MAX_MESSAGES];
    int       count;
    beacon_t  beacons[MAX_BEACONS];
    int       beacon_count;
    relay_t   relays[MAX_RELAYS];
    int       relay_count;
} comm_system_t;

/* ---- API ---- */

/* Initialize comm system */
void comm_init(comm_system_t *cs);

/* Calculate communication range for a probe based on tech level */
double comm_range(const probe_t *probe);

/* Calculate light-delay in ticks between two positions */
uint64_t comm_light_delay(vec3_t from, vec3_t to);

/* Send a targeted message to a specific probe.
 * Returns 0 on success, -1 if out of range or insufficient energy. */
int comm_send_targeted(comm_system_t *cs, probe_t *sender,
                       probe_uid_t target_id, vec3_t target_pos,
                       const char *content, uint64_t current_tick);

/* Broadcast a message to all probes in range.
 * Returns number of messages queued, -1 on error. */
int comm_send_broadcast(comm_system_t *cs, probe_t *sender,
                        const probe_t *all_probes, int probe_count,
                        const char *content, uint64_t current_tick);

/* Check if target is reachable: direct range OR via relay chain.
 * Returns effective distance (possibly shorter via relays), or -1.0 if unreachable. */
double comm_check_reachable(const comm_system_t *cs, const probe_t *sender,
                            vec3_t target_pos);

/* Tick: deliver messages whose arrival_tick <= current_tick.
 * Returns count of messages delivered this tick. */
int comm_tick_deliver(comm_system_t *cs, uint64_t current_tick);

/* Get pending (delivered) messages for a probe.
 * Returns count, writes up to max_out messages. */
int comm_get_inbox(const comm_system_t *cs, probe_uid_t probe_id,
                   message_t *out, int max_out);

/* ---- Beacons ---- */

/* Place a beacon at the probe's current location.
 * Returns 0 on success, -1 if beacon limit reached. */
int comm_place_beacon(comm_system_t *cs, const probe_t *owner,
                      probe_uid_t system_id, const char *message,
                      uint64_t current_tick);

/* Detect beacons in a system. Returns count, writes to out. */
int comm_detect_beacons(const comm_system_t *cs, probe_uid_t system_id,
                        beacon_t *out, int max_out);

/* Deactivate a beacon by owner */
int comm_deactivate_beacon(comm_system_t *cs, probe_uid_t owner_id,
                           probe_uid_t system_id);

/* ---- Relay Satellites ---- */

/* Build a relay satellite at probe's location.
 * Returns 0 on success, -1 if relay limit reached. */
int comm_build_relay(comm_system_t *cs, const probe_t *owner,
                     probe_uid_t system_id, uint64_t current_tick);

/* Find relay-assisted path distance between two points.
 * Returns shortest distance via relay chain, or direct distance if shorter.
 * Returns -1.0 if completely unreachable. */
double comm_relay_path_distance(const comm_system_t *cs,
                                vec3_t from, vec3_t to, double direct_range);

#endif /* COMMUNICATE_H */
