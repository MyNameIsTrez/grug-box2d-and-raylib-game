// So VS Code can find "CLOCK_PROCESS_CPUTIME_ID", and so we can use strdup()
#define _POSIX_C_SOURCE 200809L

#include "box2d/box2d.h"
#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define TEXTURE_SCALE 2.0f
#define PIXELS_PER_METER 20.0f // Taken from Cortex Command, where this program's sprites come from: https://github.com/cortex-command-community/Cortex-Command-Community-Project/blob/afddaa81b6d71010db299842d5594326d980b2cc/Source/System/Constants.h#L23
#define MAX_ENTITIES 1000 // Prevents box2d crashing when there's more than 32k overlapping entities, which can happen when the game is paused and the player shoots over 32k bullets
#define FONT_SIZE 10
#define MAX_MEASUREMENTS 420
#define MAX_TYPE_FILES 420420
#define MAX_MESSAGES 10
#define MAX_MESSAGE_LENGTH 420420
#define ERROR_MESSAGE_DURATION_MS 5000
#define ERROR_MESSAGE_FADING_MOMENT_MS 4000
#define NANOSECONDS_PER_SECOND 1000000000L
#define MAX_I32_MAP_ENTRIES 420

typedef int32_t i32;
typedef uint32_t u32;

enum entity_type {
	OBJECT_GUN,
	OBJECT_BULLET,
	OBJECT_GROUND,
	OBJECT_CRATE,
	OBJECT_COUNTER,
};

struct gun_fields {
	i32 ms_per_round_fired;
};

struct bullet_fields {
	float density;
};

struct i32_map {
	char *keys[MAX_I32_MAP_ENTRIES];
	i32 values[MAX_I32_MAP_ENTRIES];

	u32 buckets[MAX_I32_MAP_ENTRIES];
	u32 chains[MAX_I32_MAP_ENTRIES];

	size_t size;
};

struct entity {
	i32 id;
	enum entity_type type;
	b2BodyId body_id;
	b2ShapeId shape_id;
	Texture texture;

	// This has ownership, because grug_resource_reloads[] contains texture paths
	// that start dangling the moment the .so is unloaded
	// There is no way for `streq(entity->texture_path, reload.path)` to work without ownership
	char *texture_path;

	void *dll;

	void *on_fns;
	void *globals;

	bool flippable;
	bool enable_hit_events;

	struct i32_map *i32_map;

	union {
		struct gun_fields gun;
		struct bullet_fields bullet;
	};
};

struct gun {
	char *name;
	char *sprite_path;
	i32 ms_per_round_fired;
	char *companion;
};

struct bullet {
	char *name;
	char *sprite_path;
	float density;
};

struct box {
	char *name;
	char *sprite_path;
	bool static_;
};

struct counter {
	char *name;
};

static struct entity entities[MAX_ENTITIES];
static size_t entities_size;
static size_t drawn_entities;

static int debug_line_number;

static b2WorldId world_id;

static Texture background_texture;

struct measurement {
	struct timespec time;
	char *description;
};

static struct measurement measurements[MAX_MEASUREMENTS];
static size_t measurements_size;

static struct gun gun_definition;
static struct bullet bullet_definition;
static struct box box_definition;
static struct counter counter_definition;

static struct entity *gun;

static struct grug_file *type_files[MAX_TYPE_FILES];
static size_t type_files_size;

static bool debug_info = true;
static bool draw_bounding_box = false;

struct message_data {
	char message[MAX_MESSAGE_LENGTH];
	struct timespec time;
};

static struct message_data messages[MAX_MESSAGES];
static size_t messages_size;
static size_t messages_start;
static char message[MAX_MESSAGE_LENGTH];

static i32 next_entity_id;

static Sound metal_blunt_1;
static Sound metal_blunt_2;

static size_t sound_cooldown_metal_blunt_1;
static size_t sound_cooldown_metal_blunt_2;

static bool paused = false;

struct gun_on_fns {
	void (*spawn)(void *globals, i32 self);
	void (*despawn)(void *globals, i32 self);
	void (*fire)(void *globals, i32 self);
};

struct bullet_on_fns {
	void (*tick)(void *globals, i32 self);
};

struct counter_on_fns {
	void (*tick)(void *globals, i32 self);
};

static void add_message(void);
static void despawn_entity(size_t entity_index);
static i32 spawn_entity(b2BodyDef body_def, enum entity_type type, struct grug_file *file, bool flippable, bool enable_hit_events);

// TODO: Optimize this to O(1), by adding an array that maps
// TODO: the entity ID to the entities[] index
static size_t get_entity_index_from_entity_id(i32 id) {
	for (size_t i = 0; i < entities_size; i++) {
		if (entities[i].id == id) {
			return i;
		}
	}

	snprintf(message, sizeof(message), "Failed to find the entity with ID %d\n", id);
	add_message();

	return SIZE_MAX;
}

