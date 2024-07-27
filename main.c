#include "box2d/box2d.h"
#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct Conversion
{
	float scale;
	float size;
	float screenWidth;
	float screenHeight;
} Conversion;

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

static Vector2 ConvertWorldToScreen(b2Vec2 p, Conversion cv)
{
	Vector2 result = { cv.scale * p.x + 0.5f * cv.screenWidth, 0.5f * cv.screenHeight - cv.scale * p.y };
	return result;
}

static void DrawEntity(const Entity* entity, Conversion cv)
{
	float textureScale = cv.size * cv.scale / (float)entity->texture.width;

	// b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { -0.5f * cv.size, 0.5f * cv.size });
	// b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { 0, 0 });
	// b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { -0.5f * entity->texture.width, 0.5f * entity->texture.height });
	b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { -0.5f, 0.3f });
	// b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { -1.0f, 1.0f });
	float radians = b2Body_GetAngle(entity->bodyId);

	// printf("p.x: %f, p.y: %f\n", p.x, p.y);
	Vector2 ps = ConvertWorldToScreen(p, cv);

	// Rectangle rect = {0, 0, entity->texture.width * textureScale, entity->texture.height * textureScale};
	// Vector2 origin = {ps.x, ps.y};
	Rectangle rect = {ps.x, ps.y, entity->texture.width * textureScale, entity->texture.height * textureScale};
	Vector2 origin = {0, 0};
	Color color = {.r=42, .g=42, .b=242, .a=100};
	// Have to negate rotation to account for y-flip
	DrawRectanglePro(rect, origin, -radians * RAD2DEG, color);

	DrawTextureEx(entity->texture, ps, -radians * RAD2DEG, textureScale, WHITE);

	// Use these circles to ensure the coordinate transformation is correct
	DrawCircleV(ps, 5.0f, BLACK);

	p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2){0.0f, 0.0f});
	ps = ConvertWorldToScreen(p, cv);
	DrawCircleV(ps, 5.0f, BLUE);

	p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2){0.5f * cv.size, -0.5f * cv.size});
	ps = ConvertWorldToScreen(p, cv);
	DrawCircleV(ps, 5.0f, RED);
}

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		struct grug_modified reload = grug_reloads[reload_index];

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
	int width = 1280, height = 720;
	InitWindow(width, height, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	float size = 1.0f;
	float scale = 400.0f;

	Conversion cv = { scale, size, (float)width, (float)height };

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	Texture texture = LoadTexture("mods/vanilla/thumper/thumper.png");

	b2Polygon squarePolygon = b2MakeSquare(0.5f * size);

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_staticBody;
	bodyDef.position = (b2Vec2){ 0, 0 };
	// bodyDef.fixedRotation = true; // TODO: Maybe use?
	Entity entity;
	entity.bodyId = b2CreateBody(worldId, &bodyDef);
	entity.texture = texture;
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2CreatePolygonShape(entity.bodyId, &shapeDef, &squarePolygon);

	float angle = 0;

	while (!WindowShouldClose())
	{
		if (grug_regenerate_modified_mods()) {
			fprintf(stderr, "%s in %s:%d\n", grug_error.msg, grug_error.filename, grug_error.line_number);
			exit(EXIT_FAILURE);
		}

		reload_grug_entities();

		float deltaTime = GetFrameTime();
		b2World_Step(worldId, deltaTime, 4);

		// Let the gun follow the mouse
		angle += deltaTime;
		b2Body_SetTransform(entity.bodyId, b2Body_GetPosition(entity.bodyId), angle);

		BeginDrawing();
		ClearBackground(SKYBLUE);

		DrawFPS(0, 0);

		DrawEntity(&entity, cv);

		EndDrawing();
	}

	UnloadTexture(texture);

	CloseWindow();

	return 0;
}
