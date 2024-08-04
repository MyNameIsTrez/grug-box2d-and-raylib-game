#include "box2d/box2d.h"
#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define TEXTURE_SCALE 2.0f
#define PIXELS_PER_METER 20.0f // Taken from Cortex Command, where this program's sprites come from: https://github.com/cortex-command-community/Cortex-Command-Community-Project/blob/afddaa81b6d71010db299842d5594326d980b2cc/Source/System/Constants.h#L23
#define BULLET_VELOCITY 42.0f // In m/s
#define MAX_ENTITIES 420420

enum entity_type {
	OBJECT_GUN,
	OBJECT_BULLET,
	OBJECT_GROUND,
	OBJECT_CRATE,
};

struct entity {
	enum entity_type type;
	b2BodyId body_id;
	Texture texture;
	bool flippable;
};

struct gun {
	char *name;
	int32_t rate_of_fire;
	bool full_auto;
};

static struct entity entities[MAX_ENTITIES];
static size_t entities_size;

static b2WorldId world_id;

static Texture crate_texture;
static Texture concrete_texture;
static Texture bullet_texture;

static struct gun gun_definition;

float game_fn_get_angle(int32_t entity_id) {
	(void)entity_id;
	// TODO: Implement
	return 4.2f;
}

float game_fn_get_muzzle_y(int32_t entity_id) {
	(void)entity_id;
	// TODO: Implement
	return 4.2f;
}

float game_fn_get_muzzle_x(int32_t entity_id) {
	(void)entity_id;
	// TODO: Implement
	return 4.2f;
}

int32_t game_fn_get_milliseconds_since_spawn(int32_t entity_id) {
	(void)entity_id;
	// TODO: Implement
	return 42;
}

void game_fn_spawn_bullet(char *name, int32_t x, int32_t y, float angle_in_radians, float velocity_in_meters_per_second) {
	(void)name;
	(void)x;
	(void)y;
	(void)angle_in_radians;
	(void)velocity_in_meters_per_second;
	// TODO: Implement
}

void game_fn_define_gun(char *name, int32_t rate_of_fire, bool full_auto) {
	gun_definition = (struct gun){
		.name = name,
		.rate_of_fire = rate_of_fire,
		.full_auto = full_auto,
	};
}

static void draw_debug_info(void) {
	DrawFPS(0, 0);

	// mspf doesn't get averaged here, unlike DrawFPS()
	float mspf = GetFrameTime() * 1000;
	DrawText(TextFormat("%.2f MSPF", mspf), 0, 20, 20, LIME);

	DrawText(TextFormat("%zu entities", entities_size), 0, 40, 20, LIME);
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
}

static void spawn_entity(b2BodyDef body_def, enum entity_type type, Texture texture, bool flippable) {
	b2BodyId body_id = b2CreateBody(world_id, &body_def);

	b2ShapeDef shape_def = b2DefaultShapeDef();
	b2Polygon polygon = b2MakeBox(texture.width / 2.0f, texture.height / 2.0f);
	b2CreatePolygonShape(body_id, &shape_def, &polygon);

	entities[entities_size++] = (struct entity){
		.type = type,
		.body_id = body_id,
		.texture = texture,
		.flippable = flippable,
	};
}

static void spawn_bullet(b2Vec2 pos, float angle, b2Vec2 velocity, Texture texture) {
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.type = b2_dynamicBody;
	body_def.position = pos;
	body_def.rotation = b2MakeRot(angle);
	body_def.linearVelocity = velocity;
	body_def.userData = (void *)entities_size;

	spawn_entity(body_def, OBJECT_BULLET, texture, false);
}

static struct entity *spawn_gun(b2Vec2 pos, Texture texture) {
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.position = pos;
	body_def.userData = (void *)entities_size;

	spawn_entity(body_def, OBJECT_GUN, texture, true);

	return entities + entities_size - 1;
}

static void spawn_crates(Texture texture) {
	int spawned_crate_count = 16;

	for (int i = 0; i < spawned_crate_count; i++) {
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_dynamicBody;
		body_def.position = (b2Vec2){ -100.0f, (i - spawned_crate_count / 2) * texture.height + 3.0f };
		body_def.userData = (void *)entities_size;

		spawn_entity(body_def, OBJECT_CRATE, texture, false);
	}
}

static void spawn_ground(Texture texture) {
	int ground_entity_count = 16;

	for (int i = 0; i < ground_entity_count; i++) {
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.position = (b2Vec2){ (i - ground_entity_count / 2) * texture.width, -100.0f };
		body_def.userData = (void *)entities_size;

		spawn_entity(body_def, OBJECT_GROUND, texture, false);
	}
}

