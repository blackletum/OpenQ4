// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditorPreview.h"
#include "../ui/DeviceContext.h"
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace {
constexpr int LexerFlags = LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS;
constexpr std::size_t MaxPreviewVertices = 2000000;
struct Mesh { std::vector<idDrawVert> verts; std::vector<int> indexes; };
struct SurfaceKey {
	const idMaterial *material;
	std::size_t part;
	bool operator<( const SurfaceKey &other ) const {
		return material == other.material ? part < other.part : std::less<const idMaterial *>()( material, other.material );
	}
};
using Surfaces = std::map<SurfaceKey, Mesh>;
Mesh &Batch( Surfaces &surfaces, const idMaterial *material ) {
	// Flare, autosprite and other discrete shaders must retain their individual
	// faces. Merging them violates their quad/primitive deformation contract.
	return surfaces[{ material, material->IsDiscrete() ? surfaces.size() + 1 : 0 }];
}

bool Finite( const idVec3 &v ) {
	return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ) &&
		idMath::Fabs( v.x ) < 1000000.0f && idMath::Fabs( v.y ) < 1000000.0f && idMath::Fabs( v.z ) < 1000000.0f;
}

idVec3 Origin( const oq4editor::Entity &entity ) {
	idVec3 origin = vec3_origin;
	const std::string text = entity.Value( "origin" );
	if ( sscanf( text.c_str(), "%f %f %f", &origin.x, &origin.y, &origin.z ) != 3 || !Finite( origin ) ) { return vec3_origin; }
	return origin;
}

idVec3 MarkerColor( const oq4editor::Entity &entity ) {
	const auto classname = entity.Value( "classname" );
	if ( oq4editor::EqualNoCase( classname, "light" ) ) { return idVec3( 0.8f, 0.7f, 0.3f ); }
	if ( classname.find( "info_player" ) == 0 ) { return idVec3( 0.3f, 0.8f, 0.45f ); }
	return idVec3( 0.35f, 0.6f, 0.85f );
}

bool PatchHeaderSafe( const std::string &source, bool explicitSubdivisions ) {
	Lexer lexer( source.c_str(), static_cast<int>( source.size() ), "editor patch header", LexerFlags );
	idToken token;
	float info[7] = {};
	if ( !lexer.ExpectTokenString( "{" ) || !lexer.ReadToken( &token ) || !lexer.ExpectTokenString( "{" ) ||
		!lexer.ReadToken( &token ) || !lexer.Parse1DMatrix( explicitSubdivisions ? 7 : 5, info ) ) { return false; }
	// Validate before the legacy parser allocates width * height control points.
	for ( int i = 0; i < 2; ++i ) {
		if ( !std::isfinite( info[i] ) || info[i] < 3 || info[i] > 65 ||
			info[i] != std::floor( info[i] ) || static_cast<int>( info[i] ) % 2 == 0 ) { return false; }
	}
	return !explicitSubdivisions || ( std::isfinite( info[2] ) && std::isfinite( info[3] ) &&
		info[2] >= 1 && info[2] <= 32 && info[3] >= 1 && info[3] <= 32 &&
		info[2] == std::floor( info[2] ) && info[3] == std::floor( info[3] ) );
}

