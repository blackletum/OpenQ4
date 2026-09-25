// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_DOCUMENT_H
#define OPENQ4_EDITOR_MAP_DOCUMENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Authoring state deliberately has no dependency on a window, renderer, live
// game world, or idMapFile::Resolve. Primitive source remains lossless until a
// geometry tool explicitly edits it. This is a document service, not a second
// geometry compiler: dmap remains responsible for geometric validation.
namespace oq4editor {

using EntityId = std::uint64_t;

struct Property {
	std::string key;
	std::string value;
	std::size_t begin = 0;
	std::size_t valueBegin = 0;
	std::size_t end = 0;
};

struct Entity {
	EntityId id = 0;
	std::string prefix;
	std::string source;
	std::vector<Property> properties;
	std::vector<std::pair<std::size_t, std::size_t>> primitiveRanges;
	std::size_t primitiveCount = 0;
	std::size_t propertyInsert = 0;
	std::string Value( const std::string &key ) const;
};

bool EqualNoCase( const std::string &a, const std::string &b );

class MapDocument {
public:
	static constexpr std::size_t MaxSourceBytes = 64 * 1024 * 1024;
	static constexpr std::size_t MaxHistoryBytes = 16 * 1024 * 1024;
	static constexpr std::size_t MaxHistoryEntries = 256;

	bool OpenText( const std::string &source, std::string &error );
	void New();
	void Close();
	bool IsOpen() const { return open; }
	int Version() const { return version; }
	bool IsDirty() const { return open && revision != savedRevision; }
	void MarkSaved() { savedRevision = revision; }
	void MarkUnsaved() { savedRevision = 0; }
	std::uint64_t Revision() const { return revision; }
	std::size_t HistoryBytes() const { return historyBytes; }
	bool CanUndo() const { return cursor != 0; }
	bool CanRedo() const { return cursor < history.size(); }
	const std::vector<Entity> &Entities() const { return entities; }
	const Entity *Find( EntityId id ) const;
	std::string Serialize() const;

	bool SetProperty( EntityId id, const std::string &key, const std::string &value, std::string &error );
	bool RemoveProperty( EntityId id, const std::string &key, std::string &error );
	bool AddEntity( const std::string &classname, const std::string &name, EntityId &id, std::string &error );
	bool DeleteEntity( EntityId id, std::string &error );
	bool Undo( std::string &error );
	bool Redo( std::string &error );

private:
	enum class ChangeKind { Text, Insert, Erase };
	struct Change {
		ChangeKind kind = ChangeKind::Text;
		EntityId id = 0;
		std::size_t index = 0;
		std::size_t offset = 0;
		std::string before, after, prefix;
		std::uint64_t beforeRevision = 0, afterRevision = 0;
		std::size_t Bytes() const { return before.size() + after.size() + prefix.size() + sizeof( Change ); }
	};
	bool Commit( Change change, std::string &error );
	void Apply( const Change &change, bool forward );
	bool CheckProperty( const Entity &entity, const std::string &key, const std::string &value, std::string &error ) const;
	bool NameAvailable( const std::string &name, EntityId except ) const;
	std::size_t IndexOf( EntityId id ) const;
	void ResetHistory();

	std::vector<Entity> entities;
	std::vector<Change> history;
	std::string header, tail;
	std::size_t cursor = 0, historyBytes = 0, sourceBytes = 0;
	EntityId nextId = 1;
	std::uint64_t revision = 0, savedRevision = 0, nextRevision = 1;
	bool open = false;
	int version = 3;
};

}
#endif
