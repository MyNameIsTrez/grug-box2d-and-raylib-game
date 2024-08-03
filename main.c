#include "box2d/box2d.h"
#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_BULLETS 420420
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define TEXTURE_SCALE 2.0f
#define PIXELS_PER_METER 20.0f // Taken from Cortex Command, where this program's sprites come from: https://github.com/cortex-command-community/Cortex-Command-Community-Project/blob/afddaa81b6d71010db299842d5594326d980b2cc/Source/System/Constants.h#L23
#define BULLET_VELOCITY 2.0f // In m/s
#define GROUND_ENTITY_COUNT 16
#define CRATE_ENTITY_COUNT 16

typedef struct Entity
{
	b2BodyId bodyId;
	Texture texture;
} Entity;

struct gun {
	char *name;
	int32_t rate_of_fire;
	bool full_auto;
};

static Entity bullets[MAX_BULLETS];
static size_t bullets_size;

static Entity ground_entities[GROUND_ENTITY_COUNT];
static Entity crate_entities[CRATE_ENTITY_COUNT];

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

// Not averaged, unlike DrawFPS()
static void draw_mspf(int x, int y) {
	Color color = LIME;
	float mspf = GetFrameTime() * 1000;
	DrawText(TextFormat("%.2f MSPF", mspf), x, y, 20, color);
}

static void draw_debug_info(void) {
	DrawFPS(0, 0);
	draw_mspf(0, 20);
}

static Vector2 world_to_screen(b2Vec2 p)
{
	return (Vector2){
		  p.x * TEXTURE_SCALE * PIXELS_PER_METER + SCREEN_WIDTH  / 2.0f,
		- p.y * TEXTURE_SCALE * PIXELS_PER_METER + SCREEN_HEIGHT / 2.0f
	};
}

static void draw_entity(const Entity entity, bool flippable)
{
	Texture texture = entity.texture;

	b2Vec2 local_point = {
		-texture.width / 2.0f / PIXELS_PER_METER,
		texture.height / 2.0f / PIXELS_PER_METER
	};

	// Rotates the local_point argument by the entity's angle
	b2Vec2 p = b2Body_GetWorldPoint(entity.bodyId, local_point);

	Vector2 ps = world_to_screen(p);

	b2Rot rot = b2Body_GetRotation(entity.bodyId);
	float angle = b2Rot_GetAngle(rot);

	bool facing_left = (angle > PI / 2) || (angle < -PI / 2);
    Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height * (flippable && facing_left ? -1 : 1) };
    Rectangle dest = { ps.x, ps.y, (float)texture.width*TEXTURE_SCALE, (float)texture.height*TEXTURE_SCALE };
    Vector2 origin = { 0.0f, 0.0f };
	float rotation = -angle * RAD2DEG;
	Color tint = WHITE;
	DrawTexturePro(texture, source, dest, origin, rotation, tint);

	// Draws the bounding box
	// Rectangle rect = {ps.x, ps.y, texture.width * TEXTURE_SCALE, texture.height * TEXTURE_SCALE};
	// Color color = {.r=42, .g=42, .b=242, .a=100};
	// DrawRectanglePro(rect, origin, -angle * RAD2DEG, color);
}

static void spawn_bullet_in_world(b2Vec2 pos, float angle, b2Vec2 velocity, b2WorldId worldId, Texture texture) {
	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_dynamicBody;
	bodyDef.position = pos;
	bodyDef.rotation = b2MakeRot(angle);
	bodyDef.linearVelocity = velocity;

	Entity bullet;
	bullet.bodyId = b2CreateBody(worldId, &bodyDef);
	bullet.texture = texture;

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2Polygon polygon = b2MakeBox(texture.width / 2.0f / PIXELS_PER_METER, texture.height / 2.0f / PIXELS_PER_METER);
	b2CreatePolygonShape(bullet.bodyId, &shapeDef, &polygon);

	bullets[bullets_size++] = bullet;
}

static Entity spawn_gun(b2Vec2 pos, b2WorldId worldId, Texture texture) {
	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_staticBody;
	bodyDef.position = pos;
	// bodyDef.fixedRotation = true; // TODO: Maybe use?

	Entity gun;
	gun.bodyId = b2CreateBody(worldId, &bodyDef);
	gun.texture = texture;

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2Polygon polygon = b2MakeBox(texture.width / 2.0f / PIXELS_PER_METER, texture.height / 2.0f / PIXELS_PER_METER);
	b2CreatePolygonShape(gun.bodyId, &shapeDef, &polygon);

	return gun;
}

