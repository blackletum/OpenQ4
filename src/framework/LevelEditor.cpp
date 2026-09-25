// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditor.h"
#include "../tools/leveleditor/MapWorkspace.h"

#include <limits>
#include <memory>

static idCVar editorExperimental_autoSaveSeconds( "editorExperimental_autoSaveSeconds", "120", CVAR_TOOL | CVAR_ARCHIVE | CVAR_INTEGER,
	"seconds between dirty editor recovery saves (0 disables periodic saves)", 0, 3600 );

namespace {

class EngineMapStorage final : public oq4editor::MapStorage {
public:
	bool Read( const std::string &path, std::string &text, bool &exists, std::string &error ) override {
		text.clear();
		idFile *file = fileSystem->OpenFileRead( path.c_str() );
		exists = file != NULL;
		if ( !file ) { return true; }
		const int length = file->Length();
		if ( length < 0 || static_cast<std::size_t>( length ) > oq4editor::MapDocument::MaxSourceBytes ) {
			fileSystem->CloseFile( file ); error = "file exceeds the 64 MiB editor limit: " + path; return false;
		}
		text.resize( length );
		const bool ok = length == 0 || file->Read( &text[0], length ) == length;
		fileSystem->CloseFile( file );
		if ( !ok ) { error = "incomplete read: " + path; }
		return ok;
	}
	bool WriteAndSync( const std::string &path, const std::string &text, std::string &error ) override {
		idFile *file = fileSystem->OpenFileWrite( path.c_str(), "fs_savepath" );
		if ( !file ) { error = "could not open editor output: " + path; return false; }
		const bool complete = file->Write( text.data(), static_cast<int>( text.size() ) ) == static_cast<int>( text.size() );
		const bool synced = complete && file->Sync();
		fileSystem->CloseFile( file );
		if ( !synced ) { error = "could not write and sync editor output: " + path; }
		return synced;
	}
	bool Promote( const std::string &from, const std::string &to, std::string &error ) override {
		if ( fileSystem->PromoteFile( from.c_str(), to.c_str(), "fs_savepath" ) ) { return true; }
		error = "could not replace editor output: " + to;
		return false;
	}
	void Remove( const std::string &path ) override { fileSystem->RemoveFileChecked( path.c_str(), "fs_savepath" ); }
};

EngineMapStorage editorStorage;
std::unique_ptr<oq4editor::MapWorkspace> editorExperimentalWorkspace;
unsigned int lastRecoveryCheck = 0;

void PrintStatus() {
	const auto &document = editorExperimentalWorkspace->Document();
	std::size_t primitives = 0;
	for ( const auto &entity : document.Entities() ) { primitives += entity.primitiveCount; }
	common->Printf( "editor status: open=%d dirty=%d entities=%zu primitives=%zu undo=%d redo=%d historyBytes=%zu path=%s\n",
		document.IsOpen(), document.IsDirty(), document.Entities().size(), primitives,
		document.CanUndo(), document.CanRedo(), document.HistoryBytes(),
		editorExperimentalWorkspace->Path().empty() ? "<unnamed>" : editorExperimentalWorkspace->Path().c_str() );
}

bool ParseEntityId( const char *text, oq4editor::EntityId &id ) {
	id = 0;
	if ( !text || !*text ) { return false; }
	oq4editor::EntityId candidate = 0;
	for ( const char *scan = text; *scan; ++scan ) {
		if ( *scan < '0' || *scan > '9' || candidate > ( ( std::numeric_limits<oq4editor::EntityId>::max )() - ( *scan - '0' ) ) / 10 ) { return false; }
		candidate = candidate * 10 + ( *scan - '0' );
	}
	id = candidate;
	return id != 0;
}

void EditorCommand( const idCmdArgs &args ) {
	if ( args.Argc() == 1 || ( args.Argc() == 2 && idStr::Icmp( args.Argv(1), "workspace" ) == 0 ) ) {
		LevelEditor_OpenWorkspace(); return;
	}
	if ( idStr::Icmp( args.Argv(1), "help" ) == 0 ) {
		common->Printf(
			"Experimental level editor: 'editorExperimental' opens the workspace. Legacy 'editor' is unchanged.\n"
			"mapEdit new | open <map> | close [discard] | status\n"
			"mapEdit entities [filter] | get <handle>\n"
			"mapEdit set <handle> <key> <value> | unset <handle> <key>\n"
			"mapEdit create <classname> <name> | delete <handle> | undo | redo\n"
			"mapEdit save [map] | savecopy <new-map> | autosave | recover [map]\n"
			"Writes go to fs_savepath/baseoq4/maps; recover without a map restores an unnamed document.\n"
			"Use dmap <saved-map> and devmap <saved-map> for compilation and play testing.\n" );
		return;
	}
	if ( !editorExperimentalWorkspace ) { editorExperimentalWorkspace.reset( new oq4editor::MapWorkspace( editorStorage ) ); }
	auto &workspace = *editorExperimentalWorkspace;
	auto &document = workspace.Document();
	const std::string command = args.Argv(1);
	std::string error;
	bool result = false;
	oq4editor::EntityId id = 0;
	if ( args.Argc() > 2 ) { ParseEntityId( args.Argv(2), id ); }
	if ( command == "new" && args.Argc() == 2 ) { result = workspace.New( error ); }
	else if ( command == "open" && args.Argc() == 3 ) { result = workspace.Open( args.Argv(2), error ); }
	else if ( command == "close" && ( args.Argc() == 2 || ( args.Argc() == 3 && idStr::Cmp( args.Argv(2), "discard" ) == 0 ) ) ) {
		result = workspace.Close( args.Argc() == 3, error );
	} else if ( command == "status" && args.Argc() == 2 ) { PrintStatus(); return; }
	else if ( command == "entities" && args.Argc() <= 3 ) {
		for ( const auto &entity : document.Entities() ) {
			const std::string label = entity.Value( "name" ) + " " + entity.Value( "classname" );
			if ( args.Argc() == 3 && idStr::FindText( label.c_str(), args.Argv(2), false ) < 0 ) { continue; }
			common->Printf( "editor entity: %llu class=%s name=%s primitives=%zu\n",
				static_cast<unsigned long long>( entity.id ), entity.Value( "classname" ).c_str(), entity.Value( "name" ).c_str(), entity.primitiveCount );
		}
		return;
	} else if ( command == "get" && args.Argc() == 3 ) {
		const auto *entity = document.Find( id );
		if ( entity ) {
			for ( const auto &property : entity->properties ) { common->Printf( "  %s = %s\n", property.key.c_str(), property.value.c_str() ); }
			return;
		}
		error = "unknown entity handle";
	} else if ( command == "set" && args.Argc() == 5 ) { result = document.SetProperty( id, args.Argv(3), args.Argv(4), error ); }
	else if ( command == "unset" && args.Argc() == 4 ) { result = document.RemoveProperty( id, args.Argv(3), error ); }
	else if ( command == "create" && args.Argc() == 4 ) {
		result = document.AddEntity( args.Argv(2), args.Argv(3), id, error );
		if ( result ) { common->Printf( "editor created: %llu\n", static_cast<unsigned long long>( id ) ); }
	} else if ( command == "delete" && args.Argc() == 3 ) { result = document.DeleteEntity( id, error ); }
	else if ( command == "undo" && args.Argc() == 2 ) { result = document.Undo( error ); }
	else if ( command == "redo" && args.Argc() == 2 ) { result = document.Redo( error ); }
	else if ( command == "save" && args.Argc() <= 3 ) { result = workspace.Save( args.Argc() == 3 ? args.Argv(2) : "", false, error ); }
	else if ( command == "savecopy" && args.Argc() == 3 ) { result = workspace.Save( args.Argv(2), true, error ); }
	else if ( command == "autosave" && args.Argc() == 2 ) { result = workspace.AutoSave( error ); }
	else if ( command == "recover" && args.Argc() <= 3 ) { result = workspace.Recover( args.Argc() == 3 ? args.Argv(2) : "", error ); }
	else { error = "invalid arguments; use 'editorExperimental help'"; }
	if ( !result ) { common->Printf( "editor rejected: %s\n", error.c_str() ); return; }
	common->Printf( "editor %s: OK\n", command.c_str() );
	PrintStatus();
}

void EditorComplete( const idCmdArgs &args, void( *callback )( const char *s ) ) {
	static const char *commands[] = { "help", "new", "open", "close", "status", "entities", "get", "set", "unset", "create", "delete", "undo", "redo", "save", "savecopy", "autosave", "recover", NULL };
	if ( args.Argc() <= 2 ) {
		for ( int i = 0; commands[i]; ++i ) { callback( va( "%s %s", args.Argv(0), commands[i] ) ); }
	} else if ( idStr::Icmp( args.Argv(1), "open" ) == 0 ) {
		// The completion helper consumes argv[1] as the path. Preserve the
		// two-word command as a single token rather than completing "open".
		idCmdArgs mapArgs;
		mapArgs.AppendArg( va( "%s open", args.Argv(0) ) );
		mapArgs.AppendArg( args.Argv(2) );
		cmdSystem->ArgCompletion_FolderExtension( mapArgs, callback, "maps/", true, ".map", NULL );
	}
}

}

