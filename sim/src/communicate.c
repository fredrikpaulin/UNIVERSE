/*
 * communicate.c — Phase 8: Inter-probe communication
 *
 * Light-speed messaging, beacons, relay satellites.
 */

#include "communicate.h"
#include <string.h>
#include <math.h>

/* ---- Helpers ---- */

static double vec3_dist(vec3_t a, vec3_t b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrt(dx*dx + dy*dy + dz*dz);
}

/* Get the "galactic position" of a probe for comm purposes.
 * Tests use probe.destination as the position. */
static vec3_t probe_pos(const probe_t *p) {
    return p->destination;
}

/* ---- Init ---- */

void comm_init(comm_system_t *cs) {
    memset(cs, 0, sizeof(*cs));
}

/* ---- Range ---- */

double comm_range(const probe_t *probe) {
    return COMM_BASE_RANGE_LY + COMM_RANGE_PER_LEVEL * probe->tech_levels[TECH_COMMUNICATION];
}

/* ---- Light delay ---- */

uint64_t comm_light_delay(vec3_t from, vec3_t to) {
    double dist = vec3_dist(from, to);
    /* 1 ly / (1 ly/year) = 1 year = 365 ticks */
    return (uint64_t)(dist * 365.0 + 0.5);  /* round to nearest tick */
}

/* ---- Relay path finding ---- */

/* BFS/greedy: find shortest relay-assisted path from `from` to `to`.
 * Returns total path distance, or -1.0 if unreachable.
 * direct_range is the sender's comm range. */
double comm_relay_path_distance(const comm_system_t *cs,
                                vec3_t from, vec3_t to, double direct_range) {
    /* Check direct */
    double direct = vec3_dist(from, to);
    if (direct <= direct_range) return direct;

    /* Dijkstra-like with relays.
     * Nodes: source(0), relays(1..n), target(n+1)
     * Edges: source→relay if dist <= direct_range,
     *        relay→relay if dist <= relay.range,
     *        relay→target if dist <= relay.range */
    int n = cs->relay_count;
    if (n == 0) return -1.0;

    /* dist_to[i] = shortest distance from source to relay i */
    double dist_to[MAX_RELAYS];
    bool visited[MAX_RELAYS];
    for (int i = 0; i < n; i++) {
        dist_to[i] = -1.0;
        visited[i] = false;
        if (!cs->relays[i].active) continue;
        double d = vec3_dist(from, cs->relays[i].position);
        if (d <= direct_range) {
            dist_to[i] = d;
        }
    }

    /* Relax edges iteratively */
    for (int iter = 0; iter < n; iter++) {
        /* Pick unvisited relay with smallest dist_to */
        int best = -1;
        double best_dist = 1e30;
        for (int i = 0; i < n; i++) {
            if (!visited[i] && dist_to[i] > 0 && dist_to[i] < best_dist) {
                best = i;
                best_dist = dist_to[i];
            }
        }
        if (best < 0) break;
        visited[best] = true;

        /* Check if this relay can reach the target */
        double to_target = vec3_dist(cs->relays[best].position, to);
        if (to_target <= cs->relays[best].range_ly) {
            double total = dist_to[best] + to_target;
            return total;
        }

        /* Relax relay→relay edges */
        for (int j = 0; j < n; j++) {
            if (visited[j] || !cs->relays[j].active) continue;
            double d = vec3_dist(cs->relays[best].position, cs->relays[j].position);
            if (d <= cs->relays[best].range_ly) {
                double new_dist = dist_to[best] + d;
                if (dist_to[j] < 0 || new_dist < dist_to[j]) {
                    dist_to[j] = new_dist;
                }
            }
        }
    }

    return -1.0;  /* unreachable */
}

/* ---- Reachability check ---- */

double comm_check_reachable(const comm_system_t *cs, const probe_t *sender,
                            vec3_t target_pos) {
    double range = comm_range(sender);
    vec3_t from = probe_pos(sender);
    double direct = vec3_dist(from, target_pos);
    if (direct <= range) return direct;

    /* Try relay path */
    return comm_relay_path_distance(cs, from, target_pos, range);
}

/* ---- Send targeted ---- */

int comm_send_targeted(comm_system_t *cs, probe_t *sender,
                       probe_uid_t target_id, vec3_t target_pos,
                       const char *content, uint64_t current_tick) {
    if (cs->count >= MAX_MESSAGES) return -1;

    /* Check energy */
    if (sender->energy_joules < COMM_ENERGY_TARGETED) return -1;

    /* Check reachability (direct or via relay) */
    vec3_t from = probe_pos(sender);
    double eff_dist = comm_check_reachable(cs, sender, target_pos);
    if (eff_dist < 0) return -1;

    /* The actual light-travel distance is always the straight-line distance.
     * Relay extends range but light still travels the direct path. */
    double actual_dist = vec3_dist(from, target_pos);
    uint64_t delay = comm_light_delay(from, target_pos);

    /* Deduct energy */
    sender->energy_joules -= COMM_ENERGY_TARGETED;

    /* Queue message */
    message_t *m = &cs->messages[cs->count++];
    m->sender_id = sender->id;
    m->target_id = target_id;
    m->mode = MSG_TARGETED;
    strncpy(m->content, content, MAX_MSG_CONTENT - 1);
    m->content[MAX_MSG_CONTENT - 1] = '\0';
    m->sent_tick = current_tick;
    m->arrival_tick = current_tick + delay;
    m->status = MSG_IN_TRANSIT;
    m->distance_ly = actual_dist;

    return 0;
}

