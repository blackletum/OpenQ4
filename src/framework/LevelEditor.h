// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_H
#define OPENQ4_LEVEL_EDITOR_H

void LevelEditor_InitCommands();
void LevelEditor_Frame();
void LevelEditor_Shutdown();

namespace oq4editor { class MapWorkspace; }
class idDeviceContext;
class idRectangle;
oq4editor::MapWorkspace &LevelEditor_Workspace();
void LevelEditor_OpenWorkspace();
void LevelEditor_UIFrame();
bool LevelEditor_UIIsActive();
void LevelEditor_UIShutdown();
void LevelEditor_DrawPreview( idDeviceContext *dc, const idRectangle &rect, int time );
bool LevelEditor_UIInputEnabled();
void LevelEditor_Navigate( float yaw, float pitch, float zoom );
void LevelEditor_Pan( float x, float y );
void LevelEditor_Pick( float x, float y, float aspect );
void LevelEditor_ResizePane( int pane, float delta );
void LevelEditor_FrameSelection();

#endif
