// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapDocument.h"

#include <algorithm>
#include <utility>

namespace oq4editor {
namespace {

char Fold( char c ) { return c >= 'A' && c <= 'Z' ? c + ( 'a' - 'A' ) : c; }
bool Space( char c ) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string TrimEnd( std::string value ) {
	while ( !value.empty() && static_cast<unsigned char>( value.back() ) <= ' ' ) { value.pop_back(); }
	return value;
}

struct Token {
	std::size_t begin = 0, end = 0;
	std::string text;
	bool quoted = false;
};

// Quake map strings use literal backslashes (LEXFL_NOSTRINGESCAPECHARS).
// Comments and strings must not contribute braces to structural scanning.
class Scanner {
public:
	explicit Scanner( const std::string &text ) : source( text ) {}
	bool SameLine( std::size_t begin, std::size_t end ) const {
		return source.find_first_of( "\r\n", begin ) >= end;
	}
	bool Next( Token &token ) {
		while ( position < source.size() ) {
			if ( Space( source[position] ) ) { ++position; continue; }
			if ( source.compare( position, 2, "//" ) == 0 ) {
				position = source.find( '\n', position );
				if ( position == std::string::npos ) { position = source.size(); }
				continue;
			}
			if ( source.compare( position, 2, "/*" ) == 0 ) {
				const auto end = source.find( "*/", position + 2 );
				if ( end == std::string::npos ) { error = "unterminated comment"; return false; }
				position = end + 2;
				continue;
			}
			break;
		}
		if ( position == source.size() ) { return false; }
		token = Token();
		token.begin = position;
		const char first = source[position++];
		if ( first == '"' ) {
			token.quoted = true;
			const auto end = source.find( '"', position );
			if ( end == std::string::npos ) { error = "unterminated string"; return false; }
			token.text = source.substr( position, end - position );
			if ( token.text.find_first_of( "\r\n" ) != std::string::npos ) {
				error = "newline inside a map string"; return false;
			}
			position = end + 1;
		} else if ( first == '{' || first == '}' || first == '(' || first == ')' ) {
			token.text.assign( 1, first );
		} else {
			while ( position < source.size() && !Space( source[position] ) &&
				source[position] != '{' && source[position] != '}' && source[position] != '(' &&
				source[position] != ')' && source[position] != '"' &&
				source.compare( position, 2, "//" ) != 0 && source.compare( position, 2, "/*" ) != 0 ) { ++position; }
			token.text = source.substr( token.begin, position - token.begin );
		}
		token.end = position;
		return true;
	}
	std::size_t position = 0;
	std::string error;
private:
	const std::string &source;
};

bool Is( const Token &token, const char *text ) { return !token.quoted && token.text == text; }

bool ReadEntity( Scanner &scanner, std::size_t start, Entity &entity, std::string &error,
	const std::vector<std::pair<std::size_t, std::size_t>> *knownPrimitives = nullptr ) {
	Token token;
	bool geometrySeen = false;
	while ( scanner.Next( token ) ) {
		if ( Is( token, "}" ) ) {
			if ( !geometrySeen ) { entity.propertyInsert = token.begin - start; }
			return true;
		}
		if ( Is( token, "{" ) ) {
			if ( !geometrySeen ) { entity.propertyInsert = token.begin - start; }
			geometrySeen = true;
			const auto primitiveStart = token.begin - start;
			// Property edits cannot alter primitive bytes. Reuse validated source
			// ranges so editing a worldspawn field does not re-lex the whole level.
			if ( knownPrimitives && entity.primitiveCount < knownPrimitives->size() &&
				( *knownPrimitives )[entity.primitiveCount].first == primitiveStart ) {
				const auto range = ( *knownPrimitives )[entity.primitiveCount++];
				scanner.position = start + range.second;
				entity.primitiveRanges.push_back( range );
				continue;
			}
			++entity.primitiveCount;
			std::vector<char> nesting( 1, '{' );
			while ( !nesting.empty() && scanner.Next( token ) ) {
				if ( token.quoted ) { continue; }
				if ( token.text == "{" || token.text == "(" ) {
					if ( nesting.size() >= 64 ) { error = "primitive nesting exceeds 64 levels"; return false; }
					nesting.push_back( token.text[0] );
				} else if ( token.text == "}" || token.text == ")" ) {
					const char expected = token.text == "}" ? '{' : '(';
					if ( nesting.back() != expected ) { error = "mismatched primitive delimiter"; return false; }
					nesting.pop_back();
				}
			}
			if ( !nesting.empty() ) { error = "unterminated primitive"; return false; }
			entity.primitiveRanges.emplace_back( primitiveStart, scanner.position - start );
			continue;
		}
		if ( !token.quoted || token.text.empty() ) { error = "expected a quoted entity key"; return false; }
		Property property;
		property.key = token.text;
		property.begin = token.begin - start;
		const auto keyEnd = token.end;
		if ( !scanner.Next( token ) || !token.quoted || !scanner.SameLine( keyEnd, token.begin ) ) {
			error = "expected a quoted entity value on the same line"; return false;
		}
		property.value = token.text;
		// The engine trims trailing whitespace from both entity keys and values.
		// Keep the original bytes in source, but expose the same effective values.
		property.key = TrimEnd( std::move( property.key ) );
		property.value = TrimEnd( std::move( property.value ) );
		if ( property.key.empty() ) { error = "empty entity key"; return false; }
		property.valueBegin = token.begin - start;
		property.end = token.end - start;
		entity.properties.push_back( std::move( property ) );
		if ( entity.properties.size() > 16384 ) { error = "entity has too many properties"; return false; }
	}
	error = scanner.error.empty() ? "unterminated entity" : scanner.error;
	return false;
}

void Refresh( Entity &entity, bool reuseGeometry = false ) {
	const auto knownPrimitives = reuseGeometry ? entity.primitiveRanges : std::vector<std::pair<std::size_t, std::size_t>>();
	entity.properties.clear();
	entity.primitiveRanges.clear();
	entity.primitiveCount = 0;
	Scanner scanner( entity.source );
	Token token;
	std::string error;
	scanner.Next( token );
	ReadEntity( scanner, 0, entity, error, &knownPrimitives ); // only validated edits reach here
}

bool ValidString( const std::string &value ) {
	return value.size() <= 4096 && value.find_first_of( "\"\r\n" ) == std::string::npos &&
		value.find( '\0' ) == std::string::npos;
}

}

bool EqualNoCase( const std::string &a, const std::string &b ) {
	return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(),
		[]( char x, char y ) { return Fold( x ) == Fold( y ); } );
}