static void spawn_crates(b2WorldId worldId) {
	Texture texture = LoadTexture("mods/vanilla/crate.png");

	float width_meters  = texture.width  / PIXELS_PER_METER;
	float height_meters = texture.height / PIXELS_PER_METER;

	b2Polygon polygon = b2MakeBox(width_meters / 2.0f, height_meters / 2.0f);

	for (int i = 0; i < CRATE_ENTITY_COUNT; i++) {
		Entity* entity = crate_entities + i;
		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.type = b2_dynamicBody;
		bodyDef.position = (b2Vec2){ -100.0f / PIXELS_PER_METER, (i - CRATE_ENTITY_COUNT / 2) * height_meters + 3.0f };

		entity->bodyId = b2CreateBody(worldId, &bodyDef);
		entity->texture = texture;
		b2ShapeDef shapeDef = b2DefaultShapeDef();
		b2CreatePolygonShape(entity->bodyId, &shapeDef, &polygon);
	}
}

static void spawn_ground(b2WorldId worldId) {
	Texture texture = LoadTexture("mods/vanilla/concrete.png");

	float width_meters  = texture.width  / PIXELS_PER_METER;
	float height_meters = texture.height / PIXELS_PER_METER;

	b2Polygon polygon = b2MakeBox(width_meters / 2.0f, height_meters / 2.0f);

	for (int i = 0; i < GROUND_ENTITY_COUNT; i++) {
		Entity* entity = ground_entities + i;
		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.position = (b2Vec2){ (i - GROUND_ENTITY_COUNT / 2) * width_meters, -100.0f / PIXELS_PER_METER };

		entity->bodyId = b2CreateBody(worldId, &bodyDef);
		entity->texture = texture;
		b2ShapeDef shapeDef = b2DefaultShapeDef();
		b2CreatePolygonShape(entity->bodyId, &shapeDef, &polygon);
	}
}

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		struct grug_modified reload = grug_reloads[reload_index];

		printf("Reloading %s\n", reload.path);

		// TODO: Write this
	}
}

int main(void)
{
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	Texture background_texture = LoadTexture("mods/vanilla/background.png");

	// Texture gun_texture = LoadTexture("mods/vanilla/kar98k/kar98k.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/long/long.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m16a2/m16a2.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m60/m60.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m79/m79.png");
	Texture gun_texture = LoadTexture("mods/vanilla/rpg7/rpg7.png");
	Entity gun = spawn_gun((b2Vec2){ 100.0f / PIXELS_PER_METER, 0 }, worldId, gun_texture);

	Texture bullet_texture = LoadTexture("mods/vanilla/rpg7/rpg.png");

	spawn_ground(worldId);

	spawn_crates(worldId);

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

		if (IsKeyPressed(KEY_P))
		{
			paused = !paused;
		}

		if (!paused)
		{
			float deltaTime = GetFrameTime();
			b2World_Step(worldId, deltaTime, 4);
		}

		Vector2 mousePos = GetMousePosition();
		b2Vec2 gunWorldPos = b2Body_GetPosition(gun.bodyId);
		Vector2 gunScreenPos = world_to_screen(gunWorldPos);
		Vector2 gunToMouse = Vector2Subtract(mousePos, gunScreenPos);
		float gunAngle = atan2(-gunToMouse.y, gunToMouse.x);

		if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			b2Vec2 local_point = {
				.x = (gun.texture.width / 2.0f + bullet_texture.width / 2.0f) / PIXELS_PER_METER,
				.y = 0
			};
			b2Vec2 p = b2Body_GetWorldPoint(gun.bodyId, local_point);
			b2Vec2 velocity = b2RotateVector(b2Body_GetRotation(gun.bodyId), (b2Vec2){.x=BULLET_VELOCITY * PIXELS_PER_METER, .y=0});
			spawn_bullet_in_world(p, gunAngle, velocity, worldId, bullet_texture);
		}

		// Let the gun point to the mouse
		b2Body_SetTransform(gun.bodyId, gunWorldPos, b2MakeRot(gunAngle));

		BeginDrawing();

		DrawTextureEx(background_texture, Vector2Zero(), 0, 2, WHITE);

		for (int i = 0; i < GROUND_ENTITY_COUNT; i++) {
			draw_entity(ground_entities[i], false);
		}

		for (int i = 0; i < CRATE_ENTITY_COUNT; i++) {
			draw_entity(crate_entities[i], false);
		}

		draw_entity(gun, true);

		for (size_t i = 0; i < bullets_size; i++) {
			draw_entity(bullets[i], false);
		}

		Color red = {.r=242, .g=42, .b=42, .a=255};
		DrawLine(gunScreenPos.x, gunScreenPos.y, mousePos.x, mousePos.y, red);

		draw_debug_info();

		EndDrawing();
	}

	// TODO: Are these necessary?
	UnloadTexture(gun_texture);
	UnloadTexture(bullet_texture);

	// TODO: Is this necessary?
	CloseWindow();
}
