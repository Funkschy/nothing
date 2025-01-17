#include <stdlib.h>
#include <stdbool.h>

#include "game/camera.h"
#include "game/level/platforms.h"
#include "system/lt.h"
#include "system/nth_alloc.h"
#include "system/stacktrace.h"
#include "system/line_stream.h"
#include "system/str.h"
#include "system/log.h"
#include "hashset.h"

#include "./rigid_bodies.h"

struct RigidBodies
{
    Lt *lt;
    size_t capacity;
    size_t count;

    Rect *bodies;
    Vec *velocities;
    Vec *movements;
    bool *grounded;
    Vec *forces;
    bool *deleted;
    HashSet *collided;
    bool *disabled;
};

RigidBodies *create_rigid_bodies(size_t capacity)
{
    Lt *lt = create_lt();

    RigidBodies *rigid_bodies = PUSH_LT(lt, nth_calloc(1, sizeof(RigidBodies)), free);
    if (rigid_bodies == NULL) {
        RETURN_LT(lt, NULL);
    }
    rigid_bodies->lt = lt;

    rigid_bodies->capacity = capacity;
    rigid_bodies->count = 0;

    rigid_bodies->bodies = PUSH_LT(lt, nth_calloc(capacity, sizeof(Rect)), free);
    if (rigid_bodies->bodies == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->velocities = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->velocities == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->movements = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->movements == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->grounded = PUSH_LT(lt, nth_calloc(capacity, sizeof(bool)), free);
    if (rigid_bodies->grounded == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->forces = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->forces == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->deleted = PUSH_LT(lt, nth_calloc(capacity, sizeof(bool)), free);
    if (rigid_bodies->deleted == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->collided = PUSH_LT(
        lt,
        create_hashset(sizeof(size_t) * 2, capacity * 2),
        destroy_hashset);
    if (rigid_bodies->collided == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->disabled = PUSH_LT(
        lt,
        nth_calloc(capacity, sizeof(bool)),
        free);
    if (rigid_bodies->disabled == NULL) {
        RETURN_LT(lt, NULL);
    }

    return rigid_bodies;
}

void destroy_rigid_bodies(RigidBodies *rigid_bodies)
{
    trace_assert(rigid_bodies);
    RETURN_LT0(rigid_bodies->lt);
}

static int rigid_bodies_collide_with_itself(RigidBodies *rigid_bodies)
{
    trace_assert(rigid_bodies);

    if (rigid_bodies->count == 0) {
        return 0;
    }

    size_t pair[2];
    hashset_clear(rigid_bodies->collided);

    bool the_variable_that_gets_set_when_a_collision_happens_xd = true;

    for (size_t i = 0; i < 1000 && the_variable_that_gets_set_when_a_collision_happens_xd; ++i) {
        the_variable_that_gets_set_when_a_collision_happens_xd = false;
        for (size_t i1 = 0; i1 < rigid_bodies->count - 1; ++i1) {
            if (rigid_bodies->deleted[i1] || rigid_bodies->disabled[i1]) {
                continue;
            }

            for (size_t i2 = i1 + 1; i2 < rigid_bodies->count; ++i2) {
                if (rigid_bodies->deleted[i2] || rigid_bodies->disabled[i1]) {
                    continue;
                }

                if (!rects_overlap(rigid_bodies->bodies[i1], rigid_bodies->bodies[i2])) {
                    continue;
                }

                the_variable_that_gets_set_when_a_collision_happens_xd = true;

                pair[0] = i1;
                pair[1] = i2;
                hashset_insert(rigid_bodies->collided, pair);

                Vec orient = rect_impulse(&rigid_bodies->bodies[i1], &rigid_bodies->bodies[i2]);

                if (orient.x > orient.y) {
                    if (rigid_bodies->bodies[i1].y < rigid_bodies->bodies[i2].y) {
                        rigid_bodies->grounded[i1] = true;
                    } else {
                        rigid_bodies->grounded[i2] = true;
                    }
                }

                rigid_bodies->velocities[i1] = vec(rigid_bodies->velocities[i1].x * orient.x, rigid_bodies->velocities[i1].y * orient.y);
                rigid_bodies->velocities[i2] = vec(rigid_bodies->velocities[i2].x * orient.x, rigid_bodies->velocities[i2].y * orient.y);
                rigid_bodies->movements[i1] = vec(rigid_bodies->movements[i1].x * orient.x, rigid_bodies->movements[i1].y * orient.y);
                rigid_bodies->movements[i2] = vec(rigid_bodies->movements[i2].x * orient.x, rigid_bodies->movements[i2].y * orient.y);

            }
        }
    }

    size_t *collided = hashset_values(rigid_bodies->collided);
    const size_t n = hashset_count(rigid_bodies->collided);
    for (size_t i = 0; i < n; ++i) {
        const size_t i1 = *(collided + i * 2);
        const size_t i2 = *(collided + i * 2 + 1);

        rigid_bodies_apply_force(
            rigid_bodies, i1, vec_sum(rigid_bodies->velocities[i2], rigid_bodies->movements[i2]));
        rigid_bodies_apply_force(
            rigid_bodies, i2, vec_sum(rigid_bodies->velocities[i1], rigid_bodies->movements[i1]));
    }

    return 0;
}

static int rigid_bodies_collide_with_platforms(
    RigidBodies *rigid_bodies,
    const Platforms *platforms)
{
    trace_assert(rigid_bodies);
    trace_assert(platforms);

    int sides[RECT_SIDE_N] = { 0, 0, 0, 0 };

    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        if (rigid_bodies->deleted[i] || rigid_bodies->disabled[i]) {
            continue;
        }

        memset(sides, 0, sizeof(int) * RECT_SIDE_N);

        platforms_touches_rect_sides(platforms, rigid_bodies->bodies[i], sides);

        if (sides[RECT_SIDE_BOTTOM]) {
            rigid_bodies->grounded[i] = true;
        }

        Vec v = platforms_snap_rect(platforms, &rigid_bodies->bodies[i]);
        rigid_bodies->velocities[i] = vec_entry_mult(rigid_bodies->velocities[i], v);
        rigid_bodies->movements[i] = vec_entry_mult(rigid_bodies->movements[i], v);
        rigid_bodies_damper(rigid_bodies, i, vec_entry_mult(v, vec(-16.0f, 0.0f)));
    }

    return 0;
}

int rigid_bodies_collide(RigidBodies *rigid_bodies,
                         const Platforms *platforms)
{
    // TODO(#683): RigidBodies should collide only the bodies that were updated on after a previous collision
    memset(rigid_bodies->grounded, 0, sizeof(bool) * rigid_bodies->count);

    if (rigid_bodies_collide_with_itself(rigid_bodies) < 0) {
        return -1;
    }

    if (rigid_bodies_collide_with_platforms(rigid_bodies, platforms) < 0) {
        return -1;
    }

    return 0;
}

int rigid_bodies_update(RigidBodies *rigid_bodies,
                        RigidBodyId id,
                        float delta_time)
{
    trace_assert(rigid_bodies);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return 0;
    }

    rigid_bodies->velocities[id] = vec_sum(
            rigid_bodies->velocities[id],
            vec_scala_mult(
                rigid_bodies->forces[id],
                delta_time));

    Vec position = vec(rigid_bodies->bodies[id].x,
                       rigid_bodies->bodies[id].y);

    position = vec_sum(
        position,
        vec_scala_mult(
            vec_sum(
                rigid_bodies->velocities[id],
                rigid_bodies->movements[id]),
            delta_time));

    rigid_bodies->bodies[id].x = position.x;
    rigid_bodies->bodies[id].y = position.y;

    rigid_bodies->forces[id] = vec(0.0f, 0.0f);

    return 0;
}

int rigid_bodies_render(RigidBodies *rigid_bodies,
                        RigidBodyId id,
                        Color color,
                        Camera *camera)
{
    trace_assert(rigid_bodies);
    trace_assert(camera);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return 0;
    }

    char text_buffer[256];

    if (camera_fill_rect(
            camera,
            rigid_bodies->bodies[id],
            color) < 0) {
        return -1;
    }

    snprintf(text_buffer, 256, "id: %zd", id);

    if (camera_render_debug_text(
            camera,
            text_buffer,
            vec(rigid_bodies->bodies[id].x,
                rigid_bodies->bodies[id].y)) < 0) {
        return -1;
    }

    snprintf(text_buffer, 256, "p:(%.2f, %.2f)",
             rigid_bodies->bodies[id].x,
             rigid_bodies->bodies[id].y);
    if (camera_render_debug_text(
            camera,
            text_buffer,
            vec(rigid_bodies->bodies[id].x,
                rigid_bodies->bodies[id].y + FONT_CHAR_HEIGHT * 2.0f))) {
        return -1;
    }

    snprintf(text_buffer, 256, "v:(%.2f, %.2f)",
             rigid_bodies->velocities[id].x,
             rigid_bodies->velocities[id].y);
    if (camera_render_debug_text(
            camera,
            text_buffer,
            vec(rigid_bodies->bodies[id].x,
                rigid_bodies->bodies[id].y + FONT_CHAR_HEIGHT * 4.0f))) {
        return -1;
    }

    snprintf(text_buffer, 256, "m:(%.2f, %.2f)",
             rigid_bodies->movements[id].x,
             rigid_bodies->movements[id].y);
    if (camera_render_debug_text(
            camera,
            text_buffer,
            vec(rigid_bodies->bodies[id].x,
                rigid_bodies->bodies[id].y + FONT_CHAR_HEIGHT * 6.0f))) {
        return -1;
    }

    return 0;
}

RigidBodyId rigid_bodies_add(RigidBodies *rigid_bodies,
                             Rect rect)
{
    trace_assert(rigid_bodies);
    trace_assert(rigid_bodies->count < rigid_bodies->capacity);

    RigidBodyId id = rigid_bodies->count++;
    rigid_bodies->bodies[id] = rect;

    return id;
}

void rigid_bodies_remove(RigidBodies *rigid_bodies,
                         RigidBodyId id)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->capacity);

    rigid_bodies->deleted[id] = true;
}