std::string Entity::Value( const std::string &key ) const {
	for ( auto it = properties.rbegin(); it != properties.rend(); ++it ) {
		if ( EqualNoCase( it->key, key ) ) { return it->value; }
	}
	return {};
}

void MapDocument::ResetHistory() { history.clear(); cursor = historyBytes = 0; }

bool MapDocument::OpenText( const std::string &source, std::string &error ) {
	error.clear();
	if ( source.size() > MaxSourceBytes || source.find( '\0' ) != std::string::npos ) {
		error = "map exceeds 64 MiB or contains an embedded NUL"; return false;
	}
	Scanner scanner( source );
	if ( source.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 ) { scanner.position = 3; }
	Token token;
	if ( !scanner.Next( token ) ) { error = "empty map"; return false; }
	int newVersion = 1;
	if ( Is( token, "Version" ) ) {
		if ( !scanner.Next( token ) || token.quoted || ( token.text != "1" && token.text != "2" && token.text != "3" ) ) {
			error = "supported map versions are 1, 2 and 3"; return false;
		}
		newVersion = token.text[0] - '0';
		if ( !scanner.Next( token ) ) { error = "map has no worldspawn"; return false; }
	}
	std::vector<Entity> candidate;
	const std::string newHeader = source.substr( 0, token.begin );
	std::size_t previousEnd = token.begin;
	do {
		if ( !Is( token, "{" ) ) { error = "expected an entity opening brace"; break; }
		Entity entity;
		const auto start = token.begin;
		entity.prefix = source.substr( previousEnd, start - previousEnd );
		if ( !ReadEntity( scanner, start, entity, error ) ) { break; }
		entity.source = source.substr( start, scanner.position - start );
		previousEnd = scanner.position;
		const std::string classname = entity.Value( "classname" );
		if ( classname.empty() || ( candidate.empty() != EqualNoCase( classname, "worldspawn" ) ) ) {
			error = "map must start with exactly one worldspawn; every entity needs a classname"; break;
		}
		candidate.push_back( std::move( entity ) );
		if ( candidate.size() > 65536 ) { error = "map has too many entities"; break; }
	} while ( scanner.Next( token ) );
	if ( !scanner.error.empty() ) { error = scanner.error; }
	if ( !error.empty() ) {
		error += " near line " + std::to_string( 1 + std::count( source.begin(), source.begin() + scanner.position, '\n' ) );
		return false;
	}
	// Publish only after the entire input has passed structural validation.
	for ( Entity &entity : candidate ) { entity.id = nextId++; }
	entities = std::move( candidate );
	header = newHeader;
	tail = source.substr( previousEnd );
	sourceBytes = source.size();
	ResetHistory();
	open = true;
	version = newVersion;
	revision = savedRevision = nextRevision++;
	return true;
}