// From https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elf.c#l193
static u32 elf_hash(const char *namearg) {
	u32 h = 0;

	for (const unsigned char *name = (const unsigned char *) namearg; *name; name++) {
		h = (h << 4) + *name;
		h ^= (h >> 24) & 0xf0;
	}

	return h & 0x0fffffff;
}

static bool streq(char *a, char *b) {
	return strcmp(a, b) == 0;
}

void game_fn_map_set_i32(i32 id, char *key, i32 value) {
	size_t entity_index = get_entity_index_from_entity_id(id);
	if (entity_index == SIZE_MAX) {
		return;
	}

	struct i32_map *map = entities[entity_index].i32_map;

	u32 bucket_index = elf_hash(key) % MAX_I32_MAP_ENTRIES;

	u32 i = map->buckets[bucket_index];

	while (true) {
		if (i == UINT32_MAX) {
			break;
		}

		if (streq(key, map->keys[i])) {
			break;
		}

		i = map->chains[i];
	}

	if (i == UINT32_MAX) {
		if (map->size >= MAX_I32_MAP_ENTRIES) {
			snprintf(message, sizeof(message), "The i32 map of the entity with ID %d has %d entries, which exceeds MAX_I32_MAP_ENTRIES\n", id, MAX_I32_MAP_ENTRIES);
			add_message();

			return;
		}

		i = map->size;

		map->keys[i] = strdup(key);
		map->values[i] = value;
		map->chains[i] = map->buckets[bucket_index];
		map->buckets[bucket_index] = i;

		map->size++;
	} else {
		free(map->keys[i]);
		map->keys[i] = strdup(key);
		map->values[i] = value;
	}
}

i32 game_fn_map_get_i32(i32 id, char *key) {
	size_t entity_index = get_entity_index_from_entity_id(id);
	if (entity_index == SIZE_MAX) {
		return -1;
	}

	struct i32_map *map = entities[entity_index].i32_map;

	if (map->size == 0) {
		snprintf(message, sizeof(message), "The i32 map of the entity with ID %d is empty, so can't contain the key '%s'\n", id, key);
		add_message();

		return -1;
	}

	u32 i = map->buckets[elf_hash(key) % MAX_I32_MAP_ENTRIES];

	while (true) {
		if (i == UINT32_MAX) {
			snprintf(message, sizeof(message), "The i32 map of the entity with ID %d doesn't contain the key '%s'\n", id, key);
			add_message();

			break;
		}

		if (streq(key, map->keys[i])) {
			return map->values[i];
		}

		i = map->chains[i];
	}

	return -1;
}

bool game_fn_map_has_i32(i32 id, char *key) {
	size_t entity_index = get_entity_index_from_entity_id(id);
	if (entity_index == SIZE_MAX) {
		return false;
	}

	struct i32_map *map = entities[entity_index].i32_map;

	if (map->size == 0) {
		return false;
	}

	u32 i = map->buckets[elf_hash(key) % MAX_I32_MAP_ENTRIES];

	while (true) {
		if (i == UINT32_MAX) {
			break;
		}

		if (streq(key, map->keys[i])) {
			return true;
		}

		i = map->chains[i];
	}

	return false;
}

void game_fn_despawn_entity(i32 id) {
	size_t entity_index = get_entity_index_from_entity_id(id);

	if (entity_index != SIZE_MAX) {
		despawn_entity(entity_index);
	}
}

i32 game_fn_spawn_counter(char *name) {
	b2BodyDef body_def = {0};

	struct grug_file *file = grug_get_entity_file(name);

	return spawn_entity(body_def, OBJECT_COUNTER, file, false, false);
}

void game_fn_play_sound(char *path) {
	Sound sound = LoadSound(path);
	assert(sound.frameCount > 0);

	PlaySound(sound);

	// TODO: This doesn't work here, since it frees the sound before it gets played
	// UnloadSound(sound);
}

void game_fn_print_bool(bool b) {
	snprintf(message, sizeof(message), "%s\n", b ? "true" : "false");
	add_message();
}

void game_fn_print_string(char *s) {
	snprintf(message, sizeof(message), "%s\n", s);
	add_message();
}

void game_fn_print_f32(float f) {
	snprintf(message, sizeof(message), "%f\n", f);
	add_message();
}

void game_fn_print_i32(i32 i) {
	snprintf(message, sizeof(message), "%d\n", i);
	add_message();
}

float game_fn_rand(float min, float max) {
    float range = max - min;
    return min + rand() / (double)RAND_MAX * range;
}

static void spawn_bullet(b2Vec2 pos, float angle, b2Vec2 velocity, struct grug_file *file);

