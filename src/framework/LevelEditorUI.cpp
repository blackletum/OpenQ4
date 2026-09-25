// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditor.h"
#include "LevelEditorPreview.h"
#include "../tools/leveleditor/MapWorkspace.h"
#include "../ui/DeviceContext.h"
#include "../ui/Window.h"
#include <memory>
#include <vector>
#include <cmath>
#include <cstdlib>

static idCVar editorExperimental_leftPane( "editorExperimental_leftPane", "154", CVAR_TOOL | CVAR_ARCHIVE | CVAR_FLOAT, "editor browser width", 130, 210 );
static idCVar editorExperimental_rightPane( "editorExperimental_rightPane", "188", CVAR_TOOL | CVAR_ARCHIVE | CVAR_FLOAT, "editor inspector width", 160, 220 );

namespace {
idUserInterface *workspaceGui = nullptr;
std::unique_ptr<idLevelEditorPreview> preview;
oq4editor::EntityId selected = 0;
std::uint64_t displayedRevision = 0;
std::vector<oq4editor::EntityId> entityRows;
std::vector<std::string> browserRows, propertyRows;
std::string pendingOperation, pendingPath;
std::string displayedPath;
bool switchingWorkspace = false;
bool displayedDirty = false;
int browserMode = 0;

class WorkspaceTransition {
public:
	WorkspaceTransition() : previous( switchingWorkspace ) { switchingWorkspace = true; }
	~WorkspaceTransition() { switchingWorkspace = previous; }
private:
	bool previous;
};

const char *Tr( const char *key ) { return common->GetLanguageDict()->GetString( key ); }
void Message( const char *key ) { workspaceGui->SetStateString( "ed_message", Tr( key ) ); }
void Result( bool ok, const std::string &error ) {
	Message( ok ? "#str_42926" : "#str_42927" );
	if ( !ok ) { common->Printf( "editor workspace rejected: %s\n", error.c_str() ); }
}

// Authored text is data, including tabs/newlines and Quake color escapes.
// Strip list separators/control bytes so a name cannot invent extra columns.
std::string ListText( std::string text ) {
	for ( char &c : text ) { if ( static_cast<unsigned char>( c ) < 32 || c == '^' ) { c = ' '; } }
	return text;
}

void ClearList( const char *name ) {
	for ( int i = 0; workspaceGui->State().FindKey( va( "%s_item_%d", name, i ) ); ++i ) {
		workspaceGui->DeleteStateVar( va( "%s_item_%d", name, i ) );
	}
}

void Inspector( bool preserveKey = false ) {
	const std::string previousKey = preserveKey ? workspaceGui->GetStateString( "ed_key" ) : "";
	ClearList( "edProperties" ); propertyRows.clear();
	const auto *entity = LevelEditor_Workspace().Document().Find( selected );
	workspaceGui->SetStateString( "ed_selected", entity ? ListText( entity->Value( "classname" ) + " / " + entity->Value( "name" ) ).c_str() : Tr( "#str_42923" ) );
	if ( entity ) {
		for ( const auto &property : entity->properties ) {
			workspaceGui->SetStateString( va( "edProperties_item_%d", static_cast<int>( propertyRows.size() ) ), ListText( property.key + " = " + property.value ).c_str() );
			propertyRows.push_back( property.key );
		}
	}
	workspaceGui->SetStateInt( "edProperties_sel_0", -1 );
	workspaceGui->SetStateString( "ed_key", "" );
	workspaceGui->SetStateString( "ed_value", "" );
	if ( entity && !previousKey.empty() ) {
		for ( std::size_t i = 0; i < propertyRows.size(); ++i ) {
			if ( oq4editor::EqualNoCase( propertyRows[i], previousKey ) ) {
				workspaceGui->SetStateInt( "edProperties_sel_0", static_cast<int>( i ) );
				workspaceGui->SetStateString( "ed_key", propertyRows[i].c_str() );
				workspaceGui->SetStateString( "ed_value", entity->Value( propertyRows[i] ).c_str() );
				break;
			}
		}
	}
}

void Browser() {
	ClearList( "edBrowser" ); entityRows.clear(); browserRows.clear();
	const char *filter = workspaceGui->GetStateString( "ed_filter" );
	const auto accepts = [filter]( const std::string &name ) { return !*filter || idStr::FindText( name.c_str(), filter, false ) >= 0; };
	if ( browserMode == 0 ) {
		for ( const auto &entity : LevelEditor_Workspace().Document().Entities() ) {
			std::string label = entity.Value( "name" );
			if ( label.empty() ) { label = entity.Value( "classname" ); }
			if ( !accepts( label + " " + entity.Value( "classname" ) ) ) { continue; }
			entityRows.push_back( entity.id ); browserRows.push_back( ListText( label ) );
		}
	} else if ( browserMode == 1 ) {
		idFileList *files = fileSystem->ListFilesTree( "maps", ".map", true );
		for ( int i = 0; i < files->GetNumFiles(); ++i ) {
			const std::string path = files->GetFile( i );
			if ( accepts( path ) ) { browserRows.push_back( path ); }
		}
		fileSystem->FreeFileList( files );
	} else {
		for ( int i = 0; i < declManager->GetNumDecls( DECL_ENTITYDEF ); ++i ) {
			// Catalog names only: opening the browser does not parse every entity
			// definition or load the models/materials named by those definitions.
			const std::string name = declManager->DeclByIndex( DECL_ENTITYDEF, i, false )->GetName();
			if ( accepts( name ) ) { browserRows.push_back( name ); }
		}
	}
	int row = -1;
	for ( std::size_t i = 0; i < browserRows.size(); ++i ) {
		workspaceGui->SetStateString( va( "edBrowser_item_%d", static_cast<int>( i ) ), browserRows[i].c_str() );
		if ( browserMode == 0 && entityRows[i] == selected ) { row = static_cast<int>( i ); }
	}
	workspaceGui->SetStateInt( "edBrowser_sel_0", row );
	workspaceGui->SetStateString( "ed_browserCount", va( Tr( "#str_42924" ), static_cast<int>( browserRows.size() ) ) );
	workspaceGui->SetStateInt( "ed_mode", browserMode );
}

void Layout() {
	workspaceGui->SetStateFloat( "ed_left", editorExperimental_leftPane.GetFloat() );
	workspaceGui->SetStateFloat( "ed_right", editorExperimental_rightPane.GetFloat() );
}

void Refresh( bool rebuildPreview = true ) {
	auto &workspace = LevelEditor_Workspace();
	const auto &document = workspace.Document();
	if ( displayedPath != workspace.Path() ) {
		displayedPath = workspace.Path(); workspaceGui->SetStateString( "ed_path", displayedPath.c_str() );
	}
	if ( !document.Find( selected ) ) { selected = document.Entities().empty() ? 0 : document.Entities().front().id; }
	workspaceGui->SetStateString( "ed_title", workspace.Path().empty() ? Tr( "#str_42922" ) : workspace.Path().c_str() );
	workspaceGui->SetStateString( "ed_dirty", Tr( document.IsDirty() ? "#str_42920" : "#str_42921" ) );
	workspaceGui->SetStateInt( "ed_canUndo", document.CanUndo() );
	workspaceGui->SetStateInt( "ed_canRedo", document.CanRedo() );
	Layout(); Browser(); Inspector( true );
	if ( rebuildPreview ) {
		const bool first = !preview;
		if ( first ) { preview = std::make_unique<idLevelEditorPreview>(); }
		preview->Rebuild( document );
		if ( first ) { preview->Focus( selected ); }
		preview->Select( selected );
		workspaceGui->SetStateString( "ed_previewStatus", va( Tr( "#str_42925" ), preview->Triangles(), preview->Skipped() ) );
	}
	displayedRevision = document.Revision();
	displayedDirty = document.IsDirty();
	workspaceGui->StateChanged( common->GetPresentationTime() );
}

void ReplaceDocument( const std::string &operation, const std::string &path, bool discard = false ) {
	auto &workspace = LevelEditor_Workspace();
	if ( !discard && workspace.Document().IsDirty() ) {
		pendingOperation = operation; pendingPath = path;
		workspaceGui->SetStateBool( "ed_confirm", true );
		workspaceGui->StateChanged( common->GetPresentationTime() );
		workspaceGui->HandleNamedEvent( "workspaceConfirm" );
		return;
	}
	std::string error;
	bool ok = operation == "new" ? workspace.New( error, discard ) : operation == "close" ? workspace.Close( discard, error ) : workspace.Open( path, error, discard );
	Result( ok, error );
	if ( ok ) { selected = 0; preview.reset(); workspaceGui->SetStateString( "ed_path", workspace.Path().c_str() ); Refresh(); }
}

void Action( const std::string &action ) {
	if ( !workspaceGui || session->GetActiveGUI() != workspaceGui ) { return; }
	auto &workspace = LevelEditor_Workspace();
	auto &document = workspace.Document();
	std::string error;
	if ( !pendingOperation.empty() ) {
		if ( action == "confirm" ) {
			const std::string operation = pendingOperation, path = pendingPath;
			pendingOperation.clear(); pendingPath.clear(); workspaceGui->SetStateBool( "ed_confirm", false );
			ReplaceDocument( operation, path, true );
		} else if ( action == "cancel" ) {
			pendingOperation.clear(); pendingPath.clear(); workspaceGui->SetStateBool( "ed_confirm", false );
		}
		workspaceGui->StateChanged( common->GetPresentationTime() );
		if ( pendingOperation.empty() ) { workspaceGui->HandleNamedEvent( "workspaceResume" ); }
		return;
	}
	if ( action == "new" || action == "open" || action == "close" ) { ReplaceDocument( action, workspaceGui->GetStateString( "ed_path" ) ); }
	else if ( action == "hide" ) { WorkspaceTransition transition; session->StartMenu(); preview.reset(); }
	else if ( action == "save" || action == "copy" ) {
		Result( workspace.Save( workspaceGui->GetStateString( "ed_path" ), action == "copy", error ), error ); Refresh( false );
	} else if ( action == "undo" || action == "redo" ) {
		Result( action == "undo" ? document.Undo( error ) : document.Redo( error ), error ); Refresh();
	} else if ( action == "apply" || action == "unset" ) {
		const std::string key = workspaceGui->GetStateString( "ed_key" ), value = workspaceGui->GetStateString( "ed_value" );
		const bool ok = action == "apply" ? document.SetProperty( selected, key, value, error ) : document.RemoveProperty( selected, key, error );
		Result( ok, error ); if ( ok ) { Refresh(); }
	} else if ( action == "create" ) {
		const bool ok = document.AddEntity( workspaceGui->GetStateString( "ed_class" ), workspaceGui->GetStateString( "ed_name" ), selected, error );
		Result( ok, error ); if ( ok ) { browserMode = 0; Refresh(); preview->Focus( selected ); }
	} else if ( action == "delete" ) {
		Result( document.DeleteEntity( selected, error ), error ); Refresh();
	} else if ( action == "property" ) {
		const int row = workspaceGui->GetStateInt( "edProperties_sel_0", "-1" );
		const auto *entity = document.Find( selected );
		if ( entity && row >= 0 && row < static_cast<int>( propertyRows.size() ) ) {
			workspaceGui->SetStateString( "ed_key", propertyRows[row].c_str() );
			workspaceGui->SetStateString( "ed_value", entity->Value( propertyRows[row] ).c_str() );
		}
	} else if ( action == "select" ) {
		const int row = workspaceGui->GetStateInt( "edBrowser_sel_0", "-1" );
		if ( row >= 0 && row < static_cast<int>( browserRows.size() ) ) {
			if ( browserMode == 0 ) { selected = entityRows[row]; Inspector(); if ( preview ) { preview->Select( selected ); } }
			else if ( browserMode == 1 ) { workspaceGui->SetStateString( "ed_path", browserRows[row].c_str() ); }
			else { workspaceGui->SetStateString( "ed_class", browserRows[row].c_str() ); }
		}
	} else if ( action == "entities" || action == "maps" || action == "classes" ) {
		browserMode = action == "entities" ? 0 : action == "maps" ? 1 : 2; Browser();
	} else if ( action == "filter" ) { Browser(); }
	else if ( action == "frame" && preview ) { preview->Focus( selected ); }
	else if ( action == "all" && preview ) { preview->Focus( 0 ); }
	else if ( action == "left" && preview ) { preview->Orbit( -15, 0, 1 ); }
	else if ( action == "right" && preview ) { preview->Orbit( 15, 0, 1 ); }
	else if ( action == "up" && preview ) { preview->Orbit( 0, 15, 1 ); }
	else if ( action == "down" && preview ) { preview->Orbit( 0, -15, 1 ); }
	else if ( action == "in" && preview ) { preview->Orbit( 0, 0, 0.8f ); }
	else if ( action == "out" && preview ) { preview->Orbit( 0, 0, 1.25f ); }
	else if ( action == "layout" ) {
		const bool wide = editorExperimental_leftPane.GetInteger() < 175;
		editorExperimental_leftPane.SetInteger( wide ? 190 : 154 ); editorExperimental_rightPane.SetInteger( wide ? 210 : 188 ); Layout();
	}
	workspaceGui->StateChanged( common->GetPresentationTime() );
}

const char *GuiCommand( const char *commands ) {
	idCmdArgs args( commands, false );
	for ( int i = 0; i < args.Argc(); ++i ) {
		if ( !idStr::Cmp( args.Argv( i ), "editorExperimentalAction" ) && i + 1 < args.Argc() ) { Action( args.Argv( ++i ) ); }
		else if ( !idStr::Cmp( args.Argv( i ), "play" ) && i + 1 < args.Argc() ) {
			const char *sound = args.Argv( ++i );
			if ( session->menuSoundWorld ) { session->menuSoundWorld->PlayShaderDirectly( sound ); }
		}
	}
	return "";
}

void WorkspaceCommand( const idCmdArgs &args ) {
	// Scriptable workspace actions use the same controller as GUI widgets.
	// This is not a keyboard/mouse event injector.
	if ( !workspaceGui || session->GetActiveGUI() != workspaceGui ) { common->Printf( "Open 'editorExperimental' first.\n" ); return; }
	if ( args.Argc() == 4 && idStr::Cmp( args.Argv(1), "field" ) == 0 ) {
		const char *fields[] = { "ed_path", "ed_filter", "ed_key", "ed_value", "ed_class", "ed_name", "edBrowser_sel_0", "edProperties_sel_0" };
		for ( const char *field : fields ) {
			if ( !idStr::Cmp( args.Argv(2), field ) ) { workspaceGui->SetStateString( field, args.Argv(3) ); workspaceGui->StateChanged( common->GetPresentationTime() ); return; }
		}
	} else if ( args.Argc() == 3 && idStr::Cmp( args.Argv(1), "action" ) == 0 ) { Action( args.Argv(2) ); return; }
	else if ( args.Argc() == 2 && idStr::Cmp( args.Argv(1), "status" ) == 0 ) {
		common->Printf( "editor workspace: rows=%d selected=%llu left=%.0f right=%.0f confirm=%d key=%s\n",
			workspaceGui->GetStateInt( "edBrowser_num" ), static_cast<unsigned long long>( selected ),
			editorExperimental_leftPane.GetFloat(), editorExperimental_rightPane.GetFloat(), !pendingOperation.empty(), workspaceGui->GetStateString( "ed_key" ) );
		return;
	} else if ( args.Argc() >= 5 && args.Argc() <= 6 && idStr::Cmp( args.Argv(1), "view" ) == 0 ) {
		float values[3] = {};
		for ( int i = 3; i < args.Argc(); ++i ) {
			char *end = nullptr; values[i-3] = std::strtof( args.Argv(i), &end );
			if ( !end || end == args.Argv(i) || *end || !std::isfinite( values[i-3] ) ) { common->Printf( "editor workspace rejected: invalid view argument\n" ); return; }
		}
		if ( args.Argc() == 6 && idStr::Cmp( args.Argv(2), "orbit" ) == 0 ) { LevelEditor_Navigate( values[0], values[1], values[2] ); return; }
		if ( args.Argc() == 6 && idStr::Cmp( args.Argv(2), "pick" ) == 0 ) { LevelEditor_Pick( values[0], values[1], values[2] ); return; }
		if ( args.Argc() == 5 && idStr::Cmp( args.Argv(2), "pan" ) == 0 ) { LevelEditor_Pan( values[0], values[1] ); return; }
		if ( args.Argc() == 5 && idStr::Cmp( args.Argv(2), "panes" ) == 0 ) {
			LevelEditor_ResizePane( 1, values[0] - editorExperimental_leftPane.GetFloat() ); LevelEditor_ResizePane( 2, editorExperimental_rightPane.GetFloat() - values[1] ); return;
		}
	}
	common->Printf( "editorExperimentalWorkspace field <field> <value> | action <action> | status | view <orbit|pan|pick|panes> <values>\n" );
}
}