void MapDocument::New() {
	std::string error;
	OpenText( "Version 3\n{\n\"classname\" \"worldspawn\"\n}\n", error );
	savedRevision = 0;
}

void MapDocument::Close() {
	entities.clear(); header.clear(); tail.clear(); ResetHistory();
	open = false; sourceBytes = 0; revision = savedRevision = 0;
}

std::size_t MapDocument::IndexOf( EntityId id ) const {
	for ( std::size_t i = 0; i < entities.size(); ++i ) { if ( entities[i].id == id ) { return i; } }
	return entities.size();
}

const Entity *MapDocument::Find( EntityId id ) const {
	const auto index = IndexOf( id );
	return index == entities.size() ? nullptr : &entities[index];
}

std::string MapDocument::Serialize() const {
	if ( !open ) { return {}; }
	std::string text;
	text.reserve( sourceBytes );
	text += header;
	for ( const Entity &entity : entities ) { text += entity.prefix; text += entity.source; }
	text += tail;
	return text;
}

bool MapDocument::NameAvailable( const std::string &name, EntityId except ) const {
	if ( name.empty() ) { return true; }
	for ( const Entity &entity : entities ) {
		if ( entity.id != except && EqualNoCase( entity.Value( "name" ), name ) ) { return false; }
	}
	return true;
}

bool MapDocument::CheckProperty( const Entity &entity, const std::string &key, const std::string &value, std::string &error ) const {
	if ( key.empty() || !ValidString( key ) || !ValidString( value ) ) {
		error = "keys/values must be at most 4096 bytes and cannot contain quotes, newlines or NULs"; return false;
	}
	if ( EqualNoCase( key, "classname" ) && ( value.empty() ||
		( entity.id == entities.front().id ) != EqualNoCase( value, "worldspawn" ) ) ) {
		error = "cannot remove a classname or change worldspawn ownership"; return false;
	}
	if ( EqualNoCase( key, "name" ) && !NameAvailable( value, entity.id ) ) {
		error = "another entity already uses this name"; return false;
	}
	if ( entity.primitiveCount != 0 && EqualNoCase( key, "origin" ) && value != entity.Value( key ) ) {
		error = "geometry entity origins require a geometry transform tool"; return false;
	}
	return true;
}

bool MapDocument::SetProperty( EntityId id, const std::string &keyInput, const std::string &valueInput, std::string &error ) {
	error.clear();
	if ( !ValidString( keyInput ) || !ValidString( valueInput ) ) {
		error = "keys/values must be at most 4096 bytes and cannot contain quotes, newlines or NULs"; return false;
	}
	const std::string key = TrimEnd( keyInput ), value = TrimEnd( valueInput );
	const Entity *entity = Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( !CheckProperty( *entity, key, value, error ) ) { return false; }
	Change change;
	change.id = id;
	for ( auto it = entity->properties.rbegin(); it != entity->properties.rend(); ++it ) {
		if ( EqualNoCase( it->key, key ) ) {
			if ( it->value == value ) { return true; }
			change.offset = it->valueBegin;
			change.before = entity->source.substr( it->valueBegin, it->end - it->valueBegin );
			change.after = "\"" + value + "\"";
			return Commit( std::move( change ), error );
		}
	}
	if ( entity->properties.size() >= 16384 ) { error = "entity has too many properties"; return false; }
	const char *newline = entity->source.find( "\r\n" ) != std::string::npos ? "\r\n" : "\n";
	change.offset = entity->propertyInsert;
	change.after = "\"" + key + "\" \"" + value + "\"" + newline;
	return Commit( std::move( change ), error );
}

bool MapDocument::RemoveProperty( EntityId id, const std::string &keyInput, std::string &error ) {
	error.clear();
	const std::string key = TrimEnd( keyInput );
	const Entity *entity = Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( EqualNoCase( key, "classname" ) || ( entity->primitiveCount && EqualNoCase( key, "origin" ) ) ) {
		error = "cannot remove classname or a geometry entity origin"; return false;
	}
	// Removing duplicate keys must not reveal an older shadowed value. One
	// replacement range makes all occurrences a single undoable transaction.
	std::size_t begin = entity->source.size(), end = 0;
	for ( const Property &property : entity->properties ) {
		if ( EqualNoCase( property.key, key ) ) { begin = ( std::min )( begin, property.begin ); end = property.end; }
	}
	if ( end == 0 ) { return true; }
	Change change;
	change.id = id; change.offset = begin;
	change.before = entity->source.substr( begin, end - begin );
	change.after = change.before;
	for ( auto it = entity->properties.rbegin(); it != entity->properties.rend(); ++it ) {
		if ( EqualNoCase( it->key, key ) ) { change.after.erase( it->begin - begin, it->end - it->begin ); }
	}
	return Commit( std::move( change ), error );
}