void LevelEditor_InitCommands() {
	// The legacy editor command remains owned by Radiant in tool-enabled builds.
	cmdSystem->AddCommand( "mapEdit", EditorCommand, CMD_FL_TOOL, "experimental map authoring document service", EditorComplete );
	cmdSystem->AddCommand( "editorExperimental", EditorCommand, CMD_FL_TOOL, "opens the experimental level editor", EditorComplete );
}

void LevelEditor_Frame() {
	LevelEditor_UIFrame();
	if ( !editorExperimentalWorkspace || !editorExperimentalWorkspace->Document().IsDirty() || editorExperimental_autoSaveSeconds.GetInteger() <= 0 ) { return; }
	const unsigned int now = static_cast<unsigned int>( Sys_Milliseconds() );
	if ( now - lastRecoveryCheck < static_cast<unsigned int>( editorExperimental_autoSaveSeconds.GetInteger() ) * 1000U ) { return; }
	lastRecoveryCheck = now;
	std::string error;
	if ( !editorExperimentalWorkspace->AutoSave( error ) ) { common->Printf( "editor recovery failed: %s\n", error.c_str() ); }
}

void LevelEditor_Shutdown() {
	LevelEditor_UIShutdown();
	if ( !editorExperimentalWorkspace ) { return; }
	std::string error;
	if ( !editorExperimentalWorkspace->AutoSave( error ) ) { common->Printf( "editor shutdown recovery failed: %s\n", error.c_str() ); }
	editorExperimentalWorkspace.reset();
}

oq4editor::MapWorkspace &LevelEditor_Workspace() {
	if ( !editorExperimentalWorkspace ) { editorExperimentalWorkspace.reset( new oq4editor::MapWorkspace( editorStorage ) ); }
	return *editorExperimentalWorkspace;
}
