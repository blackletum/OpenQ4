// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "LevelEditorWindow.h"

void idLevelEditorWindow::PostParse() {
	idWindow::PostParse();
	flags |= WIN_CANFOCUS;
	if ( pane ) { cursor = idDeviceContext::CURSOR_HAND; }
}

void idLevelEditorWindow::Activate( bool activate, idStr &act ) {
	// ESC can switch menus during a drag, before this GUI gets the button-up.
	// Reopening must not resume that stale drag or swallow subsequent hover.
	if ( GetCaptureChild() == this ) { gui->GetDesktop()->SetCapture( nullptr ); }
	dragMode = 0;
	idWindow::Activate( activate, act );
}

bool idLevelEditorWindow::ParseInternalVar( const char *name, idParser *src ) {
	if ( !idStr::Icmp( name, "splitter" ) ) { pane = idMath::ClampInt( 0, 2, src->ParseInt() ); return true; }
	return idWindow::ParseInternalVar( name, src );
}

const char *idLevelEditorWindow::HandleEvent( const sysEvent_t *event, bool *updateVisuals ) {
	if ( !LevelEditor_UIInputEnabled() || event->evType != SE_KEY || !event->evValue2 ) { return ""; }
	const int key = event->evValue;
	if ( key == 'f' && !pane ) { LevelEditor_FrameSelection(); }
	else if ( Contains( drawRect, gui->CursorX(), gui->CursorY() ) ) {
		if ( key == K_MOUSE1 && !pane && drawRect.w > 0 && drawRect.h > 0 ) {
			LevelEditor_Pick( ( gui->CursorX() - actualX ) / drawRect.w, ( gui->CursorY() - actualY ) / drawRect.h, drawRect.w / drawRect.h );
		} else if ( ( pane && key == K_MOUSE1 ) || ( !pane && key == K_MOUSE2 ) ) {
			dragMode = pane ? 1 : idKeyInput::IsDown( K_SHIFT ) ? 3 : 2;
			lastX = gui->CursorX(); lastY = gui->CursorY(); SetCapture( this );
		} else if ( !pane && ( key == K_MWHEELUP || key == K_MWHEELDOWN ) ) {
			LevelEditor_Navigate( 0, 0, key == K_MWHEELUP ? 0.8f : 1.25f );
		}
	}
	if ( updateVisuals ) { *updateVisuals = true; }
	return "";
}

const char *idLevelEditorWindow::RouteMouseCoords( float xd, float yd ) {
	if ( !( flags & WIN_CAPTURE ) || !dragMode || !LevelEditor_UIInputEnabled() ) { return ""; }
	const float dx = gui->CursorX() - lastX, dy = gui->CursorY() - lastY;
	lastX = gui->CursorX(); lastY = gui->CursorY();
	if ( pane ) { LevelEditor_ResizePane( pane, dx ); }
	else if ( dragMode == 2 ) { LevelEditor_Navigate( -dx * 0.6f, -dy * 0.6f, 1 ); }
	else if ( drawRect.w > 0 ) { LevelEditor_Pan( dx / drawRect.w, dy / drawRect.w ); }
	return "";
}