void game_fn_spawn_bullet(char *name, float x, float y, float angle_in_degrees, float velocity_in_meters_per_second) {
	struct grug_file *file = grug_get_entity_file(name);

	file->define_fn();

	char *texture_path = bullet_definition.sprite_path;

	Texture texture = LoadTexture(texture_path);
	assert(texture.id > 0);

	b2Vec2 local_point = {
		.x = gun->texture.width / 2.0f + texture.width / 2.0f + x,
		.y = y
	};
	UnloadTexture(texture);
	b2Vec2 muzzle_pos = b2Body_GetWorldPoint(gun->body_id, local_point);

	b2Rot rot = b2Body_GetRotation(gun->body_id);
	double gun_angle = b2Rot_GetAngle(rot);
	double added_angle = angle_in_degrees * DEG2RAD;
	bool facing_left = (gun_angle > PI / 2) || (gun_angle < -PI / 2);
	rot = b2MakeRot(gun_angle + added_angle * (facing_left ? -1 : 1));

	b2Vec2 velocity_unrotated = (b2Vec2){.x=velocity_in_meters_per_second * PIXELS_PER_METER, .y=0};
	b2Vec2 velocity = b2RotateVector(rot, velocity_unrotated);
	spawn_bullet(muzzle_pos, gun_angle, velocity, file);
}

void game_fn_define_counter(char *name) {
	counter_definition = (struct counter){
		.name = name,
	};
}

void game_fn_define_box(char *name, char *sprite_path, bool static_) {
	box_definition = (struct box){
		.name = name,
		.sprite_path = sprite_path,
		.static_ = static_,
	};
}

void game_fn_define_bullet(char *name, char *sprite_path, float density) {
	bullet_definition = (struct bullet){
		.name = name,
		.sprite_path = sprite_path,
		.density = density,
	};
}

void game_fn_define_gun(char *name, char *sprite_path, i32 rounds_per_minute, char *companion) {
	double rounds_per_second = rounds_per_minute / 60.0;
	double seconds_per_round = 1.0 / rounds_per_second;

	gun_definition = (struct gun){
		.name = name,
		.sprite_path = sprite_path,
		.ms_per_round_fired = seconds_per_round * 1000.0,
		.companion = companion,
	};
}

static double get_elapsed_ms(struct timespec start, struct timespec end) {
	return 1.0e3 * (double)(end.tv_sec - start.tv_sec) + 1.0e-6 * (double)(end.tv_nsec - start.tv_nsec);
}

static void draw_debug_line(const char *text, int x) {
	DrawText(text, x, debug_line_number++ * FONT_SIZE, FONT_SIZE, RAYWHITE);
}

static void draw_debug_line_left(const char *text) {
	draw_debug_line(text, 0);
}

static void draw_debug_line_right(const char *text) {
	draw_debug_line(text, SCREEN_WIDTH - MeasureText(text, FONT_SIZE));
}

static void draw_debug_info(void) {
	debug_line_number = 0;

	draw_debug_line_left(TextFormat("entities: %zu", entities_size));

	draw_debug_line_left(TextFormat("drawn entities: %zu", drawn_entities));

	draw_debug_line_left(TextFormat("grug mode: %s", grug_are_on_fns_in_safe_mode() ? "safe" : "fast"));

	debug_line_number = 0;

	draw_debug_line_right(TextFormat("%.2f ms/frame", get_elapsed_ms(measurements[0].time, measurements[measurements_size - 1].time)));

	for (size_t i = 1; i < measurements_size - 1; i++) {
		struct timespec previous = measurements[i - 1].time;
		struct timespec current = measurements[i].time;
		draw_debug_line_right(TextFormat("%.2f %s", get_elapsed_ms(previous, current), measurements[i].description));
	}
}

static Vector2 world_to_screen(b2Vec2 p) {
	return (Vector2){
		  p.x * TEXTURE_SCALE + SCREEN_WIDTH  / 2.0f,
		- p.y * TEXTURE_SCALE + SCREEN_HEIGHT / 2.0f
	};
}

static void draw_entity(struct entity entity) {
	Texture texture = entity.texture;

	b2Vec2 local_point = {
		-texture.width / 2.0f,
		texture.height / 2.0f
	};

	// Rotates the local_point argument by the entity's angle
	b2Vec2 pos_world = b2Body_GetWorldPoint(entity.body_id, local_point);

	Vector2 pos_screen = world_to_screen(pos_world);

	// Using this would be more accurate for huge textures, but would probably be slower
	// b2AABB aabb = b2Body_ComputeAABB(entity.body_id);
	// Vector2 lower = world_to_screen(aabb.lowerBound);
	// Vector2 upper = world_to_screen(aabb.upperBound);

	float margin = -2.0f * PIXELS_PER_METER;
	float left = pos_screen.x + margin;
	float right = pos_screen.x - margin;
	float top = pos_screen.y + margin;
	float bottom = pos_screen.y - margin;
	if (left > SCREEN_WIDTH || right < 0 || top > SCREEN_HEIGHT || bottom < 0) {
		return;
	}

	b2Rot rot = b2Body_GetRotation(entity.body_id);
	float angle = b2Rot_GetAngle(rot);

	bool facing_left = (angle > PI / 2) || (angle < -PI / 2);
    Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height * (entity.flippable && facing_left ? -1 : 1) };
    Rectangle dest = { pos_screen.x, pos_screen.y, (float)texture.width*TEXTURE_SCALE, (float)texture.height*TEXTURE_SCALE };
    Vector2 origin = { 0.0f, 0.0f };
	float rotation = -angle * RAD2DEG;
	DrawTexturePro(texture, source, dest, origin, rotation, WHITE);

	if (draw_bounding_box) {
		Rectangle rect = {pos_screen.x, pos_screen.y, texture.width * TEXTURE_SCALE, texture.height * TEXTURE_SCALE};
		Color color = {.r=42, .g=42, .b=242, .a=100};
		DrawRectanglePro(rect, origin, -angle * RAD2DEG, color);
	}

	drawn_entities++;
}