Rect rigid_bodies_hitbox(const RigidBodies *rigid_bodies,
                         RigidBodyId id)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    return rigid_bodies->bodies[id];
}

void rigid_bodies_move(RigidBodies *rigid_bodies,
                       RigidBodyId id,
                       Vec movement)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return;
    }

    rigid_bodies->movements[id] = movement;
}

int rigid_bodies_touches_ground(const RigidBodies *rigid_bodies,
                                RigidBodyId id)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    return rigid_bodies->grounded[id];
}

void rigid_bodies_apply_omniforce(RigidBodies *rigid_bodies,
                                  Vec force)
{
    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        rigid_bodies_apply_force(rigid_bodies, i, force);
    }
}

void rigid_bodies_apply_force(RigidBodies * rigid_bodies,
                              RigidBodyId id,
                              Vec force)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return;
    }

    rigid_bodies->forces[id] = vec_sum(rigid_bodies->forces[id], force);
}

void rigid_bodies_transform_velocity(RigidBodies *rigid_bodies,
                                     RigidBodyId id,
                                     mat3x3 trans_mat)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return;
    }

    rigid_bodies->velocities[id] = point_mat3x3_product(
        rigid_bodies->velocities[id],
        trans_mat);
}

void rigid_bodies_teleport_to(RigidBodies *rigid_bodies,
                              RigidBodyId id,
                              Vec position)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return;
    }

    rigid_bodies->bodies[id].x = position.x;
    rigid_bodies->bodies[id].y = position.y;
}

void rigid_bodies_damper(RigidBodies *rigid_bodies,
                         RigidBodyId id,
                         Vec v)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    if (rigid_bodies->deleted[id] || rigid_bodies->disabled[id]) {
        return;
    }

    rigid_bodies_apply_force(
        rigid_bodies, id,
        vec(
            rigid_bodies->velocities[id].x * v.x,
            rigid_bodies->velocities[id].y * v.y));
}

void rigid_bodies_disable(RigidBodies *rigid_bodies,
                          RigidBodyId id,
                          bool disabled)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    rigid_bodies->disabled[id] = disabled;
}