void LevelEditor_OpenWorkspace() {
	if ( !renderSystem->IsOpenGLRunning() ) { common->Printf( "editor workspace requires a client renderer; use 'editorExperimental help' for document commands.\n" ); return; }
	WorkspaceTransition transition;
	if ( !workspaceGui ) {
		workspaceGui = uiManager->FindGui( "guis/level_editor_experimental.gui", true, false, true );
		if ( !workspaceGui ) { common->Warning( "Could not load the level editor GUI" ); return; }
		workspaceGui->SetStateString( "ed_class", "light" );
		workspaceGui->SetStateString( "ed_name", "light_1" );
		workspaceGui->SetStateString( "ed_path", LevelEditor_Workspace().Path().c_str() );
		cmdSystem->AddCommand( "editorExperimentalWorkspace", WorkspaceCommand, CMD_FL_TOOL, "level editor workspace actions" );
	}
	std::string error;
	if ( !LevelEditor_Workspace().Document().IsOpen() ) { LevelEditor_Workspace().New( error ); }
	console->Close(); session->SetGUI( workspaceGui, GuiCommand );
	Message( "#str_42945" ); Refresh();
}

void LevelEditor_UIFrame() {
	if ( !workspaceGui ) { return; }
	if ( session->GetActiveGUI() != workspaceGui ) { preview.reset(); return; }
	const auto &workspace = LevelEditor_Workspace();
	const bool changed = displayedRevision != workspace.Document().Revision();
	if ( changed || displayedPath != workspace.Path() || displayedDirty != workspace.Document().IsDirty() || !preview ) { Refresh( changed || !preview ); }
}
bool LevelEditor_UIIsActive() { return switchingWorkspace || ( workspaceGui && session->GetActiveGUI() == workspaceGui ); }
void LevelEditor_UIShutdown() {
	preview.reset(); workspaceGui = nullptr; selected = 0; displayedRevision = 0; displayedDirty = false; switchingWorkspace = false; browserMode = 0;
	pendingOperation.clear(); pendingPath.clear(); displayedPath.clear(); entityRows.clear(); browserRows.clear(); propertyRows.clear();
}
void LevelEditor_DrawPreview( idDeviceContext *dc, const idRectangle &rect, int time ) {
	if ( preview && workspaceGui && session->GetActiveGUI() == workspaceGui ) { preview->Draw( dc, rect, time ); }
}