bool AppendBrush( const idMapBrush &brush, Surfaces &surfaces, std::size_t &vertexCount ) {
	if ( brush.GetNumSides() < 4 || brush.GetNumSides() > 128 ) { return false; }
	for ( int side = 0; side < brush.GetNumSides(); ++side ) {
		const auto &plane = brush.GetSide( side )->GetPlane();
		if ( !Finite( plane.Normal() ) || !std::isfinite( plane[3] ) || idMath::Fabs( plane[3] ) > 1000000.0f ||
			plane.Normal().LengthSqr() < 0.5f || plane.Normal().LengthSqr() > 1.5f ) { return false; }
	}
	for ( int side = 0; side < brush.GetNumSides(); ++side ) {
		const auto *face = brush.GetSide( side );
		const idMaterial *material = declManager->FindMaterial( face->GetMaterial() );
		if ( !material->IsDrawn() ) { continue; }
		idWinding winding( face->GetPlane() );
		for ( int clip = 0; clip < brush.GetNumSides() && winding.GetNumPoints(); ++clip ) {
			if ( clip != side ) { winding.ClipInPlace( -brush.GetSide( clip )->GetPlane(), 0.01f ); }
		}
		const int count = winding.GetNumPoints();
		if ( count < 3 ) { continue; }
		if ( material->Deform() == DFRM_FLARE && count != 4 ) { return false; }
		if ( vertexCount + count > MaxPreviewVertices ) { return false; }
		idVec4 tex[2]; face->GetTextureVectors( tex );
		for ( const auto &vector : tex ) { for ( int i = 0; i < 4; ++i ) { if ( !std::isfinite( vector[i] ) ) { return false; } } }
		// Validate the whole face before appending any vertices. A rejected
		// face must not leave uncounted allocations or nonfinite GPU attributes.
		for ( int i = 0; i < count; ++i ) {
			const idVec3 point = winding[i].ToVec3();
			if ( !Finite( point ) || !std::isfinite( point * tex[0].ToVec3() + tex[0][3] ) ||
				!std::isfinite( point * tex[1].ToVec3() + tex[1][3] ) ) { return false; }
		}
		Mesh &mesh = Batch( surfaces, material );
		const int start = static_cast<int>( mesh.verts.size() );
		for ( int i = 0; i < count; ++i ) {
			idDrawVert vertex = {}; vertex.Clear();
			vertex.xyz = winding[i].ToVec3();
			vertex.SetNormal( face->GetPlane().Normal() );
			vertex.st.Set( vertex.xyz * tex[0].ToVec3() + tex[0][3], vertex.xyz * tex[1].ToVec3() + tex[1][3] );
			vertex.SetColor( 0xffffffff ); mesh.verts.push_back( vertex );
		}
		for ( int i = 1; i + 1 < count; ++i ) {
			mesh.indexes.insert( mesh.indexes.end(), { start, start + i, start + i + 1 } );
		}
		vertexCount += count;
	}
	return true;
}

bool AppendPrimitive( const std::string &source, const idVec3 &origin, int version, Surfaces &surfaces, std::size_t &vertexCount ) {
	Lexer lexer( source.c_str(), static_cast<int>( source.size() ), "editor primitive", LexerFlags );
	idToken kind;
	if ( !lexer.ExpectTokenString( "{" ) || !lexer.ReadToken( &kind ) ) { return false; }
	if ( kind == "patchDef2" || kind == "patchDef3" ) {
		if ( !PatchHeaderSafe( source, kind == "patchDef3" ) ) { return false; }
		std::unique_ptr<idMapPatch> patch( idMapPatch::Parse( lexer, origin, kind == "patchDef3", version ) );
		if ( !patch ) { return false; }
		for ( int i = 0; i < patch->GetNumVertices(); ++i ) {
			const idVec3 xyz = (*patch)[i].xyz;
			const idVec2 st = (*patch)[i].st;
			if ( !Finite( xyz ) || !std::isfinite( st.x ) || !std::isfinite( st.y ) || idMath::Fabs( st.x ) > 1000000 || idMath::Fabs( st.y ) > 1000000 ) { return false; }
			(*patch)[i] = idDrawVert();
			(*patch)[i].Clear(); (*patch)[i].xyz = xyz; (*patch)[i].st = st; (*patch)[i].SetColor( 0xffffffff );
		}
		// Preview tessellation is bounded; dmap retains responsibility for the
		// final adaptive render/collision mesh. Never change authoring controls.
		patch->SubdivideExplicit( 4, 4, true, true );
		if ( vertexCount + patch->GetNumVertices() > MaxPreviewVertices ) { return false; }
		const idMaterial *material = declManager->FindMaterial( patch->GetMaterial() );
		if ( !material->IsDrawn() ) { return true; }
		if ( material->Deform() == DFRM_FLARE && ( patch->GetNumVertices() != 4 || patch->GetNumIndexes() != 6 ) ) { return false; }
		Mesh &mesh = Batch( surfaces, material );
		const int start = static_cast<int>( mesh.verts.size() );
		for ( int i = 0; i < patch->GetNumVertices(); ++i ) {
			// The legacy subdivider writes positions, UVs and normals only.
			idDrawVert vertex = {}; vertex.Clear();
			vertex.xyz = (*patch)[i].xyz; vertex.st = (*patch)[i].st; vertex.SetNormal( (*patch)[i].GetNormal() );
			vertex.SetColor( 0xffffffff ); mesh.verts.push_back( vertex );
		}
		for ( int i = 0; i < patch->GetNumIndexes(); ++i ) { mesh.indexes.push_back( start + patch->GetIndexes()[i] ); }
		vertexCount += patch->GetNumVertices();
		return true;
	}
	std::unique_ptr<idMapBrush> brush;
	if ( kind == "brushDef" || kind == "brushDef2" || kind == "brushDef3" ) {
		brush.reset( idMapBrush::Parse( lexer, origin, kind != "brushDef", version ) );
	} else if ( kind == "(" ) {
		lexer.UnreadToken( &kind ); brush.reset( idMapBrush::ParseQ3( lexer, origin ) );
	} else { return false; }
	return brush && AppendBrush( *brush, surfaces, vertexCount );
}

