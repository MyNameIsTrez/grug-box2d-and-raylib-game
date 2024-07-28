#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "box2d/box2d.h"
#pragma GCC diagnostic pop

#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_BULLETS 420
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define SCALE 100.0f
#define TEXTURE_SCALE 3.0f

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

static void spawn_bullet(b2WorldId worldId, Texture texture) {
	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_dynamicBody;
	bodyDef.position = (b2Vec2){ 2, 0 };

	Entity bullet;
	bullet.bodyId = b2CreateBody(worldId, &bodyDef);
	bullet.texture = texture;

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2Polygon polygon = b2MakeBox(42.0f, 42.0f); // TODO: Use the texture's width and height?
	b2CreatePolygonShape(bullet.bodyId, &shapeDef, &polygon);

	bullets[bullets_size++] = bullet;
}

static Entity spawn_gun(b2WorldId worldId, Texture texture) {
	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_staticBody;
	bodyDef.position = (b2Vec2){ 2, 0 };
	// bodyDef.fixedRotation = true; // TODO: Maybe use?

	Entity gun;
	gun.bodyId = b2CreateBody(worldId, &bodyDef);
	gun.texture = texture;

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2Polygon polygon = b2MakeBox(42.0f, 42.0f); // TODO: Use the texture's width and height?
	b2CreatePolygonShape(gun.bodyId, &shapeDef, &polygon);

	return gun;
}

static Vector2 convert_world_to_screen(b2Vec2 p)
{
	Vector2 result = { SCALE * p.x + 0.5f * SCREEN_WIDTH, 0.5f * SCREEN_HEIGHT - SCALE * p.y };
	return result;
}

static void draw_entity(const Entity* entity, b2Vec2 local_point)
{
	b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, local_point);
	Vector2 ps = convert_world_to_screen(p);

	float radians = b2Body_GetAngle(entity->bodyId);

	Rectangle rect = {ps.x, ps.y, entity->texture.width * TEXTURE_SCALE, entity->texture.height * TEXTURE_SCALE};
	Vector2 origin = {0, 0};
	Color color = {.r=42, .g=42, .b=242, .a=100};
	DrawRectanglePro(rect, origin, -radians * RAD2DEG, color);

	DrawTextureEx(entity->texture, ps, -radians * RAD2DEG, TEXTURE_SCALE, WHITE);
}

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		// struct grug_modified reload = grug_reloads[reload_index];

		// for (size_t i = 0; i < 2; i++) {
		// 	if (reload.old_dll == data.human_dlls[i]) {
		// 		data.human_dlls[i] = reload.new_dll;

		// 		free(data.human_globals[i]);
		// 		data.human_globals[i] = malloc(reload.globals_size);
		// 		reload.init_globals_fn(data.human_globals[i]);
		// 	}
		// }
		// for (size_t i = 0; i < 2; i++) {
		// 	if (reload.old_dll == data.tool_dlls[i]) {
		// 		data.tool_dlls[i] = reload.new_dll;

		// 		free(data.tool_globals[i]);
		// 		data.tool_globals[i] = malloc(reload.globals_size);
		// 		reload.init_globals_fn(data.tool_globals[i]);

		// 		data.tools[i].on_fns = reload.on_fns;
		// 	}
		// }
	}
}

int main(void)
{
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	// Texture texture = LoadTexture("mods/vanilla/kar98k/kar98k.png");
	// Texture texture = LoadTexture("mods/vanilla/m16a2/m16a2.png");
	// Texture texture = LoadTexture("mods/vanilla/m60/m60.png");
	// Texture texture = LoadTexture("mods/vanilla/m79/m79.png");
	Texture gun_texture = LoadTexture("mods/vanilla/rpg7/rpg7.png");
	Entity gun = spawn_gun(worldId, gun_texture);

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

		if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			spawn_bullet(worldId, bullet_texture);
		}

		// Let the gun follow the mouse
		Vector2 mousePos = GetMousePosition();
		b2Vec2 gunWorldPos = b2Body_GetPosition(gun.bodyId);
		Vector2 gunScreenPos = convert_world_to_screen(gunWorldPos);
		Vector2 gunToMouse = Vector2Subtract(mousePos, gunScreenPos);
		Color red = {.r=242, .g=42, .b=42, .a=255};
		DrawLine(gunScreenPos.x, gunScreenPos.y, mousePos.x, mousePos.y, red);
		float angle = atan2(-gunToMouse.y, gunToMouse.x);
		b2Body_SetTransform(gun.bodyId, gunWorldPos, angle);

		BeginDrawing();
		ClearBackground(SKYBLUE);

		draw_debug_info();

		draw_entity(&gun, (b2Vec2){
			-0.5f,
			(float)gun_texture.height / gun_texture.width / 2
		});

		for (size_t i = 0; i < bullets_size; i++) {
			draw_entity(bullets + i, (b2Vec2){
				-0.5f,
				(float)bullet_texture.height / bullet_texture.width / 2
			});
		}

		EndDrawing();
	}

	// TODO: Are these necessary?
	UnloadTexture(gun_texture);
	UnloadTexture(bullet_texture);

	// TODO: Is this necessary?
	CloseWindow();
}
