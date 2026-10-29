// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "retained/Hud.h"

// A presentation snapshot of the game's evaluated HUD. Scripts, input, saves
// and named events remain owned by their existing GUI; no device is polled.
class idRetainedHud {
public:
	idRetainedHud();
	~idRetainedHud();
	static bool Eligible(const char* source);
	// Classic stays on the centered stock canvas, without edge expansion.
	static bool UsesClassicCanvas(const char* source);
	static void Precache();
	bool Draw(const char* source, const std::vector<openq4::ui::HudPrimitive>& items);
	void Reset();
	bool DrawSucceeded() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

class idUserInterface;
class idUserInterfaceLocal;

// The Remastered layout of a stock HUD GUI (visual specification 14.4-14.9).
// The stock GUI keeps its scripts, saves, named events and Classic drawing.
// This peer receives the same state and events, and draws the generated
// document in place of the stock layout; any failure leaves the stock one.
class idRemasteredHud {
public:
	explicit idRemasteredHud(const char* document);
	~idRemasteredHud();
	// The Remastered document for a stock source, or null when it has none.
	static const char* DocumentFor(const char* source);
	// A small vector instrument drawn after Classic, without replacing it.
	static const char* AirDocumentFor(const char* source);
	// The layout setting and the retained gate both select Remastered.
	static bool Enabled();
	bool Draw(const idUserInterfaceLocal& legacy, int time);
	// Load the document now, as the stock HUD loads inside a level load, so
	// the first gameplay frame draws it without loading it then. A save
	// restore leaves the load to the first draw, outside its serialized stream.
	void Preload(const char* source);
	void ForwardState(const char* name, const char* value);
	void ForwardDelete(const char* name);
	void ForwardEvent(const char* name);
	// ui_hudProfile: the stock windows' unseen evaluation this frame.
	void ProfileUnseen(double milliseconds);
	bool Failed() const;
	const char* Document() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