static void remove_entity(size_t entity_index) {
	b2DestroyBody(entities[entity_index].body_id);
	entities[entity_index] = entities[--entities_size];
	if (entity_index < entities_size) {
		b2Body_SetUserData(entities[entity_index].body_id, (void *)entity_index);
	}
}

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		struct grug_modified reload = grug_reloads[reload_index];

		printf("Reloading %s\n", reload.path);

		// TODO: Write this
	}
}

int main(void) {
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	b2SetLengthUnitsPerMeter(PIXELS_PER_METER);

	b2WorldDef world_def = b2DefaultWorldDef();
	world_def.gravity.y = -9.8f * PIXELS_PER_METER;
	world_id = b2CreateWorld(&world_def);

	Texture background_texture = LoadTexture("mods/vanilla/background.png");
	crate_texture = LoadTexture("mods/vanilla/crate.png");
	concrete_texture = LoadTexture("mods/vanilla/concrete.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/kar98k/kar98k.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/long/long.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m16a2/m16a2.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m60/m60.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m79/m79.png");
	Texture gun_texture = LoadTexture("mods/vanilla/rpg7/rpg7.png");
	bullet_texture = LoadTexture("mods/vanilla/rpg7/rpg.png");

	struct entity *gun = spawn_gun((b2Vec2){ 100.0f, 0 }, gun_texture);

	spawn_ground(concrete_texture);

	spawn_crates(crate_texture);

	bool paused = false;

	while (!WindowShouldClose()) {
		if (grug_regenerate_modified_mods()) {
			if (grug_error.has_changed) {
				fprintf(stderr, "%s:%d: %s (detected in grug.c:%d)\n", grug_error.path, grug_error.line_number, grug_error.msg, grug_error.grug_c_line_number);
			}

			// Prevents the OS from showing a popup that we are unresponsive
			BeginDrawing();
			EndDrawing();

			sleep(1);

			continue;
		}

		reload_grug_entities();

		if (IsKeyPressed(KEY_P)) { // Pause
			paused = !paused;
		}
		if (IsKeyPressed(KEY_S)) { // Spawn crates
			spawn_crates(crate_texture);
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

			b2BodyEvents events = b2World_GetBodyEvents(world_id);
			for (int32_t i = 0; i < events.moveCount; i++) {
				b2BodyMoveEvent *event = events.moveEvents + i;
				if (event->transform.p.y < -100) { // TODO: Change the value to a little below the bottom of the screen
					remove_entity((size_t)event->userData);
				}
			}
		}

		Vector2 mouse_pos = GetMousePosition();
		b2Vec2 gun_world_pos = b2Body_GetPosition(gun->body_id);
		Vector2 gun_screen_pos = world_to_screen(gun_world_pos);
		Vector2 gun_to_mouse = Vector2Subtract(mouse_pos, gun_screen_pos);
		float gun_angle = atan2(-gun_to_mouse.y, gun_to_mouse.x);

		if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			b2Vec2 local_point = {
				.x = gun->texture.width / 2.0f + bullet_texture.width / 2.0f,
				.y = 0
			};
			b2Vec2 muzzle_pos = b2Body_GetWorldPoint(gun->body_id, local_point);
			b2Vec2 velocity = b2RotateVector(b2Body_GetRotation(gun->body_id), (b2Vec2){.x=BULLET_VELOCITY * PIXELS_PER_METER, .y=0});
			spawn_bullet(muzzle_pos, gun_angle, velocity, bullet_texture);
		}

		// Let the gun point to the mouse
		b2Body_SetTransform(gun->body_id, gun_world_pos, b2MakeRot(gun_angle));

		BeginDrawing();

		DrawTextureEx(background_texture, Vector2Zero(), 0, 2, WHITE);

		for (size_t i = 0; i < entities_size; i++) {
			draw_entity(entities[i]);
		}

		Color red = {.r=242, .g=42, .b=42, .a=255};
		DrawLine(gun_screen_pos.x, gun_screen_pos.y, mouse_pos.x, mouse_pos.y, red);

		draw_debug_info();

		EndDrawing();
	}

	// TODO: Are these necessary?
	UnloadTexture(background_texture);
	UnloadTexture(crate_texture);
	UnloadTexture(concrete_texture);
	UnloadTexture(gun_texture);
	UnloadTexture(bullet_texture);

	// TODO: Is this necessary?
	CloseWindow();
}
