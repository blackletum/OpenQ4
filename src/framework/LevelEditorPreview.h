// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_PREVIEW_H
#define OPENQ4_LEVEL_EDITOR_PREVIEW_H

#include "../tools/leveleditor/MapDocument.h"
#include <vector>
#include <map>

class idDeviceContext;
class idRectangle;

class idLevelEditorPreview {
public:
	~idLevelEditorPreview();
	void Rebuild( const oq4editor::MapDocument &document );
	void Draw( idDeviceContext *dc, const idRectangle &rect, int time );
	void Focus( oq4editor::EntityId id );
	void Orbit( float yawDelta, float pitchDelta, float zoom );
	void Pan( float x, float y );
	oq4editor::EntityId Pick( float x, float y, float aspect ) const;
	void Select( oq4editor::EntityId id );
	int Skipped() const { return skipped; }
	int Triangles() const { return triangles; }
	int Entities() const;
private:
	struct Record {
		int handle = -1, version = 0, skipped = 0, triangles = 0;
		std::size_t vertices = 0;
		renderEntity_t entity = {};
		idVec3 color = idVec3( 1, 1, 1 );
		std::vector<std::string> primitives;
	};
	void Clear();
	void Release( Record &record );
	void Tint( Record &record, bool selected );
	idRenderWorld *world = nullptr;
	std::map<oq4editor::EntityId, Record> records;
	oq4editor::EntityId selected = 0;
	idBounds bounds;
	idVec3 target = vec3_origin;
	float yaw = 225.0f, pitch = 25.0f, distance = 512.0f;
	int lightHandle = -1, skipped = 0, triangles = 0;
};

#endif
