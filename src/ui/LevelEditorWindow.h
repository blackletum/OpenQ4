// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_WINDOW_H
#define OPENQ4_LEVEL_EDITOR_WINDOW_H

#include "../framework/LevelEditor.h"

// A normal GUI widget: device-context clipping/aspect and session event routing
// remain shared with the shipped UI. It owns no global window or GL context.
class idLevelEditorWindow : public idWindow {
public:
	explicit idLevelEditorWindow( idUserInterfaceLocal *gui ) : idWindow( gui ) {}
	void Draw( int time, float x, float y ) override {
		if ( !pane ) { LevelEditor_DrawPreview( dc, drawRect, time ); }
	}
	void PostParse() override;
	void Activate( bool activate, idStr &act ) override;
	const char *HandleEvent( const sysEvent_t *event, bool *updateVisuals ) override;
	const char *RouteMouseCoords( float xd, float yd ) override;
	void LoseCapture() override { dragMode = 0; idWindow::LoseCapture(); }
private:
	bool ParseInternalVar( const char *name, idParser *src ) override;
	int pane = 0, dragMode = 0;
	float lastX = 0, lastY = 0;
};

#endif