bool AppendMarker( Surfaces &surfaces, std::size_t &vertexCount ) {
	if ( vertexCount + 24 > MaxPreviewVertices ) { return false; }
	Mesh &mesh = Batch( surfaces, declManager->FindMaterial( "_white" ) );
	static const int corners[6][4] = { {0,2,3,1}, {4,5,7,6}, {0,1,5,4}, {2,6,7,3}, {0,4,6,2}, {1,3,7,5} };
	for ( const auto &face : corners ) {
		const int start = static_cast<int>( mesh.verts.size() );
		for ( int corner : face ) {
			idDrawVert vertex = {}; vertex.Clear();
			vertex.xyz.Set( corner & 1 ? 4.0f : -4.0f, corner & 2 ? 4.0f : -4.0f, corner & 4 ? 4.0f : -4.0f );
			vertex.SetColor( 0xffffffff ); mesh.verts.push_back( vertex );
		}
		mesh.indexes.insert( mesh.indexes.end(), { start, start + 1, start + 2, start, start + 2, start + 3 } );
	}
	vertexCount += 24;
	return true;
}
}

idLevelEditorPreview::~idLevelEditorPreview() { Clear(); }
void idLevelEditorPreview::Clear() {
	for ( auto &[id, record] : records ) { Release( record ); }
	if ( world ) { renderSystem->FreeRenderWorld( world ); world = nullptr; }
	records.clear(); bounds.Clear(); lightHandle = -1; skipped = triangles = 0; selected = 0;
}
void idLevelEditorPreview::Release( Record &record ) {
	if ( record.handle >= 0 ) { world->FreeEntityDef( record.handle ); }
	if ( record.entity.hModel ) { renderModelManager->FreeModel( record.entity.hModel ); }
	record = Record();
}

void idLevelEditorPreview::Tint( Record &record, bool isSelected ) {
	const idVec3 color = isSelected && record.primitives.empty() ? idVec3( 1.0f, 0.55f, 0.05f ) : record.color;
	for ( int i = 0; i < 3; ++i ) { record.entity.shaderParms[i] = color[i]; }
	record.entity.shaderParms[3] = 1.0f;
	record.entity.outlineColor = isSelected ? idVec4( 1.0f, 0.55f, 0.05f, 1.0f ) : vec4_zero;
	record.entity.outlineWidth = 2.0f;
}