bool MapDocument::AddEntity( const std::string &classnameInput, const std::string &nameInput, EntityId &id, std::string &error ) {
	error.clear();
	const std::string classname = TrimEnd( classnameInput ), name = TrimEnd( nameInput );
	if ( !open ) { error = "no open document"; return false; }
	if ( classname.empty() || name.empty() || !ValidString( classnameInput ) || !ValidString( nameInput ) || EqualNoCase( classname, "worldspawn" ) ) {
		error = "a point entity needs a valid classname and unique nonempty name"; return false;
	}
	if ( !NameAvailable( name, 0 ) ) { error = "another entity already uses this name"; return false; }
	if ( entities.size() >= 65536 ) { error = "map has too many entities"; return false; }
	Change change;
	change.kind = ChangeKind::Insert;
	change.id = nextId++; change.index = entities.size(); change.prefix = "\n";
	change.after = "{\n\"classname\" \"" + classname + "\"\n\"name\" \"" + name + "\"\n\"origin\" \"0 0 0\"\n}";
	const EntityId newId = change.id;
	if ( !Commit( std::move( change ), error ) ) { return false; }
	id = newId;
	return true;
}

bool MapDocument::DeleteEntity( EntityId id, std::string &error ) {
	error.clear();
	const auto index = IndexOf( id );
	if ( index == entities.size() ) { error = "unknown entity handle"; return false; }
	if ( index == 0 ) { error = "cannot delete worldspawn"; return false; }
	Change change;
	change.kind = ChangeKind::Erase; change.id = id; change.index = index;
	change.before = entities[index].source; change.prefix = entities[index].prefix;
	return Commit( std::move( change ), error );
}

void MapDocument::Apply( const Change &change, bool forward ) {
	if ( change.kind == ChangeKind::Text ) {
		Entity &entity = entities[IndexOf( change.id )];
		const std::string &oldText = forward ? change.before : change.after;
		const std::string &newText = forward ? change.after : change.before;
		bool reuseGeometry = true;
		for ( auto &range : entity.primitiveRanges ) {
			if ( range.second <= change.offset ) { continue; }
			if ( range.first < change.offset + oldText.size() ) { reuseGeometry = false; break; }
			range.first = range.first - oldText.size() + newText.size();
			range.second = range.second - oldText.size() + newText.size();
		}
		entity.source.replace( change.offset, oldText.size(), newText );
		sourceBytes = sourceBytes - oldText.size() + newText.size();
		Refresh( entity, reuseGeometry );
	} else if ( ( change.kind == ChangeKind::Insert ) == forward ) {
		Entity entity;
		entity.id = change.id; entity.prefix = change.prefix;
		entity.source = change.kind == ChangeKind::Insert ? change.after : change.before;
		Refresh( entity );
		sourceBytes += entity.source.size() + entity.prefix.size();
		entities.insert( entities.begin() + change.index, std::move( entity ) );
	} else {
		const auto index = IndexOf( change.id );
		sourceBytes -= entities[index].source.size() + entities[index].prefix.size();
		entities.erase( entities.begin() + index );
	}
	revision = forward ? change.afterRevision : change.beforeRevision;
}

bool MapDocument::Commit( Change change, std::string &error ) {
	if ( change.Bytes() > MaxHistoryBytes ) { error = "edit exceeds the 16 MiB undo budget"; return false; }
	const std::size_t newBytes = sourceBytes - change.before.size() + change.after.size() +
		( change.kind == ChangeKind::Insert ? change.prefix.size() : 0 );
	if ( newBytes > MaxSourceBytes ) { error = "edit would exceed the 64 MiB map limit"; return false; }
	while ( history.size() > cursor ) { historyBytes -= history.back().Bytes(); history.pop_back(); }
	change.beforeRevision = revision; change.afterRevision = nextRevision++;
	Apply( change, true );
	historyBytes += change.Bytes();
	history.push_back( std::move( change ) );
	while ( history.size() > MaxHistoryEntries || historyBytes > MaxHistoryBytes ) {
		historyBytes -= history.front().Bytes(); history.erase( history.begin() );
	}
	cursor = history.size();
	return true;
}

bool MapDocument::Undo( std::string &error ) {
	error.clear();
	if ( !CanUndo() ) { error = "nothing to undo"; return false; }
	Apply( history[--cursor], false );
	return true;
}

bool MapDocument::Redo( std::string &error ) {
	error.clear();
	if ( !CanRedo() ) { error = "nothing to redo"; return false; }
	Apply( history[cursor++], true );
	return true;
}

}