static void record(char *description) {
	if (debug_info) {
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &measurements[measurements_size].time);
		measurements[measurements_size++].description = description;
	}
}

static void draw(void) {
	BeginDrawing();
	record("beginning drawing");

	DrawTextureEx(background_texture, Vector2Zero(), 0, 2, WHITE);
	record("drawing background");

	drawn_entities = 0;
	for (size_t i = 0; i < entities_size; i++) {
		struct entity entity = entities[i];

		if (entity.texture.id > 0) {
			draw_entity(entity);
		}
	}
	record("drawing entities");

	// Color red = {.r=242, .g=42, .b=42, .a=255};
	// DrawLine(gun_screen_pos.x, gun_screen_pos.y, mouse_pos.x, mouse_pos.y, red);
	// record("drawing gun line");

	struct timespec current_time;
	clock_gettime(CLOCK_MONOTONIC, &current_time);

	size_t i = 0;
	size_t start = messages_start;
	size_t size = messages_size;
	while (i < size) {
		double elapsed_ms = get_elapsed_ms(messages[(start + i) % MAX_MESSAGES].time, current_time);
		if (elapsed_ms < ERROR_MESSAGE_DURATION_MS) {
			break;
		}

		// Remove the old error message
		messages_start = (messages_start + 1) % MAX_MESSAGES;
		messages_size--;

		i++;
	}
	record("clearing old error messages");

	for (i = 0; i < messages_size; i++) {
		Color color = RAYWHITE;

		double elapsed_ms = get_elapsed_ms(messages[(messages_start + i) % MAX_MESSAGES].time, current_time);
		if (elapsed_ms > ERROR_MESSAGE_FADING_MOMENT_MS) {
			double alpha = 255.0 * (ERROR_MESSAGE_DURATION_MS - elapsed_ms) / (double)(ERROR_MESSAGE_DURATION_MS - ERROR_MESSAGE_FADING_MOMENT_MS);
			if (alpha < 0.0) {
				alpha = 0.0;
			}
			assert(alpha >= 0.0 && alpha <= 255.0);
			color.a = alpha;
		}

		DrawText(messages[(messages_start + i) % MAX_MESSAGES].message, 0, SCREEN_HEIGHT - FONT_SIZE * (messages_size - i), FONT_SIZE, color);
	}
	record("drawing error message");

	record("end");

	if (debug_info) {
		draw_debug_info();
	}

	EndDrawing();
}

static void despawn_entity(size_t entity_index) {
	if (entities[entity_index].texture.id > 0) {
		UnloadTexture(entities[entity_index].texture);

		free(entities[entity_index].texture_path);

		b2DestroyBody(entities[entity_index].body_id);
	}

	struct i32_map *map = entities[entity_index].i32_map;
	for (size_t i = 0; i < map->size; i++) {
		free(map->keys[i]);
	}
	free(map);

	entities[entity_index] = entities[--entities_size];

	if (entities[entity_index].type == OBJECT_GUN) {
		gun = entities + entity_index;
	}

	// If the removed entity wasn't at the very end of the entities array,
	// update entity_index's userdata
	if (entity_index < entities_size && entities[entity_index].texture.id > 0) {
		b2Body_SetUserData(entities[entity_index].body_id, (void *)entity_index);
	}
}

