#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "box2d/box2d.h"
#pragma GCC diagnostic pop

#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_BULLETS 420420
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define TEXTURE_SCALE 2.0f
#define PIXELS_PER_METER 20.0f // Taken from Cortex Command, where this program's sprites come from: https://github.com/cortex-command-community/Cortex-Command-Community-Project/blob/afddaa81b6d71010db299842d5594326d980b2cc/Source/System/Constants.h#L23
#define BULLET_VELOCITY 2.0f // In m/s

typedef struct Entity
{
	b2BodyId bodyId;
	Texture texture;
} Entity;

struct gun {
	char *name;
};

static Entity bullets[MAX_BULLETS];
static size_t bullets_size;

static struct gun gun_definition;

void define_gun(char *name) {
	gun_definition = (struct gun){
		.name = name,
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
	Vector2 result = { p.x * TEXTURE_SCALE * PIXELS_PER_METER + SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f - p.y * TEXTURE_SCALE * PIXELS_PER_METER };
	return result;
}

static void draw_entity(const Entity* entity)
{
	b2Vec2 local_point = {
		-entity->texture.width / 2.0f / PIXELS_PER_METER,
		entity->texture.height / 2.0f / PIXELS_PER_METER
	};

	// Rotates the local_point argument by the entity's angle
	b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, local_point);

	Vector2 ps = world_to_screen(p);

	float radians = b2Body_GetAngle(entity->bodyId);

	Rectangle rect = {ps.x, ps.y, entity->texture.width * TEXTURE_SCALE, entity->texture.height * TEXTURE_SCALE};
	Vector2 origin = {0, 0};
	Color color = {.r=42, .g=42, .b=242, .a=100};
	DrawRectanglePro(rect, origin, -radians * RAD2DEG, color);

	DrawTextureEx(entity->texture, ps, -radians * RAD2DEG, TEXTURE_SCALE, WHITE);
}

static void spawn_bullet(b2Vec2 pos, float angle, b2Vec2 velocity, b2WorldId worldId, Texture texture) {
	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_dynamicBody;
	bodyDef.position = pos;
	bodyDef.angle = angle;
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

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		// struct grug_modified reload = grug_reloads[reload_index];

		// TODO: Reload entities here
	}
}

int main(void)
{
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	// Texture gun_texture = LoadTexture("mods/vanilla/kar98k/kar98k.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/long/long.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m16a2/m16a2.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m60/m60.png");
	// Texture gun_texture = LoadTexture("mods/vanilla/m79/m79.png");
	Texture gun_texture = LoadTexture("mods/vanilla/rpg7/rpg7.png");
	Entity gun = spawn_gun((b2Vec2){ 100.0f / PIXELS_PER_METER, 0 }, worldId, gun_texture);

	Texture bullet_texture = LoadTexture("mods/vanilla/rpg7/rpg.png");

	while (!WindowShouldClose())
	{
		if (grug_regenerate_modified_mods()) {
			fprintf(stderr, "%s in %s:%d\n", grug_error.msg, grug_error.filename, grug_error.line_number);
			exit(EXIT_FAILURE);
		}

		reload_grug_entities();

		float deltaTime = GetFrameTime();
		b2World_Step(worldId, deltaTime, 4);

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
			spawn_bullet(p, gunAngle, velocity, worldId, bullet_texture);
		}

		// Let the gun point to the mouse
		b2Body_SetTransform(gun.bodyId, gunWorldPos, gunAngle);

		BeginDrawing();
		ClearBackground(SKYBLUE);

		draw_debug_info();

		draw_entity(&gun);

		for (size_t i = 0; i < bullets_size; i++) {
			draw_entity(bullets + i);
		}

		Color red = {.r=242, .g=42, .b=42, .a=255};
		DrawLine(gunScreenPos.x, gunScreenPos.y, mousePos.x, mousePos.y, red);

		EndDrawing();
	}

	// TODO: Are these necessary?
	UnloadTexture(gun_texture);
	UnloadTexture(bullet_texture);

	// TODO: Is this necessary?
	CloseWindow();
}