bool LevelEditor_UIInputEnabled() { return workspaceGui && session->GetActiveGUI() == workspaceGui && pendingOperation.empty(); }
void LevelEditor_Navigate( float yaw, float pitch, float zoom ) {
	if ( LevelEditor_UIInputEnabled() && preview && zoom > 0 && zoom <= 10 && std::isfinite( yaw ) && std::isfinite( pitch ) && std::isfinite( zoom ) ) {
		preview->Orbit( idMath::ClampFloat( -360, 360, yaw ), idMath::ClampFloat( -180, 180, pitch ), zoom );
	}
}
void LevelEditor_Pan( float x, float y ) {
	if ( LevelEditor_UIInputEnabled() && preview && std::isfinite( x ) && std::isfinite( y ) ) {
		preview->Pan( idMath::ClampFloat( -1, 1, x ), idMath::ClampFloat( -1, 1, y ) );
	}
}
void LevelEditor_Pick( float x, float y, float aspect ) {
	if ( !LevelEditor_UIInputEnabled() || !preview || x < 0 || x > 1 || y < 0 || y > 1 || aspect < 0.05f || aspect > 20 || !std::isfinite( x+y+aspect ) ) { return; }
	const auto hit = preview->Pick( x, y, aspect );
	if ( hit && LevelEditor_Workspace().Document().Find( hit ) ) {
		selected = hit; preview->Select( selected ); Inspector();
		browserMode = 0; Browser(); workspaceGui->StateChanged( common->GetPresentationTime() );
	}
}
void LevelEditor_ResizePane( int pane, float delta ) {
	if ( !LevelEditor_UIInputEnabled() || !std::isfinite( delta ) ) { return; }
	if ( pane == 1 ) { editorExperimental_leftPane.SetFloat( idMath::ClampFloat( 130, 210, editorExperimental_leftPane.GetFloat() + delta ) ); }
	else if ( pane == 2 ) { editorExperimental_rightPane.SetFloat( idMath::ClampFloat( 160, 220, editorExperimental_rightPane.GetFloat() - delta ) ); }
	Layout(); workspaceGui->StateChanged( common->GetPresentationTime() );
}
void LevelEditor_FrameSelection() { if ( LevelEditor_UIInputEnabled() && preview ) { preview->Focus( selected ); } }