static void play_collision_sound(b2ContactHitEvent *event) {
	// printf("approachSpeed: %f\n", event->approachSpeed);

	float x_normalized = (event->point.x * TEXTURE_SCALE) / (SCREEN_WIDTH / 2); // Between -1.0f and 1.0f
	// printf("x_normalized: %f\n", x_normalized);

	float y_normalized = (event->point.y * TEXTURE_SCALE) / (SCREEN_HEIGHT / 2); // Between -1.0f and 1.0f
	// printf("y_normalized: %f\n", y_normalized);

	float distance = sqrtf(x_normalized * x_normalized + y_normalized * y_normalized);
	// printf("distance: %f\n", distance);

	float audibility = 1.0f;
	if (distance > 0.0f) { // Prevents a later division by 0.0f
		distance *= 5.0f;

		// This considers the game to be a 3D space
		// See https://en.wikipedia.org/wiki/Inverse-square_law
		audibility = 1.0f / (distance * distance); // Between 0.0f and 1.0f

		// This considers the game to be a 2D space
		// audibility = 1.0f / distance; // Between 0.0f and 1.0f

		assert(audibility >= 0.0f);
	}
	// printf("audibility: %f\n", audibility);

	float volume = event->approachSpeed * 0.01f;

	volume *= audibility;

	if (volume > 1.0f) {
		volume = 1.0f;
	}
	if (volume < 0.01f) {
		return;
	}

	Sound sound;
	if (rand() % 2 == 0 && sound_cooldown_metal_blunt_1 == 0) {
		sound = metal_blunt_1;
		sound_cooldown_metal_blunt_1 = 6;
		// sound_volume_metal_blunt_1 = ;
	} else if (sound_cooldown_metal_blunt_2 == 0) {
		sound = metal_blunt_2;
		sound_cooldown_metal_blunt_2 = 6;
		// sound_volume_metal_blunt_2 = ;
	} else {
		return;
	}

	SetSoundVolume(sound, volume);

	float speed = event->approachSpeed * 0.005f;
	// printf("speed: %f\n", speed);
	float min_pitch = 0.5f;
	float max_pitch = 1.5f;
	float pitch = min_pitch + speed;
	// printf("pitch: %f\n", pitch);
	if (pitch > max_pitch) {
		pitch = max_pitch;
	}
	SetSoundPitch(sound, pitch);

	float x_normalized_inverted = -x_normalized; // Because a pan of 1.0f means all the way left, instead of right
	float pan = 0.5f + x_normalized_inverted / 2.0f; // Between 0.0f and 1.0f
	// printf("pan: %f\n", pan);
	SetSoundPan(sound, pan);

	PlaySound(sound);
}

static b2ShapeId add_shape(b2BodyId body_id, Texture texture, bool enable_hit_events, float density) {
	b2ShapeDef shape_def = b2DefaultShapeDef();

	shape_def.enableHitEvents = enable_hit_events;
	shape_def.density = density;

	b2Polygon polygon = b2MakeBox(texture.width / 2.0f, texture.height / 2.0f);

	return b2CreatePolygonShape(body_id, &shape_def, &polygon);
}

static char *get_texture_path(struct entity *entity) {
	switch (entity->type) {
		case OBJECT_GUN:
			return gun_definition.sprite_path;
		case OBJECT_BULLET:
			return bullet_definition.sprite_path;
		case OBJECT_GROUND:
		case OBJECT_CRATE:
			return box_definition.sprite_path;
		case OBJECT_COUNTER:
			break;
	}
	return NULL;
}

static void copy_entity_definition(struct entity *entity) {
	switch (entity->type) {
		case OBJECT_GUN:
			entity->gun.ms_per_round_fired = gun_definition.ms_per_round_fired;
			break;
		case OBJECT_BULLET:
			entity->bullet.density = bullet_definition.density;
			break;
		case OBJECT_GROUND:
		case OBJECT_CRATE:
			break;
		case OBJECT_COUNTER:
			break;
	}
}

static i32 spawn_entity(b2BodyDef body_def, enum entity_type type, struct grug_file *file, bool flippable, bool enable_hit_events) {
	if (entities_size >= MAX_ENTITIES) {
		snprintf(message, sizeof(message), "Won't spawn entity, as there are already %d entities, exceeding MAX_ENTITIES\n", MAX_ENTITIES);
		add_message();

		return -1;
	}

	struct entity *entity = &entities[entities_size];

	*entity = (struct entity){0};

	entity->dll = file->dll;

	entity->globals = malloc(file->globals_size);
	file->init_globals_fn(entity->globals);

	entity->on_fns = file->on_fns;

	entity->type = type;

	file->define_fn();

	copy_entity_definition(entity);

	char *texture_path = get_texture_path(entity);

	// The "counter" entity has no box2d body, nor texture
	if (texture_path) {
		body_def.userData = (void *)entities_size;

		entity->body_id = b2CreateBody(world_id, &body_def);

		entity->flippable = flippable;

		entity->enable_hit_events = enable_hit_events;

		entity->texture = LoadTexture(texture_path);
		assert(entity->texture.id > 0);

		entity->texture_path = strdup(texture_path);

		entity->shape_id = add_shape(entity->body_id, entity->texture, enable_hit_events, type == OBJECT_BULLET ? entity->bullet.density : 1.0f);
	}

	entity->i32_map = malloc(sizeof(*entity->i32_map));
	memset(entity->i32_map->buckets, 0xff, MAX_I32_MAP_ENTRIES * sizeof(u32));
	entity->i32_map->size = 0;

	entity->id = next_entity_id;
	if (entity->id == INT32_MAX) {
		next_entity_id = 0;
	} else {
		next_entity_id++;
	}

	entities_size++;

	return entity->id;
}

static void spawn_bullet(b2Vec2 pos, float angle, b2Vec2 velocity, struct grug_file *file) {
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.type = b2_dynamicBody;
	body_def.position = pos;
	body_def.rotation = b2MakeRot(angle);
	body_def.linearVelocity = velocity;

	spawn_entity(body_def, OBJECT_BULLET, file, false, true);
}