void idLevelEditorPreview::Rebuild( const oq4editor::MapDocument &document ) {
	const int startTime = Sys_Milliseconds();
	if ( !world ) { world = renderSystem->AllocRenderWorld(); world->InitFromMap( nullptr ); }
	// Compare exact primitive bytes, not a lossy hash or all entity properties.
	// Changing a key/value or selecting an entity must not rebuild a whole map.
	std::map<oq4editor::EntityId, const oq4editor::Entity *> sources;
	for ( const auto &entity : document.Entities() ) { sources[entity.id] = &entity; }
	for ( auto it = records.begin(); it != records.end(); ) {
		if ( sources.find( it->first ) == sources.end() ) { Release( it->second ); it = records.erase( it ); }
		else { ++it; }
	}
	bounds.Clear(); skipped = triangles = 0;
	std::size_t vertexCount = 0;
	for ( const auto &[id, record] : records ) { vertexCount += record.vertices; }
	int rebuilt = 0;
	for ( const auto &sourceEntity : document.Entities() ) {
		const idVec3 origin = sourceEntity.id == document.Entities().front().id ? vec3_origin : Origin( sourceEntity );
		const idVec3 color = sourceEntity.primitiveCount ? idVec3( 1, 1, 1 ) : MarkerColor( sourceEntity );
		Record &record = records[sourceEntity.id];
		bool changed = record.version != document.Version() || record.primitives.size() != sourceEntity.primitiveRanges.size();
		if ( !changed ) {
			for ( std::size_t i = 0; i < record.primitives.size(); ++i ) {
				const auto &range = sourceEntity.primitiveRanges[i];
				if ( sourceEntity.source.compare( range.first, range.second - range.first, record.primitives[i] ) != 0 ) { changed = true; break; }
			}
		}
		if ( !record.primitives.empty() && record.entity.origin != origin ) { changed = true; }
		if ( !changed ) {
			triangles += record.triangles; skipped += record.skipped;
			if ( record.handle >= 0 ) {
				if ( record.entity.origin != origin || record.color != color ) {
					record.entity.origin = origin; record.color = color; Tint( record, sourceEntity.id == selected );
					world->UpdateEntityDef( record.handle, &record.entity );
				}
				bounds.AddBounds( record.entity.bounds + origin );
			}
			continue;
		}
		vertexCount -= record.vertices; Release( record ); ++rebuilt;
		record.version = document.Version(); record.entity.origin = origin;
		const std::size_t beforeVertices = vertexCount;
		Surfaces surfaces;
		for ( const auto &range : sourceEntity.primitiveRanges ) {
			record.primitives.push_back( sourceEntity.source.substr( range.first, range.second - range.first ) );
			if ( !AppendPrimitive( record.primitives.back(), origin, document.Version(), surfaces, vertexCount ) ) { ++record.skipped; }
		}
		if ( !sourceEntity.primitiveCount && sourceEntity.id != document.Entities().front().id && !AppendMarker( surfaces, vertexCount ) ) { ++record.skipped; }
		record.vertices = vertexCount - beforeVertices; skipped += record.skipped;
		if ( surfaces.empty() ) { continue; }
		idRenderModel *model = renderModelManager->AllocModel();
		// Owned exclusively by this preview: never register or replace _areaN
		// models in the global model catalog used by the running game.
		model->InitEmpty( va( "_editor_entity_%llu", static_cast<unsigned long long>( sourceEntity.id ) ) );
		int surfaceId = 0;
		for ( auto &[key, mesh] : surfaces ) {
			if ( mesh.indexes.empty() ) { continue; }
			modelSurface_t surface = {};
			surface.id = surfaceId++; surface.shader = key.material;
			surface.geometry = model->AllocSurfaceTriangles( static_cast<int>( mesh.verts.size() ), static_cast<int>( mesh.indexes.size() ) );
			surface.geometry->numVerts = static_cast<int>( mesh.verts.size() );
			surface.geometry->numIndexes = static_cast<int>( mesh.indexes.size() );
			for ( std::size_t i = 0; i < mesh.verts.size(); ++i ) { surface.geometry->verts[i] = mesh.verts[i]; }
			for ( std::size_t i = 0; i < mesh.indexes.size(); ++i ) { surface.geometry->indexes[i] = mesh.indexes[i]; }
			record.triangles += surface.geometry->numIndexes / 3;
			model->AddSurface( surface );
		}
		if ( !surfaceId ) { renderModelManager->FreeModel( model ); continue; }
		model->FinishSurfaces(); triangles += record.triangles;
		record.entity.hModel = model; record.entity.axis.Identity(); record.entity.origin = origin;
		record.entity.bounds = model->Bounds(); record.entity.noShadow = true;
		record.color = color; Tint( record, sourceEntity.id == selected );
		record.handle = world->AddEntityDef( &record.entity );
		bounds.AddBounds( record.entity.bounds + origin );
	}
	if ( bounds.IsCleared() ) { bounds = idBounds( idVec3( -64, -64, -64 ), idVec3( 64, 64, 64 ) ); }
	common->Printf( "editor preview: entities=%d triangles=%d skipped=%d\n", Entities(), triangles, skipped );
	common->Printf( "editor preview update: rebuilt=%d elapsed=%dms\n", rebuilt, Sys_Milliseconds() - startTime );
}

int idLevelEditorPreview::Entities() const {
	int count = 0;
	for ( const auto &[id, record] : records ) { if ( record.handle >= 0 ) { ++count; } }
	return count;
}

