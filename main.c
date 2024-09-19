// Solely so stupid VS Code can find "CLOCK_PROCESS_CPUTIME_ID"
#define _POSIX_C_SOURCE 199309L

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

enum entity_type {
	OBJECT_GUN,
	OBJECT_BULLET,
	OBJECT_GROUND,
	OBJECT_CRATE,
};

struct gun_fields {
	int32_t ms_per_round_fired;
	bool full_auto;
};

struct bullet_fields {
	float density;
};

struct box_fields {
	bool static_; // TODO: USE THIS!
};

struct entity {
	int32_t id;
	enum entity_type type;
	b2BodyId body_id;
	b2ShapeId shape_id;
	Texture texture;

	// This being an array instead of a ptr is necessary for taking ownership
	// Ownership is needed, because grug_resource_reloads[] contains texture paths
	// that start dangling the moment the .so is unloaded
	// There is no way for `streq(entity->texture_path, reload.path)` to work without ownership
	char texture_path[4096];

	void *dll;

	void *on_fns;
	void *globals;

	bool flippable;
	bool enable_hit_events;

	union {
		struct gun_fields gun;
		struct bullet_fields bullet;
		struct box_fields box;
	};
};

struct gun {
	char *name;
	char *sprite_path;
	int32_t ms_per_round_fired;
	bool full_auto;
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

static struct entity *gun;

static struct grug_file *type_files[MAX_TYPE_FILES];
static size_t type_files_size;

static bool debug_info = true;

struct message_data {
	char message[MAX_MESSAGE_LENGTH];
	struct timespec time;
};

static struct message_data messages[MAX_MESSAGES];
static size_t messages_size;
static size_t messages_start;
static char message[MAX_MESSAGE_LENGTH];

static int32_t next_entity_id;

struct gun_on_fns {
	void (*fire)(void *globals, int32_t self);
};

struct bullet_on_fns {
	void (*tick)(void *globals, int32_t self);
};

static void add_message(void);

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

void game_fn_print_i32(int32_t i) {
	snprintf(message, sizeof(message), "%d\n", i);
	add_message();
}

float game_fn_rand(float min, float max) {
    float range = max - min;
    return min + rand() / (double)RAND_MAX * range;
}

static void spawn_bullet(b2Vec2 pos, float angle, b2Vec2 velocity, struct grug_file *file);

static bool streq(char *a, char *b) {
	return strcmp(a, b) == 0;
}

void game_fn_spawn_bullet(char *name, float x, float y, float angle_in_degrees, float velocity_in_meters_per_second) {
	struct grug_file *file = grug_get_entity_file(name);

	if (!streq(file->define_type, "bullet")) {
		snprintf(message, sizeof(message), "The spawn_bullet() game function expected a bullet, but got '%s'\n", file->define_type);
		add_message();
		return;
	}

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

void game_fn_define_gun(char *name, char *sprite_path, int32_t rounds_per_minute, bool full_auto) {
	double rounds_per_second = rounds_per_minute / 60.0;
	double seconds_per_round = 1.0 / rounds_per_second;

	gun_definition = (struct gun){
		.name = name,
		.sprite_path = sprite_path,
		.ms_per_round_fired = seconds_per_round * 1000.0,
		.full_auto = full_auto,
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

	// Draws the bounding box
	// Rectangle rect = {pos_screen.x, pos_screen.y, texture.width * TEXTURE_SCALE, texture.height * TEXTURE_SCALE};
	// Color color = {.r=42, .g=42, .b=242, .a=100};
	// DrawRectanglePro(rect, origin, -angle * RAD2DEG, color);

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
		draw_entity(entities[i]);
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

static void remove_entity(size_t entity_index) {
	b2DestroyBody(entities[entity_index].body_id);

	UnloadTexture(entities[entity_index].texture);

	entities[entity_index] = entities[--entities_size];

	if (entities[entity_index].type == OBJECT_GUN) {
		gun = entities + entity_index;
	}

	// If the removed entity wasn't at the very end of the entities array,
	// update entity_index's userdata
	if (entity_index < entities_size) {
		b2Body_SetUserData(entities[entity_index].body_id, (void *)entity_index);
	}
}

static b2ShapeId add_shape(b2BodyId body_id, Texture texture, bool enable_hit_events, float density) {
	b2ShapeDef shape_def = b2DefaultShapeDef();

	shape_def.enableHitEvents = enable_hit_events;
	shape_def.density = density;

	b2Polygon polygon = b2MakeBox(texture.width / 2.0f, texture.height / 2.0f);

	return b2CreatePolygonShape(body_id, &shape_def, &polygon);
}

static char *copy_entity_definition_and_get_texture_path(struct entity *entity) {
	char *texture_path;

	if (entity->type == OBJECT_GUN) {
		texture_path = gun_definition.sprite_path;
		entity->gun.ms_per_round_fired = gun_definition.ms_per_round_fired;
		entity->gun.full_auto = gun_definition.full_auto;
	} else if (entity->type == OBJECT_BULLET) {
		texture_path = bullet_definition.sprite_path;
		entity->bullet.density = bullet_definition.density;
	} else if (entity->type == OBJECT_CRATE || entity->type == OBJECT_GROUND) {
		texture_path = box_definition.sprite_path;
		entity->box.static_ = box_definition.static_;
	}

	return texture_path;
}

static void spawn_entity(b2BodyDef body_def, enum entity_type type, struct grug_file *file, bool flippable, bool enable_hit_events) {
	if (entities_size >= MAX_ENTITIES) {
		return;
	}

	body_def.userData = (void *)entities_size;

	struct entity *entity = &entities[entities_size];

	entity->dll = file->dll;

	entity->globals = malloc(file->globals_size);
	file->init_globals_fn(entity->globals);

	entity->on_fns = file->on_fns;

	entity->body_id = b2CreateBody(world_id, &body_def);

	entity->type = type;

	entity->flippable = flippable;

	entity->enable_hit_events = enable_hit_events;

	file->define_fn();

	char *texture_path = copy_entity_definition_and_get_texture_path(entity);

	entity->texture = LoadTexture(texture_path);
	assert(entity->texture.id > 0);

	strcpy(entity->texture_path, texture_path);

	entity->shape_id = add_shape(entity->body_id, entity->texture, enable_hit_events, type == OBJECT_BULLET ? entity->bullet.density : 1.0f);

	entity->id = next_entity_id;
	if (entity->id == INT32_MAX) {
		next_entity_id = 0;
	} else {
		next_entity_id++;
	}

	entities_size++;
}

static void spawn_bullet(b2Vec2 pos, float angle, b2Vec2 velocity, struct grug_file *file) {
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.type = b2_dynamicBody;
	body_def.position = pos;
	body_def.rotation = b2MakeRot(angle);
	body_def.linearVelocity = velocity;

	spawn_entity(body_def, OBJECT_BULLET, file, false, true);
}

static struct entity *spawn_gun(struct grug_file *file, b2Vec2 pos) {
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.position = pos;

	spawn_entity(body_def, OBJECT_GUN, file, true, false);

	return entities + entities_size - 1;
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

	strncpy(error->message, message, MAX_MESSAGE_LENGTH);

	clock_gettime(CLOCK_MONOTONIC, &error->time);
}

static void reload_entity_shape(struct entity *entity, char *texture_path) {
	printf("Reloading entity shape %s\n", texture_path);

	UnloadTexture(entity->texture);

	// Retrying this in a loop is necessary for GIMP,
	// since it doesn't write all bytes at once,
	// causing the LoadTexture() after this loop to sporadically fail
	size_t attempts = 0;
	do {
		entity->texture = LoadTexture(texture_path);
		attempts++;
	} while (entity->texture.id == 0);
	printf("The reloaded entity's new texture took %zu attempt%s to load succesfully\n", attempts, attempts == 1 ? "" : "s");

	strcpy(entity->texture_path, texture_path);

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

	char *texture_path = copy_entity_definition_and_get_texture_path(entity);

	reload_entity_shape(entity, texture_path);
}

static void reload_modified_grug_resources(void) {
	for (size_t i = 0; i < grug_resource_reloads_size; i++) {
		struct grug_modified_resource reload = grug_resource_reloads[i];

		printf("Reloading resource %s\n", reload.path);

		for (size_t entity_index = 0; entity_index < entities_size; entity_index++) {
			struct entity *entity = &entities[entity_index];

			if (streq(entity->texture_path, reload.path)) {
				reload_entity_shape(entity, reload.path);
			}
		}
	}
}

static void reload_modified_grug_entities(void) {
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

int main(void) {
	// SetTargetFPS(60);

	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "box2d-raylib");

	b2SetLengthUnitsPerMeter(PIXELS_PER_METER);

	b2WorldDef world_def = b2DefaultWorldDef();
	world_def.gravity.y = -9.8f * PIXELS_PER_METER;
	world_id = b2CreateWorld(&world_def);

	background_texture = LoadTexture("background.png");
	assert(background_texture.id > 0);

	bool paused = false;

	bool initialized = false;

	struct timespec previous_round_fired_time;
	clock_gettime(CLOCK_MONOTONIC, &previous_round_fired_time);

	size_t gun_index = 0;

	while (!WindowShouldClose()) {
		measurements_size = 0;
		record("start");

		if (grug_mod_had_runtime_error()) {
			snprintf(message, sizeof(message), "Runtime error: %s\n", grug_get_runtime_error_reason());
			add_message();
			snprintf(message, sizeof(message), "Error occurred when the game called %s(), from %s\n", grug_on_fn_name, grug_on_fn_path);
			add_message();

			draw();

			// struct timespec req = {
			// 	.tv_sec = 0,
			// 	.tv_nsec = 0.1 * NANOSECONDS_PER_SECOND,
			// };
			// nanosleep(&req, NULL);

			continue;
		}
		record("checking for runtime error");

		if (grug_regenerate_modified_mods()) {
			// snprintf(message, sizeof(message), "Loading error: %s:%d: %s\n", grug_error.path, grug_error.line_number, grug_error.msg);
			snprintf(message, sizeof(message), "Loading error: %s:%d: %s (grug.c:%d)\n", grug_error.path, grug_error.line_number, grug_error.msg, grug_error.grug_c_line_number);
			add_message();

			draw();

			// struct timespec req = {
			// 	.tv_sec = 0,
			// 	.tv_nsec = 0.1 * NANOSECONDS_PER_SECOND,
			// };
			// nanosleep(&req, NULL);

			continue;
		}
		record("mod regeneration");

		reload_modified_grug_entities();
		record("reloading entities");

		reload_modified_grug_resources();
		record("reloading resources");

		struct grug_file *gun_file = get_type_files("gun")[gun_index];
		size_t gun_count = type_files_size;

		struct grug_file **box_files = get_type_files("box");

		// TODO: Stop having these indices hardcoded
		struct grug_file *concrete_file = box_files[0];
		struct grug_file *crate_file = box_files[1];

		if (!initialized) {
			initialized = true;

			b2Vec2 pos = { 100.0f, 0 };

			gun = spawn_gun(gun_file, pos);

			free(gun->globals);
			gun->globals = malloc(gun_file->globals_size);
			gun_file->init_globals_fn(gun->globals);

			spawn_ground(concrete_file);
			spawn_crates(crate_file);
		}

		float mouse_movement = GetMouseWheelMove();
		if (mouse_movement > 0) {
			gun_index++;
			gun_index %= gun_count;

			reload_entity(gun, gun_file);
		}
		if (mouse_movement < 0) {
			gun_index--;
			gun_index %= gun_count;

			reload_entity(gun, gun_file);
		}

		if (IsKeyPressed(KEY_D)) { // Toggle drawing and measuring debug info
			debug_info = !debug_info;
		}
		if (IsKeyPressed(KEY_P)) {
			paused = !paused;
		}
		if (IsKeyPressed(KEY_S)) {
			spawn_crates(crate_file);
		}
		if (IsKeyPressed(KEY_C)) { // Clear bullets and crates
			for (size_t i = entities_size; i > 0; i--) {
				enum entity_type type = entities[i - 1].type;
				if (type == OBJECT_BULLET || type == OBJECT_CRATE) {
					remove_entity(i - 1);
				}
			}
		}

		if (!paused) {
			float deltaTime = GetFrameTime();
			b2World_Step(world_id, deltaTime, 4);
			record("world step");

			static bool removed_entities[MAX_ENTITIES];
			memset(removed_entities, false, sizeof(removed_entities));
			record("clearing removed_entities");

			b2BodyEvents events = b2World_GetBodyEvents(world_id);
			for (int32_t i = 0; i < events.moveCount; i++) {
				b2BodyMoveEvent *event = events.moveEvents + i;
				// Remove entities that end up below the screen
				if (event->transform.p.y < -SCREEN_HEIGHT / 2.0f / TEXTURE_SCALE - 100.0f) {
					removed_entities[(size_t)event->userData] = true;
				}
			}
			record("getting body events");

			// This is O(n), but should be fast enough in practice
			for (size_t i = entities_size; i > 0; i--) {
				if (removed_entities[i - 1]) {
					remove_entity(i - 1);
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

		double elapsed_ms = get_elapsed_ms(previous_round_fired_time, current_time);
		bool can_fire = elapsed_ms > gun->gun.ms_per_round_fired;
		if ((gun->gun.full_auto ? IsMouseButtonDown(MOUSE_BUTTON_LEFT) : IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) && can_fire) {
			previous_round_fired_time = current_time;

			struct gun_on_fns *on_fns = gun->on_fns;
			if (on_fns->fire) {
				on_fns->fire(gun->globals, gun->id);
			}
		}
		record("calling the gun's on_fire()");

		for (size_t entity_index = 0; entity_index < entities_size; entity_index++) {
			struct entity *entity = &entities[entity_index];

			if (entity->type == OBJECT_BULLET) {
				struct bullet_on_fns *on_fns = entity->on_fns;
				if (on_fns->tick) {
					on_fns->tick(entity->globals, entity->id);
				}
			}
		}
		record("calling bullets their on_tick()");

		b2Body_SetTransform(gun->body_id, gun_world_pos, b2MakeRot(gun_angle));
		record("point gun to mouse");

		draw();
	}

	// TODO: Are these necessary?
	UnloadTexture(background_texture);
	for (size_t i = 0; i < entities_size; i++) {
		UnloadTexture(entities[i].texture);
	}

	// TODO: Is this necessary?
	CloseWindow();
}