static void spawn_companion(char *name) {
	struct grug_file *file = grug_get_entity_file(name);

	file->define_fn();

	b2BodyDef body_def = b2DefaultBodyDef();
	if (!box_definition.static_) {
		body_def.type = b2_dynamicBody;
	}
	body_def.position = (b2Vec2){ 50.0f, 100.0f };

	spawn_entity(body_def, OBJECT_CRATE, file, false, true);
}

static struct entity *spawn_gun(struct grug_file *file, b2Vec2 pos) {
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.position = pos;

	struct entity *gun_entity = entities + entities_size;
	spawn_entity(body_def, OBJECT_GUN, file, true, false);

	file->define_fn();
	spawn_companion(gun_definition.companion);

	return gun_entity;
}

static void spawn_crates(struct grug_file *file) {
	int spawned_crate_count = 160;

	file->define_fn();
	char *texture_path = box_definition.sprite_path;
	Texture texture = LoadTexture(texture_path);
	assert(texture.id > 0);

	for (int i = 0; i < spawned_crate_count; i++) {
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_dynamicBody;
		body_def.position = (b2Vec2){ -100.0f, (i - spawned_crate_count / 2) * texture.height + 1000.0f };

		spawn_entity(body_def, OBJECT_CRATE, file, false, true);
	}

	UnloadTexture(texture);
}

static void spawn_ground(struct grug_file *file) {
	int ground_entity_count = 16;

	file->define_fn();
	char *texture_path = box_definition.sprite_path;
	Texture texture = LoadTexture(texture_path);
	assert(texture.id > 0);

	for (int i = 0; i < ground_entity_count; i++) {
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.position = (b2Vec2){ (i - ground_entity_count / 2) * texture.width, -100.0f };

		spawn_entity(body_def, OBJECT_GROUND, file, false, false);
	}

	UnloadTexture(texture);
}

static void push_file_containing_fn(struct grug_file *file) {
	if (type_files_size + 1 > MAX_TYPE_FILES) {
		fprintf(stderr, "There are more than %d files containing the requested type, exceeding MAX_TYPE_FILES", MAX_TYPE_FILES);
		exit(EXIT_FAILURE);
	}
	type_files[type_files_size++] = file;
}

static void update_type_files_impl(struct grug_mod_dir dir, char *define_type) {
	for (size_t i = 0; i < dir.dirs_size; i++) {
		update_type_files_impl(dir.dirs[i], define_type);
	}
	for (size_t i = 0; i < dir.files_size; i++) {
		if (streq(define_type, dir.files[i].define_type)) {
			push_file_containing_fn(&dir.files[i]);
		}
	}
}

static struct grug_file **get_type_files(char *define_type) {
	type_files_size = 0;
	update_type_files_impl(grug_mods, define_type);
	return type_files;
}

static void add_message(void) {
	struct message_data *error = &messages[(messages_start + messages_size) % MAX_MESSAGES];

	if (messages_size < MAX_MESSAGES) {
		messages_size++;
	} else {
		// We'll be overwriting the oldest message
		messages_start = (messages_start + 1) % MAX_MESSAGES;
	}

	memcpy(error->message, message, strlen(message) + 1);

	clock_gettime(CLOCK_MONOTONIC, &error->time);
}

static void reload_entity_shape(struct entity *entity, char *texture_path) {
	printf("Reloading entity shape %s\n", texture_path);

	UnloadTexture(entity->texture);

	// Retrying this in a loop is necessary for GIMP,
	// since it doesn't write all bytes at once,
	// causing LoadTexture() to sporadically fail
	size_t attempts = 0;
	do {
		entity->texture = LoadTexture(texture_path);
		attempts++;
	} while (entity->texture.id == 0);
	printf("The reloaded entity's new texture took %zu attempt%s to load succesfully\n", attempts, attempts == 1 ? "" : "s");

	free(entity->texture_path);
	entity->texture_path = strdup(texture_path);

	b2DestroyShape(entity->shape_id);
	entity->shape_id = add_shape(entity->body_id, entity->texture, entity->enable_hit_events, entity->type == OBJECT_BULLET ? entity->bullet.density : 1.0f);
}

static void reload_entity(struct entity *entity, struct grug_file *file) {
	entity->dll = file->dll;

	free(entity->globals);
	entity->globals = malloc(file->globals_size);
	file->init_globals_fn(entity->globals);

	entity->on_fns = file->on_fns;

	file->define_fn();

	copy_entity_definition(entity);

	char *texture_path = get_texture_path(entity);

	if (texture_path) {
		reload_entity_shape(entity, texture_path);
	}
}