void idLevelEditorPreview::Focus( oq4editor::EntityId id ) {
	idBounds focus = bounds;
	const auto found = records.find( id );
	if ( found != records.end() && found->second.handle >= 0 ) { focus = found->second.entity.bounds + found->second.entity.origin; }
	target = focus.GetCenter();
	distance = idMath::ClampFloat( 96.0f, 60000.0f, ( focus[1] - focus[0] ).Length() * 0.8f + 64.0f );
}
void idLevelEditorPreview::Orbit( float yawDelta, float pitchDelta, float zoom ) {
	yaw = idMath::AngleNormalize360( yaw + yawDelta ); pitch = idMath::ClampFloat( -85.0f, 85.0f, pitch + pitchDelta );
	distance = idMath::ClampFloat( 32.0f, 60000.0f, distance * zoom );
}
void idLevelEditorPreview::Select( oq4editor::EntityId id ) {
	if ( id == selected ) { return; }
	for ( auto changed : { selected, id } ) {
		const auto found = records.find( changed );
		if ( found == records.end() || found->second.handle < 0 ) { continue; }
		auto &record = found->second;
		Tint( record, changed == id );
		world->UpdateEntityDef( record.handle, &record.entity );
	}
	selected = id;
}

void idLevelEditorPreview::Pan( float x, float y ) {
	const idMat3 axis = idAngles( pitch, yaw, 0 ).ToMat3();
	target += ( axis[1] * x + axis[2] * y ) * distance * 1.53465f;
}

oq4editor::EntityId idLevelEditorPreview::Pick( float x, float y, float aspect ) const {
	if ( !world || aspect <= 0 ) { return 0; }
	const idMat3 axis = idAngles( pitch, yaw, 0 ).ToMat3();
	const idVec3 start = target - axis[0] * distance;
	idVec3 ray = axis[0] + axis[1] * ( ( 1.0f - 2.0f * x ) * 0.767327f ) + axis[2] * ( ( 1.0f - 2.0f * y ) * 0.767327f / aspect );
	ray.Normalize();
	const idVec3 end = start + ray * 2000000.0f;
	float nearest = 1.0f;
	oq4editor::EntityId hit = 0;
	for ( const auto &[id, record] : records ) {
		if ( record.handle < 0 || !( record.entity.bounds + record.entity.origin ).LineIntersection( start, end ) ) { continue; }
		if ( record.primitives.empty() ) {
			float scale;
			if ( ( record.entity.bounds + record.entity.origin ).RayIntersection( start, end - start, scale ) && scale >= 0 && scale < nearest ) { nearest = scale; hit = id; }
			continue;
		}
		modelTrace_t trace = {};
		if ( world->ModelTrace( trace, record.handle, start, end, 0.0f ) && trace.fraction < nearest ) { nearest = trace.fraction; hit = id; }
	}
	return hit;
}

void idLevelEditorPreview::Draw( idDeviceContext *dc, const idRectangle &rect, int time ) {
	if ( !world || rect.w <= 0 || rect.h <= 0 ) { return; }
	float x = rect.x, y = rect.y, width = rect.w, height = rect.h;
	if ( dc->ClippedCoords( &x, &y, &width, &height, nullptr, nullptr, nullptr, nullptr ) || width <= 0 || height <= 0 ) { return; }
	renderView_t view = {};
	view.fov_x = 75.0f;
	view.fov_y = 2.0f * atan( tan( view.fov_x * idMath::M_DEG2RAD * 0.5f ) * height / width ) * idMath::M_RAD2DEG;
	dc->AdjustCoords( &x, &y, &width, &height );
	const float sx = engineWindowState.uiViewportWidth / 640.0f, sy = engineWindowState.uiViewportHeight / 480.0f;
	view.x = idMath::Ftoi( ( engineWindowState.uiViewportX + x * sx ) * 640.0f / engineWindowState.vidWidth );
	view.y = idMath::Ftoi( ( engineWindowState.uiViewportY + y * sy ) * 480.0f / engineWindowState.vidHeight );
	view.width = Max( 1, idMath::Ftoi( width * sx * 640.0f / engineWindowState.vidWidth ) );
	view.height = Max( 1, idMath::Ftoi( height * sy * 480.0f / engineWindowState.vidHeight ) );
	view.viewaxis = idAngles( pitch, yaw, 0 ).ToMat3();
	view.vieworg = target - view.viewaxis[0] * distance;
	view.time = time;
	for ( int i = 0; i < 4; ++i ) { view.shaderParms[i] = 1.0f; }
	renderLight_t light = {}; light.axis.Identity(); light.origin = view.vieworg;
	light.pointLight = true; light.noShadows = true;
	light.lightRadius.Set( 65536, 65536, 65536 );
	for ( int i = 0; i < 4; ++i ) { light.shaderParms[i] = 1.0f; }
	if ( lightHandle < 0 ) { lightHandle = world->AddLightDef( &light ); } else { world->UpdateLightDef( lightHandle, &light ); }
	world->RenderScene( &view );
}
