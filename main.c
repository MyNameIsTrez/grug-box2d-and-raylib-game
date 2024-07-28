#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "box2d/box2d.h"
#pragma GCC diagnostic pop

#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

static int screenWidth = 1280;
static int screenHeight = 720;
static float scale = 100.0f;

typedef struct Entity
{
	b2BodyId bodyId;
	Texture texture;
} Entity;

struct gun {
	char *name;
};

static struct gun gun_definition;

void define_gun(char *name) {
	gun_definition = (struct gun){
		.name = name,
	};
}

static Vector2 ConvertWorldToScreen(b2Vec2 p)
{
	Vector2 result = { scale * p.x + 0.5f * screenWidth, 0.5f * screenHeight - scale * p.y };
	return result;
}

static void DrawEntity(const Entity* entity)
{
	float textureScale = scale / entity->texture.width;

	b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { -0.5f, (float)entity->texture.height / entity->texture.width / 2 });
	Vector2 ps = ConvertWorldToScreen(p);

	Rectangle rect = {ps.x, ps.y, entity->texture.width * textureScale, entity->texture.height * textureScale};
	Vector2 origin = {0, 0};
	float radians = b2Body_GetAngle(entity->bodyId);
	Color color = {.r=42, .g=42, .b=242, .a=100};
	DrawRectanglePro(rect, origin, -radians * RAD2DEG, color);

	DrawTextureEx(entity->texture, ps, -radians * RAD2DEG, textureScale, WHITE);
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
	InitWindow(screenWidth, screenHeight, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	// Texture texture = LoadTexture("mods/vanilla/kar98k/kar98k.png");
	Texture texture = LoadTexture("mods/vanilla/m16a2/m16a2.png");
	// Texture texture = LoadTexture("mods/vanilla/m60/m60.png");
	// Texture texture = LoadTexture("mods/vanilla/m79/m79.png");
	// Texture texture = LoadTexture("mods/vanilla/rpg7/rpg7.png");

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_staticBody;
	bodyDef.position = (b2Vec2){ 2, 0 };
	// bodyDef.fixedRotation = true; // TODO: Maybe use?
	Entity entity;
	entity.bodyId = b2CreateBody(worldId, &bodyDef);
	entity.texture = texture;
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2Polygon polygon = b2MakeBox(42.0f, 42.0f); // TODO: Use the texture's width and height?
	b2CreatePolygonShape(entity.bodyId, &shapeDef, &polygon);

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
			printf("foo\n");
		}

		// Let the gun follow the mouse
		Vector2 mousePos = GetMousePosition();
		b2Vec2 gunWorldPos = b2Body_GetPosition(entity.bodyId);
		Vector2 gunScreenPos = ConvertWorldToScreen(gunWorldPos);
		Vector2 gunToMouse = Vector2Subtract(mousePos, gunScreenPos);
		Color red = {.r=242, .g=42, .b=42, .a=255};
		DrawLine(gunScreenPos.x, gunScreenPos.y, mousePos.x, mousePos.y, red);
		float angle = atan2(-gunToMouse.y, gunToMouse.x);
		b2Body_SetTransform(entity.bodyId, gunWorldPos, angle);

		BeginDrawing();
		ClearBackground(SKYBLUE);

		DrawFPS(0, 0);

		DrawEntity(&entity);

		EndDrawing();
	}

	UnloadTexture(texture);

	CloseWindow();

	return 0;
}