static void reload_gun(struct grug_file *gun_file) {
	struct gun_on_fns *on_fns = gun->on_fns;
	if (on_fns->despawn) {
		on_fns->despawn(gun->globals, gun->id);
	}

	reload_entity(gun, gun_file);

	gun_file->define_fn();
	spawn_companion(gun_definition.companion);

	on_fns = gun->on_fns; // This is necessary due to the reload_entity() call
	if (on_fns->spawn) {
		on_fns->spawn(gun->globals, gun->id);
	}
}

static void reload_modified_resources(void) {
	for (size_t i = 0; i < grug_resource_reloads_size; i++) {
		struct grug_modified_resource reload = grug_resource_reloads[i];

		printf("Reloading resource %s\n", reload.path);

		for (size_t entity_index = 0; entity_index < entities_size; entity_index++) {
			struct entity *entity = &entities[entity_index];

			if (entity->texture_path && streq(entity->texture_path, reload.path)) {
				reload_entity_shape(entity, reload.path);
			}
		}
	}
}

static void reload_modified_entities(void) {
	for (size_t i = 0; i < grug_reloads_size; i++) {
		struct grug_modified reload = grug_reloads[i];

		printf("Reloading %s\n", reload.path);

		for (size_t entity_index = 0; entity_index < entities_size; entity_index++) {
			struct entity *entity = &entities[entity_index];

			if (reload.old_dll == entity->dll) {
				reload_entity(entity, reload.file);
			}
		}
	}
}

static void update(struct timespec *previous_round_fired_time) {
	measurements_size = 0;
	record("start");

	if (grug_regenerate_modified_mods()) {
		snprintf(message, sizeof(message), "grug loading error: %s, in %s (detected in grug.c:%d)\n", grug_error.msg, grug_error.path, grug_error.grug_c_line_number);
		add_message();

		draw();

		// Slows regeneration attempts down,
		// which was necessary for my university's network-synced file system
		// struct timespec req = {
		// 	.tv_sec = 0,
		// 	.tv_nsec = 0.1 * NANOSECONDS_PER_SECOND,
		// };
		// nanosleep(&req, NULL);

		return;
	}
	record("mod regeneration");

	reload_modified_entities();
	record("reloading entities");

	reload_modified_resources();
	record("reloading resources");

	static size_t gun_index = 0;

	struct grug_file *gun_file = get_type_files("gun")[gun_index];
	size_t gun_count = type_files_size;

	struct grug_file **box_files = get_type_files("box");

	struct grug_file *concrete_file = NULL;
	// Use the first static box
	for (size_t i = 0; i < type_files_size; i++) {
		box_files[i]->define_fn();
		if (box_definition.static_) {
			concrete_file = box_files[i];
			break;
		}
	}
	assert(concrete_file && "There must be at least one static type of box, cause we want to form a floor");

	struct grug_file *crate_file = NULL;
	// Use the first non-static box
	for (size_t i = 0; i < type_files_size; i++) {
		box_files[i]->define_fn();
		if (!box_definition.static_) {
			crate_file = box_files[i];
			break;
		}
	}
	assert(crate_file && "There must be at least one non-static type of box, cause we want to have crates that can fall down");

	static bool initialized = false;
	if (!initialized) {
		initialized = true;

		b2Vec2 pos = { 100.0f, 0 };

		gun = spawn_gun(gun_file, pos);

		free(gun->globals);
		gun->globals = malloc(gun_file->globals_size);
		gun_file->init_globals_fn(gun->globals);

		struct gun_on_fns *on_fns = gun->on_fns;
		if (on_fns->spawn) {
			on_fns->spawn(gun->globals, gun->id);
		}

		spawn_ground(concrete_file);
		spawn_crates(crate_file);
	}

	float mouse_movement = GetMouseWheelMove();
	if (mouse_movement > 0) {
		gun_index++;
		gun_index %= gun_count;
		gun_file = get_type_files("gun")[gun_index];
		reload_gun(gun_file);
	}
	if (mouse_movement < 0) {
		gun_index--;
		gun_index %= gun_count;
		gun_file = get_type_files("gun")[gun_index];
		reload_gun(gun_file);
	}

	if (IsKeyPressed(KEY_B)) {
		draw_bounding_box = !draw_bounding_box;
	}
	// Clear bullets and crates
	if (IsKeyPressed(KEY_C)) {
		for (size_t i = entities_size; i > 0; i--) {
			enum entity_type type = entities[i - 1].type;
			if (type == OBJECT_BULLET || type == OBJECT_CRATE) {
				despawn_entity(i - 1);
			}
		}
	}
	// Toggle drawing and measuring debug info
	if (IsKeyPressed(KEY_D)) {
		debug_info = !debug_info;
	}
	if (IsKeyPressed(KEY_F)) {
		grug_toggle_on_fns_mode();
	}
	if (IsKeyPressed(KEY_P)) {
		paused = !paused;
	}
	if (IsKeyPressed(KEY_S)) {
		spawn_crates(crate_file);
	}

	if (!paused) {
		float deltaTime = GetFrameTime();
		b2World_Step(world_id, deltaTime, 4);
		record("world step");

		static bool out_of_bounds_entities[MAX_ENTITIES];
		memset(out_of_bounds_entities, false, sizeof(out_of_bounds_entities));
		record("clearing out_of_bounds_entities");

		b2BodyEvents events = b2World_GetBodyEvents(world_id);
		for (i32 i = 0; i < events.moveCount; i++) {
			b2BodyMoveEvent *event = events.moveEvents + i;
			// Remove entities that end up below the screen
			if (event->transform.p.y < -SCREEN_HEIGHT / 2.0f / TEXTURE_SCALE - 100.0f) {
				out_of_bounds_entities[(size_t)event->userData] = true;
			}
		}
		record("getting body events");

		if (sound_cooldown_metal_blunt_1 > 0) {
			sound_cooldown_metal_blunt_1--;
		}
		if (sound_cooldown_metal_blunt_2 > 0) {
			sound_cooldown_metal_blunt_2--;
		}
		b2ContactEvents contactEvents = b2World_GetContactEvents(world_id);
		for (i32 i = 0; i < contactEvents.hitCount; i++) {
			b2ContactHitEvent *event = &contactEvents.hitEvents[i];
			// printf("Hit event!\n");
			play_collision_sound(event);
		}
		record("collision handling");

		// This is O(n), but should be fast enough in practice
		for (size_t i = entities_size; i > 0; i--) {
			if (out_of_bounds_entities[i - 1]) {
				despawn_entity(i - 1);
			}
		}
		record("removing entities");
	}

	Vector2 mouse_pos = GetMousePosition();
	b2Vec2 gun_world_pos = b2Body_GetPosition(gun->body_id);
	Vector2 gun_screen_pos = world_to_screen(gun_world_pos);
	Vector2 gun_to_mouse = Vector2Subtract(mouse_pos, gun_screen_pos);
	double gun_angle = atan2(-gun_to_mouse.y, gun_to_mouse.x);
	record("calculating gun_angle");

	struct timespec current_time;
	clock_gettime(CLOCK_MONOTONIC, &current_time);

	double elapsed_ms = get_elapsed_ms(*previous_round_fired_time, current_time);
	bool can_fire = elapsed_ms > gun->gun.ms_per_round_fired;
	if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && can_fire) {
		*previous_round_fired_time = current_time;

		struct gun_on_fns *on_fns = gun->on_fns;
		if (on_fns->fire) {
			record("deciding whether to fire");
			on_fns->fire(gun->globals, gun->id);
			record("calling the gun's on_fire()");
		}
	}

	for (size_t entity_index = 0; entity_index < entities_size; entity_index++) {
		struct entity *entity = &entities[entity_index];

		if (entity->type == OBJECT_BULLET) {
			struct bullet_on_fns *on_fns = entity->on_fns;
			if (on_fns->tick) {
				on_fns->tick(entity->globals, entity->id);
			}
		} else if (entity->type == OBJECT_COUNTER) {
			struct counter_on_fns *on_fns = entity->on_fns;
			if (on_fns->tick) {
				on_fns->tick(entity->globals, entity->id);
			}
		}
	}
	record("calling bullets and counters their on_tick()");

	b2Body_SetTransform(gun->body_id, gun_world_pos, b2MakeRot(gun_angle));
	record("point gun to mouse");

	draw();
}