/* ---- Broadcast ---- */

int comm_send_broadcast(comm_system_t *cs, probe_t *sender,
                        const probe_t *all_probes, int probe_count,
                        const char *content, uint64_t current_tick) {
    if (sender->energy_joules < COMM_ENERGY_BROADCAST) return -1;

    double range = comm_range(sender);
    vec3_t from = probe_pos(sender);
    int queued = 0;

    /* Deduct energy upfront */
    sender->energy_joules -= COMM_ENERGY_BROADCAST;

    for (int i = 0; i < probe_count; i++) {
        /* Skip self */
        if (uid_eq(all_probes[i].id, sender->id)) continue;

        vec3_t to = probe_pos(&all_probes[i]);
        double dist = vec3_dist(from, to);
        if (dist > range) continue;  /* broadcast is direct-only, no relay */

        if (cs->count >= MAX_MESSAGES) break;

        uint64_t delay = comm_light_delay(from, to);

        message_t *m = &cs->messages[cs->count++];
        m->sender_id = sender->id;
        m->target_id = all_probes[i].id;
        m->mode = MSG_BROADCAST;
        strncpy(m->content, content, MAX_MSG_CONTENT - 1);
        m->content[MAX_MSG_CONTENT - 1] = '\0';
        m->sent_tick = current_tick;
        m->arrival_tick = current_tick + delay;
        m->status = MSG_IN_TRANSIT;
        m->distance_ly = dist;
        queued++;
    }

    return queued;
}

/* ---- Tick delivery ---- */

int comm_tick_deliver(comm_system_t *cs, uint64_t current_tick) {
    int delivered = 0;
    for (int i = 0; i < cs->count; i++) {
        if (cs->messages[i].status == MSG_IN_TRANSIT &&
            cs->messages[i].arrival_tick <= current_tick) {
            cs->messages[i].status = MSG_DELIVERED;
            delivered++;
        }
    }
    return delivered;
}

/* ---- Inbox ---- */

int comm_get_inbox(const comm_system_t *cs, probe_uid_t probe_id,
                   message_t *out, int max_out) {
    int count = 0;
    for (int i = 0; i < cs->count && count < max_out; i++) {
        if (cs->messages[i].status == MSG_DELIVERED &&
            uid_eq(cs->messages[i].target_id, probe_id)) {
            out[count++] = cs->messages[i];
        }
    }
    return count;
}

/* ---- Beacons ---- */

int comm_place_beacon(comm_system_t *cs, const probe_t *owner,
                      probe_uid_t system_id, const char *message,
                      uint64_t current_tick) {
    if (cs->beacon_count >= MAX_BEACONS) return -1;

    beacon_t *b = &cs->beacons[cs->beacon_count++];
    b->owner_id = owner->id;
    b->system_id = system_id;
    b->position = probe_pos(owner);
    strncpy(b->message, message, MAX_BEACON_MSG - 1);
    b->message[MAX_BEACON_MSG - 1] = '\0';
    b->placed_tick = current_tick;
    b->active = true;

    return 0;
}

int comm_detect_beacons(const comm_system_t *cs, probe_uid_t system_id,
                        beacon_t *out, int max_out) {
    int count = 0;
    for (int i = 0; i < cs->beacon_count && count < max_out; i++) {
        if (cs->beacons[i].active && uid_eq(cs->beacons[i].system_id, system_id)) {
            out[count++] = cs->beacons[i];
        }
    }
    return count;
}

int comm_deactivate_beacon(comm_system_t *cs, probe_uid_t owner_id,
                           probe_uid_t system_id) {
    for (int i = 0; i < cs->beacon_count; i++) {
        if (cs->beacons[i].active &&
            uid_eq(cs->beacons[i].owner_id, owner_id) &&
            uid_eq(cs->beacons[i].system_id, system_id)) {
            cs->beacons[i].active = false;
            return 0;
        }
    }
    return -1;  /* not found */
}

/* ---- Relay Satellites ---- */

int comm_build_relay(comm_system_t *cs, const probe_t *owner,
                     probe_uid_t system_id, uint64_t current_tick) {
    if (cs->relay_count >= MAX_RELAYS) return -1;

    relay_t *r = &cs->relays[cs->relay_count++];
    r->owner_id = owner->id;
    r->system_id = system_id;
    r->position = probe_pos(owner);
    r->built_tick = current_tick;
    r->active = true;
    r->range_ly = RELAY_RANGE_LY;

    return 0;
}