static void runtime_error_handler(char *reason, enum grug_runtime_error_type type, char *on_fn_name, char *on_fn_path) {
	(void)type;

	snprintf(message, sizeof(message), "grug runtime error in %s(): %s, in %s\n", on_fn_name, reason, on_fn_path);
	add_message();

	draw();
}

int main(void) {
	// SetTargetFPS(60);

	grug_set_runtime_error_handler(runtime_error_handler);

	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "box2d-raylib");

	b2SetLengthUnitsPerMeter(PIXELS_PER_METER);

	b2WorldDef world_def = b2DefaultWorldDef();
	world_def.gravity.y = -9.8f * PIXELS_PER_METER;
	// world_def.hitEventThreshold = 0.1f;
	world_id = b2CreateWorld(&world_def);

	background_texture = LoadTexture("background.png");
	assert(background_texture.id > 0);

	InitAudioDevice();

	metal_blunt_1 = LoadSound("MetalBlunt1.wav");
	assert(metal_blunt_1.frameCount > 0);
	metal_blunt_2 = LoadSound("MetalBlunt2.wav");
	assert(metal_blunt_2.frameCount > 0);

	struct timespec previous_round_fired_time;
	clock_gettime(CLOCK_MONOTONIC, &previous_round_fired_time);

	while (!WindowShouldClose()) {
		update(&previous_round_fired_time);
	}

	// TODO: Are these necessary?
	UnloadTexture(background_texture);
	for (size_t i = 0; i < entities_size; i++) {
		UnloadTexture(entities[i].texture);
	}
	UnloadSound(metal_blunt_1);
	UnloadSound(metal_blunt_2);
	CloseAudioDevice();
	CloseWindow();
}
