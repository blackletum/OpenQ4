/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "DeviceContext.h"
#include "UserInterface.h"
#include "../renderer/tr_local.h"

idVec4 idDeviceContext::colorPurple;
idVec4 idDeviceContext::colorOrange;
idVec4 idDeviceContext::colorYellow;
idVec4 idDeviceContext::colorGreen;
idVec4 idDeviceContext::colorBlue;
idVec4 idDeviceContext::colorRed;
idVec4 idDeviceContext::colorBlack;
idVec4 idDeviceContext::colorWhite;
idVec4 idDeviceContext::colorNone;


idCVar gui_smallFontLimit( "gui_smallFontLimit", "0.30", CVAR_GUI | CVAR_ARCHIVE, "" );
idCVar gui_mediumFontLimit( "gui_mediumFontLimit", "0.60", CVAR_GUI | CVAR_ARCHIVE, "" );


idList<fontInfoEx_t> idDeviceContext::fonts;

namespace {

static const float Q4_GUI_FONT_BASE_POINT_SIZE = 48.0f;
static const float Q4_TEXT_BRIGHTNESS_STEP = 0.1f;
static const float Q4_TEXT_RGB_ESCAPE_SCALE = 1.0f / 9.0f;
static const float Q4_TEXT_OUTLINE_DARK_THRESHOLD = 0.2f;
static const float Q4_TEXT_STYLE_OFFSET = 1.0f;
static const float Q4_TEXT_LINE_SPACING_SCALE = 1.25f;
static const float Q4_GLYPH_HORIZONTAL_GUARD_TEXELS = 0.5f;
static const int Q4_TEXT_STYLE_SHADOW = 1;
static const int Q4_TEXT_STYLE_OUTLINE = 2;
static const int Q4_TEXT_ALIGN_VERTICAL_CENTER = 3;
static const int Q4_TEXT_CURSOR_NONE = -1;
static const int Q4_TEXT_LINE_BUFFER_SIZE = 1024;
static const int Q4_TEXT_REPEAT_ESCAPE_MAX = 9;
static const int Q4_EMBEDDED_ICON_FULL_IMAGE = -1;
static const unsigned char Q4_INSERT_CURSOR_GLYPH = '|';
static const unsigned char Q4_OVERSTRIKE_CURSOR_GLYPH = '_';
static const unsigned char Q4_EMBEDDED_ICON_REFERENCE_GLYPH = 'W';

enum q4EmbeddedIconMeasure_t {
	Q4_EMBEDDED_ICON_DRAW_WIDTH,
	Q4_EMBEDDED_ICON_REGISTERED_WIDTH
};

static int OpenQ4_TextEscapeLength( const char *text, int *type = NULL ) {
	return idStr::IsEscape( text, type );
}

static bool OpenQ4_IsRepeatTextEscape( const char *escape, int escapeLength ) {
	return escapeLength > 2 && escape != NULL && ( escape[1] == 'N' || escape[1] == 'n' );
}

static int OpenQ4_TextEscapeRepeatCount( const char *escape ) {
	int repeats = static_cast<unsigned char>( escape[2] ) - '0';
	if ( repeats < 0 ) {
		repeats = 0;
	} else if ( repeats >= Q4_TEXT_REPEAT_ESCAPE_MAX ) {
		repeats = Q4_TEXT_REPEAT_ESCAPE_MAX;
	}
	return repeats;
}

struct q4ScaledFont_t {
	const fontInfo_t *font;
	float renderScale;
	float maxWidth;
	float maxHeight;
};

struct q4VirtualScreenTransform_t {
	float xScale;
	float yScale;
	float xOffset;
	float yOffset;
};

static bool OpenQ4_ExtractIconCode( const char *escape, char code[4] ) {
	int escapeType = 0;
	if ( OpenQ4_TextEscapeLength( escape, &escapeType ) != 5 || escapeType != S_ESCAPE_ICON ) {
		return false;
	}

	code[0] = escape[2];
	code[1] = escape[3];
	code[2] = escape[4];
	code[3] = '\0';
	return true;
}

static float OpenQ4_FontRenderScale( const fontInfo_t *font, float scale ) {
	if ( font == NULL || font->pointSize == 0.0f ) {
		return 0.0f;
	}
	return scale / font->pointSize * Q4_GUI_FONT_BASE_POINT_SIZE;
}

static int OpenQ4_ScaledFontUnits( float fontScale, float units ) {
	return static_cast<int>( fontScale * units );
}

static int OpenQ4_RoundedGlyphAdvance( const glyphInfo_t *glyph ) {
	return static_cast<int>( idMath::Ceil( glyph->horiAdvance ) );
}

static int OpenQ4_GlyphAdvanceUnits( const glyphInfo_t *glyph, int adjust ) {
	return adjust + OpenQ4_RoundedGlyphAdvance( glyph );
}

static int OpenQ4_GlyphHeightUnits( const glyphInfo_t *glyph ) {
	return static_cast<int>( glyph->height );
}

static int OpenQ4_EmbeddedIconDimensionOrImageSize( int registeredDimension, float imageDimension ) {
	return registeredDimension == Q4_EMBEDDED_ICON_FULL_IMAGE ? static_cast<int>( imageDimension ) : registeredDimension;
}

static void OpenQ4_SetEmbeddedIconAxisUV( float &uv1, float &uv2, int registeredOffset, int registeredLength, float imageLength ) {
	if ( imageLength <= 0.0f ) {
		uv1 = 0.0f;
		uv2 = 0.0f;
		return;
	}

	if ( registeredOffset == Q4_EMBEDDED_ICON_FULL_IMAGE ) {
		uv1 = 0.0f;
		uv2 = 1.0f;
		return;
	}

	uv1 = static_cast<float>( registeredOffset ) / imageLength;
	uv2 = static_cast<float>( registeredOffset + registeredLength ) / imageLength;
}

static int OpenQ4_EmbeddedIconWidthUnits( float iconWidth, float iconHeight, float referenceHeight, q4EmbeddedIconMeasure_t measureMode = Q4_EMBEDDED_ICON_DRAW_WIDTH ) {
	if ( measureMode == Q4_EMBEDDED_ICON_REGISTERED_WIDTH ) {
		return static_cast<int>( iconWidth );
	}

	if ( referenceHeight <= 0.0f || iconWidth <= 0.0f || iconHeight <= 0.0f ) {
		return 0;
	}
	return static_cast<int>( iconWidth * ( referenceHeight / iconHeight ) );
}

static float OpenQ4_ScaledGlyphAdvance( float fontScale, const glyphInfo_t *glyph, float adjust ) {
	return idMath::Ceil( ( glyph->horiAdvance + adjust ) * fontScale );
}

static float OpenQ4_GlyphDrawX( float x, float fontScale, const glyphInfo_t *glyph ) {
	return x + fontScale * glyph->horiBearingX;
}

static float OpenQ4_GlyphDrawY( float y, float fontScale, const glyphInfo_t *glyph ) {
	return y - fontScale * glyph->horiBearingY;
}

static bool OpenQ4_ApplyGlyphHorizontalGuard( const glyphInfo_t *glyph, float fontScale, float &x, float &width, float &s1, float &s2 ) {
	if ( glyph == NULL || fontScale == 0.0f || width <= 0.0f || s2 <= s1 ) {
		return false;
	}

	const float atlasWidth = width / ( s2 - s1 );
	if ( atlasWidth <= 0.0f ) {
		return false;
	}

	const float leftGuard = Min( Q4_GLYPH_HORIZONTAL_GUARD_TEXELS, Max( 0.0f, s1 * atlasWidth ) );
	const float rightGuard = Min( Q4_GLYPH_HORIZONTAL_GUARD_TEXELS, Max( 0.0f, ( 1.0f - s2 ) * atlasWidth ) );
	if ( leftGuard == 0.0f && rightGuard == 0.0f ) {
		return false;
	}

	x -= leftGuard * fontScale;
	width += leftGuard + rightGuard;
	s1 -= leftGuard / atlasWidth;
	s2 += rightGuard / atlasWidth;
	return true;
}

static bool OpenQ4_HasRenderableFont( const q4ScaledFont_t &scaledFont ) {
	return scaledFont.font != NULL && scaledFont.renderScale != 0.0f;
}

static bool OpenQ4_TextCursorReached( int cursor, int count ) {
	return cursor != Q4_TEXT_CURSOR_NONE && cursor <= count;
}

static void OpenQ4_ApplyRgbTextEscapeColor( idVec4 &drawTextColor, idVec4 &currentColor, const unsigned char *payload ) {
	drawTextColor[0] = ( payload[2] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	drawTextColor[1] = ( payload[3] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	drawTextColor[2] = ( payload[4] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	currentColor = drawTextColor;
}

static bool OpenQ4_ShouldDrawEmptyTextCursor( bool calcOnly, int cursor ) {
	return !calcOnly && cursor == 0;
}

static bool OpenQ4_ShouldDrawFinalTextCursor( int cursor ) {
	return cursor == 0;
}

static bool OpenQ4_IsLineBreakChar( char c ) {
	return c == '\n' || c == '\r' || c == '\0';
}

static const char *OpenQ4_SkipPairedLineBreak( const char *text ) {
	if ( ( *text == '\n' && text[1] == '\r' ) || ( *text == '\r' && text[1] == '\n' ) ) {
		return text + 1;
	}
	return text;
}

static bool OpenQ4_ShouldCaptureBreak( bool lineBreak, bool wrap, char c ) {
	return lineBreak || ( wrap && ( c == ' ' || c == '\t' ) );
}

static float OpenQ4_InitialTextBaseline( idRectangle &rect, int &textAlign, float lineHeight ) {
	if ( textAlign == Q4_TEXT_ALIGN_VERTICAL_CENTER ) {
		textAlign = idDeviceContext::ALIGN_LEFT;
		return rect.y + rect.h * 0.5f + lineHeight * 0.5f;
	}
	return rect.y + lineHeight;
}

static float OpenQ4_AlignedTextX( const idRectangle &rect, int textAlign, int textWidth ) {
	if ( textAlign == idDeviceContext::ALIGN_RIGHT ) {
		return rect.x + rect.w - textWidth;
	}
	if ( textAlign == idDeviceContext::ALIGN_CENTER ) {
		return rect.x + ( rect.w - textWidth ) * 0.5f;
	}
	return rect.x;
}

static void OpenQ4_ClearVirtualScreenTransform( q4VirtualScreenTransform_t &transform ) {
	transform.xScale = 0.0f;
	transform.yScale = 0.0f;
	transform.xOffset = 0.0f;
	transform.yOffset = 0.0f;
}

static void OpenQ4_SetRetailVirtualTransform( float width, float height, q4VirtualScreenTransform_t &transform ) {
	OpenQ4_ClearVirtualScreenTransform( transform );

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	transform.xScale = static_cast<float>( VIRTUAL_WIDTH ) * ( 1.0f / width );
	transform.yScale = static_cast<float>( VIRTUAL_HEIGHT ) * ( 1.0f / height );
}

static bool OpenQ4_GetCurrentViewportSize( float &windowWidth, float &windowHeight ) {
	windowWidth = static_cast<float>( glConfig.uiViewportWidth );
	windowHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( glConfig.vidWidth );
		windowHeight = static_cast<float>( glConfig.vidHeight );
	}
	return windowWidth > 0.0f && windowHeight > 0.0f;
}

static void OpenQ4_CalcVirtualScreenTransform( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, q4VirtualScreenTransform_t &transform ) {
	OpenQ4_ClearVirtualScreenTransform( transform );

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	if ( !aspectCorrect || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		OpenQ4_SetRetailVirtualTransform( width, height, transform );
		return;
	}

	const float targetAspect = width / height;
	const float windowAspect = windowWidth / windowHeight;
	const float uniformPhysicalScale = ( windowAspect >= targetAspect ) ? ( windowHeight / height ) : ( windowWidth / width );
	const float drawWidth = width * uniformPhysicalScale;
	const float drawHeight = height * uniformPhysicalScale;

	const float virtualPerPhysicalX = static_cast<float>( VIRTUAL_WIDTH ) / windowWidth;
	const float virtualPerPhysicalY = static_cast<float>( VIRTUAL_HEIGHT ) / windowHeight;

	transform.xScale = uniformPhysicalScale * virtualPerPhysicalX;
	transform.yScale = uniformPhysicalScale * virtualPerPhysicalY;
	transform.xOffset = ( windowWidth - drawWidth ) * 0.5f * virtualPerPhysicalX;
	transform.yOffset = ( windowHeight - drawHeight ) * 0.5f * virtualPerPhysicalY;
}

static void OpenQ4_CalcVirtualScreenExpansion( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, float &xExpand, float &yExpand ) {
	xExpand = 0.0f;
	yExpand = 0.0f;

	if ( !aspectCorrect || width <= 0.0f || height <= 0.0f || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const float targetAspect = width / height;
	const float windowAspect = windowWidth / windowHeight;
	const float aspectEpsilon = 0.0001f;

	if ( windowAspect > targetAspect + aspectEpsilon ) {
		xExpand = ( width * ( windowAspect / targetAspect - 1.0f ) ) * 0.5f;
	} else if ( windowAspect + aspectEpsilon < targetAspect ) {
		yExpand = ( height * ( targetAspect / windowAspect - 1.0f ) ) * 0.5f;
	}
}

static float OpenQ4_ApplyVirtualX( const q4VirtualScreenTransform_t &transform, float x ) {
	return x * transform.xScale + transform.xOffset;
}

static float OpenQ4_ApplyVirtualY( const q4VirtualScreenTransform_t &transform, float y ) {
	return y * transform.yScale + transform.yOffset;
}

static bool OpenQ4_NearlyEqual( float actual, float expected, float epsilon = 0.001f ) {
	return idMath::Fabs( actual - expected ) <= epsilon;
}

static bool OpenQ4_CheckNear( const char *label, float actual, float expected, float epsilon = 0.001f ) {
	if ( OpenQ4_NearlyEqual( actual, expected, epsilon ) ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %.6f, expected %.6f", label, actual, expected );
	return false;
}

static bool OpenQ4_CheckBool( const char *label, bool actual, bool expected ) {
	if ( actual == expected ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %d, expected %d", label, actual ? 1 : 0, expected ? 1 : 0 );
	return false;
}

static bool OpenQ4_CheckInt( const char *label, int actual, int expected ) {
	if ( actual == expected ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %d, expected %d", label, actual, expected );
	return false;
}

struct q4GlyphClipCase_t {
	const char *label;
	float x;
	float y;
	float w;
	float h;
	float s1;
	float t1;
	float s2;
	float t2;
	bool clipped;
	float expectedX;
	float expectedY;
	float expectedW;
	float expectedH;
	float expectedS1;
	float expectedT1;
	float expectedS2;
	float expectedT2;
};

static bool OpenQ4_CheckGlyphClipCase( idDeviceContext &dc, const q4GlyphClipCase_t &clipCase ) {
	float x = clipCase.x;
	float y = clipCase.y;
	float w = clipCase.w;
	float h = clipCase.h;
	float s1 = clipCase.s1;
	float t1 = clipCase.t1;
	float s2 = clipCase.s2;
	float t2 = clipCase.t2;
	bool ok = true;

	const bool clipped = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= OpenQ4_CheckBool( va( "%s result", clipCase.label ), clipped, clipCase.clipped );
	ok &= OpenQ4_CheckNear( va( "%s x", clipCase.label ), x, clipCase.expectedX );
	ok &= OpenQ4_CheckNear( va( "%s y", clipCase.label ), y, clipCase.expectedY );
	ok &= OpenQ4_CheckNear( va( "%s w", clipCase.label ), w, clipCase.expectedW );
	ok &= OpenQ4_CheckNear( va( "%s h", clipCase.label ), h, clipCase.expectedH );
	ok &= OpenQ4_CheckNear( va( "%s s1", clipCase.label ), s1, clipCase.expectedS1 );
	ok &= OpenQ4_CheckNear( va( "%s t1", clipCase.label ), t1, clipCase.expectedT1 );
	ok &= OpenQ4_CheckNear( va( "%s s2", clipCase.label ), s2, clipCase.expectedS2 );
	ok &= OpenQ4_CheckNear( va( "%s t2", clipCase.label ), t2, clipCase.expectedT2 );
	return ok;
}

static void OpenQ4_SetGuiSortForFont( fontInfoEx_t &font ) {
	if ( font.fontInfoSmall.material != NULL ) {
		font.fontInfoSmall.material->SetSort( SS_GUI );
	}
	if ( font.fontInfoMedium.material != NULL ) {
		font.fontInfoMedium.material->SetSort( SS_GUI );
	}
	if ( font.fontInfoLarge.material != NULL ) {
		font.fontInfoLarge.material->SetSort( SS_GUI );
	}
}

}

int idDeviceContext::FindFont( const char *name ) {
	int c = fonts.Num();

	for (int i = 0; i < c; i++) {
		if (idStr::Icmp(name, fonts[i].name) == 0) {
			OpenQ4_SetGuiSortForFont( fonts[i] );
			return i;
		}
	}

	// If the font was not found, try to register it
	idStr fileName = name;
	if ( idStr::Icmp( fileName.c_str(), "fonts" ) == 0 ) {
		fileName = "fonts/chain";
	}
	fileName.Replace("fonts", va("fonts/%s", fontLang.c_str()) );

	fontInfoEx_t fontInfo;
	int index = fonts.Append( fontInfo );
	if ( renderSystem->RegisterFont( fileName, fonts[index] ) ) {
		idStr::Copynz( fonts[index].name, name, sizeof( fonts[index].name ) );
		return index;
	} else {
		common->Printf( "Could not register font %s [%s]\n", name, fileName.c_str() );
		return -1;
	}
}

void idDeviceContext::SetupFonts() {
	fonts.SetGranularity( 1 );

	fontLang = cvarSystem->GetCVarString( "sys_lang" );
	
	// western european languages can use the english font
	if ( fontLang == "french" || fontLang == "german" || fontLang == "spanish" || fontLang == "italian" ) {
		fontLang = "english";
	}

	// Default font has to be added first.
	FindFont( "fonts/chain" );
}

void idDeviceContext::SetFont( int num ) {
	if ( fonts.Num() == 0 ) {
		activeFont = NULL;
		return;
	}
	if ( num >= 0 && num < fonts.Num() ) {
		activeFont = &fonts[num];
	} else {
		activeFont = &fonts[0];
	}
}

void idDeviceContext::SizeIcon( embeddedIcon_t &icon ) {
	if ( icon.material == NULL ) {
		return;
	}

	const float imageWidth = static_cast<float>( icon.material->GetImageWidth() );
	const float imageHeight = static_cast<float>( icon.material->GetImageHeight() );
	if ( imageWidth <= 0.0f || imageHeight <= 0.0f ) {
		icon.width = 0.0f;
		icon.height = 0.0f;
		return;
	}

	const int x = static_cast<int>( icon.s1 );
	const int y = static_cast<int>( icon.t1 );
	const int registeredWidth = static_cast<int>( icon.width );
	const int registeredHeight = static_cast<int>( icon.height );

	OpenQ4_SetEmbeddedIconAxisUV( icon.s1, icon.s2, x, registeredWidth, imageWidth );
	OpenQ4_SetEmbeddedIconAxisUV( icon.t1, icon.t2, y, registeredHeight, imageHeight );
	icon.width = static_cast<float>( OpenQ4_EmbeddedIconDimensionOrImageSize( registeredWidth, imageWidth ) );
	icon.height = static_cast<float>( OpenQ4_EmbeddedIconDimensionOrImageSize( registeredHeight, imageHeight ) );
}

bool idDeviceContext::FindIcon( const char *code, const embeddedIcon_t **icon ) const {
	embeddedIcon_t *foundIcon = NULL;
	const bool found = icons.Get( code, &foundIcon );
	if ( icon != NULL ) {
		*icon = foundIcon;
	}
	return found && foundIcon != NULL;
}

float idDeviceContext::GetIconDisplayWidth( const embeddedIcon_t &icon, float referenceHeight ) const {
	return static_cast<float>( OpenQ4_EmbeddedIconWidthUnits( icon.width, icon.height, referenceHeight, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
}

void idDeviceContext::RegisterIcon( const char *code, const char *shader, int x, int y, int w, int h ) {
	if ( code == NULL || shader == NULL || code[0] == '\0' || shader[0] == '\0' ) {
		return;
	}

	embeddedIcon_t icon;
	idStr::Copynz( icon.code, code, sizeof( icon.code ) );
	icon.material = declManager->FindMaterial( shader );
	if ( icon.material == NULL ) {
		return;
	}

	const_cast<idMaterial *>( icon.material )->EnsureNotPurged();
	icon.material->SetSort( SS_GUI );
	icon.s1 = static_cast<float>( x );
	icon.t1 = static_cast<float>( y );
	icon.width = static_cast<float>( w );
	icon.height = static_cast<float>( h );
	SizeIcon( icon );
	icons.Set( icon.code, icon );
	idStr::RegisterIconEscapeCode( icon.code );
}

void idDeviceContext::RegisterBuiltinIcons() {
	static const struct {
		const char *code;
		const char *shader;
	} builtinIcons[] = {
		{ "vce", "gfx/guis/hud/icons/icon_speaker" },
		{ "vcd", "gfx/guis/hud/icons/icon_speaker_disabled" },
		{ "fde", "gfx/guis/hud/icons/icon_friend" },
		{ "fdd", "gfx/guis/hud/icons/icon_friend_disabled" },
		{ "flm", "gfx/guis/hud/icons/sb_flag_marine" },
		{ "fls", "gfx/guis/hud/icons/sb_flag_strogg" },
		{ "yrd", "gfx/guis/hud/icons/icon_ready" },
		{ "nrd", "gfx/guis/hud/icons/icon_notready" },
		{ "qad", "gfx/guis/hud/icons/item_quadkill_colored" },
		{ "ds0", "gfx/guis/mainmenu/icon_dedserver" },
		{ "dsp", "gfx/guis/mainmenu/icon_pb" },
		{ "sl0", "gfx/guis/mainmenu/icon_locked" },
		{ "sf0", "gfx/guis/mainmenu/icon_favorite" }
	};

	for ( int i = 0; i < static_cast<int>( sizeof( builtinIcons ) / sizeof( builtinIcons[0] ) ); ++i ) {
		RegisterIcon( builtinIcons[i].code, builtinIcons[i].shader );
	}
}


void idDeviceContext::Init() {
	xScale = 0.0;
	aspectCorrect = true;
	SetSize(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
	whiteImage = declManager->FindMaterial("gfx/guis/white");
	whiteImage->SetSort( SS_GUI );
	mbcs = false;
	SetupFonts();
	activeFont = fonts.Num() > 0 ? &fonts[0] : NULL;
	icons.Clear();
	idStr::ClearIconEscapeCodes();
	RegisterBuiltinIcons();
	colorPurple = idVec4(1, 0, 1, 1);
	colorOrange = idVec4(1, 1, 0, 1);
	colorYellow = idVec4(0, 1, 1, 1);
	colorGreen = idVec4(0, 1, 0, 1);
	colorBlue = idVec4(0, 0, 1, 1);
	colorRed = idVec4(1, 0, 0, 1);
	colorWhite = idVec4(1, 1, 1, 1);
	colorBlack = idVec4(0, 0, 0, 1);
	colorNone = idVec4(0, 0, 0, 0);
	cursorImages[CURSOR_ARROW] = declManager->FindMaterial("gfx/guis/guicursor_arrow");
	cursorImages[CURSOR_HAND] = declManager->FindMaterial("gfx/guis/guicursor_hand");
	scrollBarImages[SCROLLBAR_HBACK] = declManager->FindMaterial("gfx/guis/scrollbarh");
	scrollBarImages[SCROLLBAR_VBACK] = declManager->FindMaterial("gfx/guis/scrollbarv");
	scrollBarImages[SCROLLBAR_THUMB] = declManager->FindMaterial("gfx/guis/scrollbar_thumb");
	scrollBarImages[SCROLLBAR_RIGHT] = declManager->FindMaterial("gfx/guis/scrollbar_right");
	scrollBarImages[SCROLLBAR_LEFT] = declManager->FindMaterial("gfx/guis/scrollbar_left");
	scrollBarImages[SCROLLBAR_UP] = declManager->FindMaterial("gfx/guis/scrollbar_up");
	scrollBarImages[SCROLLBAR_DOWN] = declManager->FindMaterial("gfx/guis/scrollbar_down");
	cursorImages[CURSOR_ARROW]->SetSort( SS_GUI );
	cursorImages[CURSOR_HAND]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_HBACK]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_VBACK]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_THUMB]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_RIGHT]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_LEFT]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_UP]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_DOWN]->SetSort( SS_GUI );
	cursor = CURSOR_ARROW;
	enableClipping = true;
	overStrikeMode = true;
	drawTextColor = colorWhite;
	drawTextColorAdjust = 0.0f;
	mat.Identity();
	origin.Zero();
	initialized = true;
}

void idDeviceContext::Shutdown() {
	fontName.Clear();
	fontLang.Clear();
	clipRects.Clear();
	fonts.Clear();
	Clear();
}

void idDeviceContext::Clear() {
	initialized = false;
	useFont = NULL;
	activeFont = NULL;
	mbcs = false;
	aspectCorrect = true;
	drawTextColor.Zero();
	drawTextColorAdjust = 0.0f;
	icons.Clear();
}

idDeviceContext::idDeviceContext() {
	Clear();
}

void idDeviceContext::SetTransformInfo(const idVec3 &org, const idMat3 &m) {
	origin = org;
	mat = m;
}

void idDeviceContext::SetAspectCorrection( bool enabled ) {
	aspectCorrect = enabled;
}

// 
//  added method
void idDeviceContext::GetTransformInfo(idVec3& org, idMat3& m )
{
	m = mat;
	org = origin;
}
// 

void idDeviceContext::PopClipRect() {
	if (clipRects.Num()) {
		clipRects.RemoveIndex(clipRects.Num()-1);
	}
}

void idDeviceContext::PushClipRect(idRectangle r) {
	clipRects.Append(r);
}

void idDeviceContext::PushClipRect(float x, float y, float w, float h) {
	clipRects.Append(idRectangle(x, y, w, h));
}

bool idDeviceContext::ClippedCoords(float *x, float *y, float *w, float *h, float *s1, float *t1, float *s2, float *t2) {

	if ( enableClipping == false || clipRects.Num() == 0 ) {
		return false;
	}

	int c = clipRects.Num();
	while( --c > 0 ) {
		idRectangle *clipRect = &clipRects[c];
 
		float ox = *x;
		float oy = *y;
		float ow = *w;
		float oh = *h;

		if ( ow <= 0.0f || oh <= 0.0f ) {
			break;
		}

		if (*x < clipRect->x) {
			*w -= clipRect->x - *x;
			*x = clipRect->x;
		} else if (*x > clipRect->x + clipRect->w) {
			*x = *w = *y = *h = 0;
		}
		if (*y < clipRect->y) {
			*h -= clipRect->y - *y;
			*y = clipRect->y;
		} else if (*y > clipRect->y + clipRect->h) {
			*x = *w = *y = *h = 0;
		}
		if (*w > clipRect->w) {
			*w = clipRect->w - *x + clipRect->x;
		} else if (*x + *w > clipRect->x + clipRect->w) {
			*w = clipRect->Right() - *x;
		}
		if (*h > clipRect->h) {
			*h = clipRect->h - *y + clipRect->y;
		} else if (*y + *h > clipRect->y + clipRect->h) {
			*h = clipRect->Bottom() - *y;
		}

		if ( s1 && s2 && t1 && t2 && ow > 0.0f ) {
			float ns1, ns2, nt1, nt2;
			// upper left
			float u = ( *x - ox ) / ow;
			ns1 = *s1 * ( 1.0f - u ) + *s2 * ( u );

			// upper right
			u = ( *x + *w - ox ) / ow;
			ns2 = *s1 * ( 1.0f - u ) + *s2 * ( u );

			// lower left
			u = ( *y - oy ) / oh;
			nt1 = *t1 * ( 1.0f - u ) + *t2 * ( u );

			// lower right
			u = ( *y + *h - oy ) / oh;
			nt2 = *t1 * ( 1.0f - u ) + *t2 * ( u );

			// set values
			*s1 = ns1;
			*s2 = ns2;
			*t1 = nt1;
			*t2 = nt2;
		}
	}

	return (*w == 0 || *h == 0) ? true : false;
}


void idDeviceContext::AdjustCoords(float *x, float *y, float *w, float *h) {
	if (x) {
		*x = (*x * xScale) + xOffset;
	}
	if (y) {
		*y = (*y * yScale) + yOffset;
	}
	if (w) {
		*w *= xScale;
	}
	if (h) {
		*h *= yScale;
	}
}

static ID_INLINE void TransformVertInVirtualSpace( idDrawVert &vert, const idVec3 &origin, const idMat3 &mat, float xScale, float yScale, float xOffset, float yOffset ) {
	if ( xScale == 0.0f || yScale == 0.0f ) {
		vert.xyz -= origin;
		vert.xyz *= mat;
		vert.xyz += origin;
		return;
	}

	// UI transforms are authored in virtual GUI space, so map to virtual space,
	// apply transform, then map back to the current draw-space viewport.
	idVec3 virtualPos = vert.xyz;
	virtualPos[0] = ( virtualPos[0] - xOffset ) / xScale;
	virtualPos[1] = ( virtualPos[1] - yOffset ) / yScale;
	virtualPos -= origin;
	virtualPos *= mat;
	virtualPos += origin;
	vert.xyz[0] = ( virtualPos[0] * xScale ) + xOffset;
	vert.xyz[1] = ( virtualPos[1] * yScale ) + yOffset;
	vert.xyz[2] = virtualPos[2];
}

static ID_INLINE void OpenQ4_SetGuiDrawVert( idDrawVert &vert, float x, float y, float z, float s, float t ) {
	vert.Clear();
	vert.xyz.Set( x, y, z );
	vert.st.Set( s, t );
	vert.normal.Set( 0, 0, 1 );
	vert.tangents[0].Set( 1, 0, 0 );
	vert.tangents[1].Set( 0, 1, 0 );
	vert.color[0] = vert.color[1] = vert.color[2] = vert.color[3] = 255;
	vert.color2[0] = vert.color2[1] = vert.color2[2] = vert.color2[3] = 255;
}

void idDeviceContext::DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *shader) {
	idDrawVert verts[4];
	glIndex_t indexes[6];
	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	OpenQ4_SetGuiDrawVert( verts[0], x, y, 0.0f, s1, t1 );
	OpenQ4_SetGuiDrawVert( verts[1], x + w, y, 0.0f, s2, t1 );
	OpenQ4_SetGuiDrawVert( verts[2], x + w, y + h, 0.0f, s2, t2 );
	OpenQ4_SetGuiDrawVert( verts[3], x, y + h, 0.0f, s1, t2 );
	
	const bool hasTransform = !mat.IsIdentity();
	if ( hasTransform ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	tr.DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, hasTransform, 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) );
	
}


void idDeviceContext::DrawMaterial(float x, float y, float w, float h, const idMaterial *mat, const idVec4 &color, float scalex, float scaley) {

	renderSystem->SetColor(color);

	float	s0, s1, t0, t1;
// 
//  handle negative scales as well	
	if ( scalex < 0 )
	{
		w *= -1;
		scalex *= -1;
	}
	if ( scaley < 0 )
	{
		h *= -1;
		scaley *= -1;
	}
// 
	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1 * scalex;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1 * scalex;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1 * scaley;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1 * scaley;
	}

	if ( ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);

	DrawStretchPic( x, y, w, h, s0, t0, s1, t1, mat);
}

void idDeviceContext::DrawMaterialRotated(float x, float y, float w, float h, const idMaterial *mat, const idVec4 &color, float scalex, float scaley, float angle) {
	
	renderSystem->SetColor(color);

	float	s0, s1, t0, t1;
	// 
	//  handle negative scales as well	
	if ( scalex < 0 )
	{
		w *= -1;
		scalex *= -1;
	}
	if ( scaley < 0 )
	{
		h *= -1;
		scaley *= -1;
	}
	// 
	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1 * scalex;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1 * scalex;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1 * scaley;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1 * scaley;
	}

	if ( angle == 0.0f && ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);

	DrawStretchPicRotated( x, y, w, h, s0, t0, s1, t1, mat, angle);
}

void idDeviceContext::DrawStretchPicRotated(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *shader, float angle) {
	
	idDrawVert verts[4];
	glIndex_t indexes[6];
	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	OpenQ4_SetGuiDrawVert( verts[0], x, y, 0.0f, s1, t1 );
	OpenQ4_SetGuiDrawVert( verts[1], x + w, y, 0.0f, s2, t1 );
	OpenQ4_SetGuiDrawVert( verts[2], x + w, y + h, 0.0f, s2, t2 );
	OpenQ4_SetGuiDrawVert( verts[3], x, y + h, 0.0f, s1, t2 );

	const bool ident = !mat.IsIdentity();
	if ( ident ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	//Generate a translation so we can translate to the center of the image rotate and draw
	idVec3 origTrans;
	origTrans.x = x+(w/2);
	origTrans.y = y+(h/2);
	origTrans.z = 0;


	//Rotate the verts about the z axis before drawing them
	idMat4 rotz;
	rotz.Identity();
	float sinAng = idMath::Sin(angle);
	float cosAng = idMath::Cos(angle);
	rotz[0][0] = cosAng;
	rotz[0][1] = sinAng;
	rotz[1][0] = -sinAng;
	rotz[1][1] = cosAng;
	for(int i = 0; i < 4; i++) {
		//Translate to origin
		verts[i].xyz -= origTrans;

		//Rotate
		verts[i].xyz = rotz * verts[i].xyz;

		//Translate back
		verts[i].xyz += origTrans;
	}


	tr.DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, ident || angle != 0.0f, 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) );
}

void idDeviceContext::DrawFilledRect( float x, float y, float w, float h, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	
	if (ClippedCoords(&x, &y, &w, &h, NULL, NULL, NULL, NULL)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic( x, y, w, h, 0, 0, 0, 0, whiteImage);
}


void idDeviceContext::DrawRect( float x, float y, float w, float h, float size, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	
	if (ClippedCoords(&x, &y, &w, &h, NULL, NULL, NULL, NULL)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic( x, y, size, h, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x + w - size, y, size, h, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x, y, w, size, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x, y + h - size, w, size, 0, 0, 0, 0, whiteImage );
}

void idDeviceContext::DrawMaterialRect( float x, float y, float w, float h, float size, const idMaterial *mat, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	DrawMaterial( x, y, size, h, mat, color );
	DrawMaterial( x + w - size, y, size, h, mat, color );
	DrawMaterial( x, y, w, size, mat, color );
	DrawMaterial( x, y + h - size, w, size, mat, color );
}


void idDeviceContext::SetCursor(int n) {
	cursor = (n < CURSOR_ARROW || n >= CURSOR_COUNT) ? CURSOR_ARROW : n;
}

void idDeviceContext::DrawCursor(float *x, float *y, float size) {
	float minX = 0.0f;
	float maxX = vidWidth;
	float minY = 0.0f;
	float maxY = vidHeight;
	GetCursorBounds( minX, maxX, minY, maxY );

	*x = idMath::ClampFloat( minX, maxX, *x );
	*y = idMath::ClampFloat( minY, maxY, *y );

	renderSystem->SetColor(colorWhite);
	// Keep GUI cursor state in virtual coordinates; only transform local draw coords.
	float drawX = *x;
	float drawY = *y;
	float drawWidth = size;
	float drawHeight = size;
	// Scale dimensions independently for aspect correction while keeping the hotspot at drawX/drawY.
	AdjustCoords(&drawX, &drawY, &drawWidth, &drawHeight);
	DrawStretchPic(drawX, drawY, drawWidth, drawHeight, 0, 0, 1, 1, cursorImages[cursor]);
}
/*
 =======================================================================================================================
 =======================================================================================================================
 */

void idDeviceContext::PaintChar(float x,float y,float width,float height,float scale,float	s,float	t,float	s2,float t2,const idMaterial *hShader) {
	float	w, h;
	w = width * scale;
	h = height * scale;

	if (ClippedCoords(&x, &y, &w, &h, &s, &t, &s2, &t2)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic(x, y, w, h, s, t, s2, t2, hShader);
}

void idDeviceContext::PaintGlyph( float x, float y, float scale, const glyphInfo_t *glyph, const idMaterial *hShader ) {
	if ( glyph == NULL ) {
		return;
	}

	float width = glyph->width;
	float s = glyph->s;
	float s2 = glyph->s2;
	OpenQ4_ApplyGlyphHorizontalGuard( glyph, scale, x, width, s, s2 );
	PaintChar( x, y, width, glyph->height, scale, s, glyph->t, s2, glyph->t2, hShader );
}


void idDeviceContext::SetFontByScale(float scale) {
	if ( activeFont == NULL ) {
		useFont = NULL;
		return;
	}
	if (scale <= gui_smallFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoSmall;
		activeFont->maxHeight = activeFont->maxHeightSmall;
		activeFont->maxWidth = activeFont->maxWidthSmall;
	} else if (scale <= gui_mediumFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoMedium;
		activeFont->maxHeight = activeFont->maxHeightMedium;
		activeFont->maxWidth = activeFont->maxWidthMedium;
	} else {
		useFont = &activeFont->fontInfoLarge;
		activeFont->maxHeight = activeFont->maxHeightLarge;
		activeFont->maxWidth = activeFont->maxWidthLarge;
	}
}

int idDeviceContext::DrawText(float x, float y, float scale, idVec4 color, const char *text, float adjust, int limit, int style, int cursor, bool resetEscapes) {
	SetFontByScale( scale );
	q4ScaledFont_t scaledFont;
	scaledFont.font = useFont;
	scaledFont.renderScale = OpenQ4_FontRenderScale( useFont, scale );
	scaledFont.maxWidth = activeFont != NULL ? activeFont->maxWidth : 0.0f;
	scaledFont.maxHeight = activeFont != NULL ? activeFont->maxHeight : 0.0f;

	if ( !OpenQ4_HasRenderableFont( scaledFont ) || text == NULL || color.w == 0.0f ) {
		return 0;
	}

	if ( resetEscapes ) {
		drawTextColor = color;
		drawTextColorAdjust = 0.0f;
	}

	idVec4 currentColor = drawTextColor;
	renderSystem->SetColor( currentColor );

	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	int len = strlen( text );
	if ( limit > 0 && len > limit ) {
		len = limit;
	}

	int count = 0;
	while ( *s != '\0' && count < len ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			const unsigned char *payload = s;
			int payloadType = escapeType;
			int payloadLength = escapeLength;
			int sourceAdvance = escapeLength;
			int countAdvance = escapeLength;
			int repeats = 1;

			if ( OpenQ4_IsRepeatTextEscape( reinterpret_cast<const char *>( s ), escapeLength ) ) {
				payload = s + escapeLength;
				payloadLength = OpenQ4_TextEscapeLength( reinterpret_cast<const char *>( payload ), &payloadType );
				if ( payloadLength <= 0 ) {
					s += escapeLength;
					continue;
				}
				sourceAdvance = escapeLength + payloadLength;
				countAdvance = payloadLength;
				repeats = OpenQ4_TextEscapeRepeatCount( reinterpret_cast<const char *>( s ) );
			}

			for ( int repeatIndex = 0; repeatIndex < repeats; ++repeatIndex ) {
				if ( payloadType == S_ESCAPE_ICON ) {
					char iconCode[4];
					if ( OpenQ4_ExtractIconCode( reinterpret_cast<const char *>( payload ), iconCode ) ) {
						const embeddedIcon_t *icon = NULL;
						if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
							const glyphInfo_t *referenceGlyph = &scaledFont.font->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
							const float referenceHeight = referenceGlyph->height;
							const float iconWidth = GetIconDisplayWidth( *icon, referenceHeight );
							if ( iconWidth > 0.0f ) {
								const float iconY = OpenQ4_GlyphDrawY( y, scaledFont.renderScale, referenceGlyph );
								PaintChar( x, iconY, iconWidth, referenceHeight, scaledFont.renderScale, icon->s1, icon->t1, icon->s2, icon->t2, icon->material );
								x += iconWidth;
							}
						}
					}
				} else {
					switch ( payload[1] ) {
						case '+':
							drawTextColorAdjust += Q4_TEXT_BRIGHTNESS_STEP;
							currentColor = idVec4( drawTextColor.x + drawTextColorAdjust, drawTextColor.y + drawTextColorAdjust, drawTextColor.z + drawTextColorAdjust, color.w );
							renderSystem->SetColor( currentColor );
							break;
						case '-':
							drawTextColorAdjust -= Q4_TEXT_BRIGHTNESS_STEP;
							currentColor = idVec4( drawTextColor.x + drawTextColorAdjust, drawTextColor.y + drawTextColorAdjust, drawTextColor.z + drawTextColorAdjust, color.w );
							renderSystem->SetColor( currentColor );
							break;
						case '0':
						case 'R':
						case 'r':
							drawTextColor = color;
							drawTextColorAdjust = 0.0f;
							currentColor = color;
							renderSystem->SetColor( currentColor );
							break;
						case '1': case '2': case '3': case '4': case '5':
						case '6': case '7': case '8': case '9': case ':':
							drawTextColor = idStr::ColorForIndex( payload[1] );
							drawTextColor[3] = color[3];
							drawTextColorAdjust = 0.0f;
							currentColor = drawTextColor;
							renderSystem->SetColor( currentColor );
							break;
						case 'C':
						case 'c':
							if ( payloadLength >= 5 ) {
								OpenQ4_ApplyRgbTextEscapeColor( drawTextColor, currentColor, payload );
								drawTextColorAdjust = 0.0f;
								renderSystem->SetColor( currentColor );
							}
							break;
						default:
							break;
					}
				}
			}
			s += sourceAdvance;
			count += countAdvance;
			continue;
		}

		const glyphInfo_t *glyph = &scaledFont.font->glyphs[*s];
		const float drawX = OpenQ4_GlyphDrawX( x, scaledFont.renderScale, glyph );
		const float drawY = OpenQ4_GlyphDrawY( y, scaledFont.renderScale, glyph );

		if ( style == Q4_TEXT_STYLE_SHADOW ) {
			idVec4 shadowColor( 0.0f, 0.0f, 0.0f, currentColor[3] );
			renderSystem->SetColor( shadowColor );
			PaintGlyph( drawX + Q4_TEXT_STYLE_OFFSET, drawY + Q4_TEXT_STYLE_OFFSET, scaledFont.renderScale, glyph, scaledFont.font->material );
			renderSystem->SetColor( currentColor );
		} else if ( style == Q4_TEXT_STYLE_OUTLINE ) {
			const bool darkOutline = currentColor[0] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD || currentColor[1] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD || currentColor[2] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD;
			idVec4 outlineColor = darkOutline ? idVec4( 0.0f, 0.0f, 0.0f, currentColor[3] ) : idVec4( 1.0f, 1.0f, 1.0f, currentColor[3] );
			static const float offsets[4][2] = {
				{ Q4_TEXT_STYLE_OFFSET, Q4_TEXT_STYLE_OFFSET },
				{ -Q4_TEXT_STYLE_OFFSET, Q4_TEXT_STYLE_OFFSET },
				{ -Q4_TEXT_STYLE_OFFSET, -Q4_TEXT_STYLE_OFFSET },
				{ Q4_TEXT_STYLE_OFFSET, -Q4_TEXT_STYLE_OFFSET }
			};
			renderSystem->SetColor( outlineColor );
			for ( int i = 0; i < 4; ++i ) {
				PaintGlyph( drawX + offsets[i][0], drawY + offsets[i][1], scaledFont.renderScale, glyph, scaledFont.font->material );
			}
			renderSystem->SetColor( currentColor );
		}

		PaintGlyph( drawX, drawY, scaledFont.renderScale, glyph, scaledFont.font->material );

		if ( OpenQ4_TextCursorReached( cursor, count ) ) {
			DrawEditCursor( x, y, scale );
			cursor = Q4_TEXT_CURSOR_NONE;
		}
		x += OpenQ4_ScaledGlyphAdvance( scaledFont.renderScale, glyph, adjust );
		s++;
		count++;
	}
	if ( OpenQ4_TextCursorReached( cursor, count ) ) {
		DrawEditCursor( x, y, scale );
	}
	return count;
}

void idDeviceContext::CalcVirtualScaleOffset( float width, float height, float &outXScale, float &outYScale, float &outXOffset, float &outYOffset ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	OpenQ4_GetCurrentViewportSize( windowWidth, windowHeight );

	q4VirtualScreenTransform_t transform;
	OpenQ4_CalcVirtualScreenTransform( width, height, windowWidth, windowHeight, aspectCorrect, transform );
	outXScale = transform.xScale;
	outYScale = transform.yScale;
	outXOffset = transform.xOffset;
	outYOffset = transform.yOffset;
}

void idDeviceContext::GetVirtualScreenExpansion( float width, float height, float &xExpand, float &yExpand ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	OpenQ4_GetCurrentViewportSize( windowWidth, windowHeight );
	OpenQ4_CalcVirtualScreenExpansion( width, height, windowWidth, windowHeight, aspectCorrect, xExpand, yExpand );
}

float idDeviceContext::GetCanvasAspect() const {
	float windowWidth = static_cast<float>( glConfig.uiViewportWidth );
	float windowHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( glConfig.vidWidth );
		windowHeight = static_cast<float>( glConfig.vidHeight );
	}

	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return static_cast<float>( VIRTUAL_WIDTH ) / static_cast<float>( VIRTUAL_HEIGHT );
	}

	return windowWidth / windowHeight;
}

void idDeviceContext::SetSize(float width, float height) {
	vidWidth = ( width > 0.0f ) ? width : static_cast<float>( VIRTUAL_WIDTH );
	vidHeight = ( height > 0.0f ) ? height : static_cast<float>( VIRTUAL_HEIGHT );

	CalcVirtualScaleOffset( width, height, xScale, yScale, xOffset, yOffset );
}

void idDeviceContext::GetCursorBounds( float &minX, float &maxX, float &minY, float &maxY ) const {
	minX = 0.0f;
	maxX = vidWidth;
	minY = 0.0f;
	maxY = vidHeight;

	if ( xScale != 0.0f ) {
		minX = ( 0.0f - xOffset ) / xScale;
		maxX = ( vidWidth - xOffset ) / xScale;
		if ( minX > maxX ) {
			const float tmp = minX;
			minX = maxX;
			maxX = tmp;
		}
	}

	if ( yScale != 0.0f ) {
		minY = ( 0.0f - yOffset ) / yScale;
		maxY = ( vidHeight - yOffset ) / yScale;
		if ( minY > maxY ) {
			const float tmp = minY;
			minY = maxY;
			maxY = tmp;
		}
	}
}

int idDeviceContext::CharWidth( const char c, float scale, int adjust ) {
	SetFontByScale( scale );
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return 0;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[(const unsigned char)c];
	return OpenQ4_ScaledFontUnits( useScale, OpenQ4_GlyphAdvanceUnits( glyph, adjust ) );
}

int idDeviceContext::TextWidth( const char *text, float scale, int limit, int adjust ) {
	SetFontByScale( scale );
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	int width = 0;
	int index = 0;
	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			if ( escapeType == S_ESCAPE_ICON ) {
				char iconCode[4];
				if ( OpenQ4_ExtractIconCode( reinterpret_cast<const char *>( s ), iconCode ) ) {
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						width += OpenQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_DRAW_WIDTH );
					}
				}
			}
			s += escapeLength;
			index += escapeLength;
			continue;
		}

		width += OpenQ4_GlyphAdvanceUnits( &useFont->glyphs[*s], adjust );
		s++;
		index++;
	}
	return OpenQ4_ScaledFontUnits( useScale, width );
}

int idDeviceContext::TextHeight(const char *text, float scale, int limit, int adjust) {
	(void)adjust;

	SetFontByScale( scale );
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	int maxHeight = 0;
	int index = 0;
	const char *s = text;
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( s, &escapeType );
		if ( escapeLength > 0 ) {
			if ( escapeType == S_ESCAPE_ICON ) {
				const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
				const int referenceHeight = OpenQ4_GlyphHeightUnits( referenceGlyph );
				if ( maxHeight < referenceHeight ) {
					maxHeight = referenceHeight;
				}
			}
			s += escapeLength;
			index += escapeLength;
			continue;
		}

		const glyphInfo_t *glyph = &useFont->glyphs[*(const unsigned char *)s];
		const int glyphHeight = OpenQ4_GlyphHeightUnits( glyph );
		if ( maxHeight < glyphHeight ) {
			maxHeight = glyphHeight;
		}
		s++;
		index++;
	}

	return OpenQ4_ScaledFontUnits( useScale, maxHeight );
}

bool idDeviceContext::GetMaxTextIndex( const char *text, int limit, float textScale, wrapInfo_t &wrapInfo ) {
	SetFontByScale( textScale );
	const float useScale = OpenQ4_FontRenderScale( useFont, textScale );
	if ( text == NULL || text[0] == '\0' || useFont == NULL || useScale == 0.0f ) {
		return false;
	}

	int width = 0;
	int index = 0;
	while ( text[index] != '\0' ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( &text[index], &escapeType );
		const int tokenLength = escapeLength > 0 ? escapeLength : 1;
		int tokenWidth = 0;

		if ( escapeType == S_ESCAPE_ICON ) {
			char iconCode[4];
			if ( OpenQ4_ExtractIconCode( &text[index], iconCode ) ) {
				const embeddedIcon_t *icon = NULL;
				if ( FindIcon( iconCode, &icon ) ) {
					const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
					tokenWidth = OpenQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_REGISTERED_WIDTH );
				}
			}
		} else if ( escapeLength == 0 ) {
			tokenWidth = OpenQ4_GlyphAdvanceUnits( &useFont->glyphs[static_cast<unsigned char>( text[index] )], 0 );
		}

		width += tokenWidth;
		if ( OpenQ4_ScaledFontUnits( useScale, width ) > limit ) {
			const int lastTokenIndex = index + ( escapeLength > 0 ? escapeLength - 1 : 0 );
			wrapInfo.maxIndex = lastTokenIndex - 1;
			return true;
		}

		const int lastTokenIndex = index + tokenLength - 1;
		if ( text[lastTokenIndex] == ' ' ) {
			wrapInfo.lastWhitespace = lastTokenIndex;
		}

		index += tokenLength;
	}

	return false;
}

int idDeviceContext::MaxCharWidth(float scale) {
	SetFontByScale(scale);
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return OpenQ4_ScaledFontUnits( useScale, activeFont->maxWidth );
}

int idDeviceContext::MaxCharHeight(float scale) {
	SetFontByScale(scale);
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return OpenQ4_ScaledFontUnits( useScale, activeFont->maxHeight );
}

const idMaterial *idDeviceContext::GetScrollBarImage(int index) {
	if (index >= SCROLLBAR_HBACK && index < SCROLLBAR_COUNT) {
		return scrollBarImages[index];
	}
	return scrollBarImages[SCROLLBAR_HBACK];
}

// this only supports left aligned text
idRegion *idDeviceContext::GetTextRegion(const char *text, float textScale, idRectangle rectDraw, float xStart, float yStart) {
#if 0
	const char	*p, *textPtr, *newLinePtr;
	char		buff[1024];
	int			len, textWidth, newLine, newLineWidth;
	float		y;

	float charSkip = MaxCharWidth(textScale) + 1;
	float lineSkip = MaxCharHeight(textScale);

	textWidth = 0;
	newLinePtr = NULL;
#endif
	return NULL;
/*
	if (text == NULL) {
		return;
	}

	textPtr = text;
	if (*textPtr == '\0') {
		return;
	}

	y = lineSkip + rectDraw.y + yStart; 
	len = 0;
	buff[0] = '\0';
	newLine = 0;
	newLineWidth = 0;
	p = textPtr;

	textWidth = 0;
	while (p) {
		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\0') {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = textWidth;
		}

		if ((newLine && textWidth > rectDraw.w) || *p == '\n' || *p == '\0') {
			if (len) {

				float x = rectDraw.x ;
				
				buff[newLine] = '\0';
				DrawText(x, y, textScale, color, buff, 0, 0, 0);
				if (!wrap) {
					return;
				}
			}

			if (*p == '\0') {
				break;
			}

			y += lineSkip + 5;
			p = newLinePtr;
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			continue;
		}

		buff[len++] = *p++;
		buff[len] = '\0';
		textWidth = TextWidth( buff, textScale, -1 );
	}
*/
}

void idDeviceContext::DrawEditCursor( float x, float y, float scale ) {
	if ( (int)( com_ticNumber >> 4 ) & 1 ) {
		return;
	}
	SetFontByScale(scale);
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || useFont->material == NULL ) {
		return;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[overStrikeMode ? Q4_OVERSTRIKE_CURSOR_GLYPH : Q4_INSERT_CURSOR_GLYPH];
	PaintGlyph( x, OpenQ4_GlyphDrawY( y, useScale, glyph ), useScale, glyph, useFont->material );
}

int idDeviceContext::DrawText( const char *text, float textScale, int textAlign, idVec4 color, idRectangle rectDraw, bool wrap, int cursor, bool calcOnly, idList<int> *breaks, int limit, int adjust, int style, bool chatWindow ) {
	const float charSkip = MaxCharWidth( textScale ) + 1;
	const float lineSkip = MaxCharHeight( textScale );
	const float cursorSkip = ( cursor >= 0 ? charSkip : 0 );
	const int visibleCellCount = charSkip > 0.0f ? idMath::FtoiFast( rectDraw.w / charSkip ) : 0;

	SetFontByScale( textScale );
	const float useScale = OpenQ4_FontRenderScale( useFont, textScale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return visibleCellCount;
	}

	drawTextColor = color;
	drawTextColorAdjust = 0.0f;

	if ( breaks ) {
		breaks->Append( 0 );
	}

	if ( !( text && *text ) ) {
		if ( OpenQ4_ShouldDrawEmptyTextCursor( calcOnly, cursor ) ) {
			renderSystem->SetColor( color );
			DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
		}
		return visibleCellCount;
	}

	char buff[Q4_TEXT_LINE_BUFFER_SIZE];
	buff[0] = '\0';
	int len = 0;
	int newLine = 0;
	int newLineWidth = 0;
	float textWidth = 0.0f;
	bool lineBreak = false;
	bool wordBreak = false;
	const char *p = text;
	const char *newLinePtr = NULL;
	float y = OpenQ4_InitialTextBaseline( rectDraw, textAlign, lineSkip );
	int count = 0;

	while ( p != NULL ) {
		if ( OpenQ4_IsLineBreakChar( *p ) ) {
			lineBreak = true;
			p = OpenQ4_SkipPairedLineBreak( p );
		}

		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( p, &escapeType );
		const bool isIconEscape = escapeLength > 0 && escapeType == S_ESCAPE_ICON;
		if ( escapeLength > 0 ) {
			if ( len + escapeLength < static_cast<int>( sizeof( buff ) ) ) {
				idStr::Copynz( &buff[len], p, escapeLength + 1 );
			}
			if ( !isIconEscape ) {
				len += escapeLength;
				p += escapeLength;
				continue;
			}
		}

		int nextCharWidth = 0;
		if ( chatWindow && !lineBreak ) {
			if ( isIconEscape ) {
				char iconCode[4];
				if ( OpenQ4_ExtractIconCode( p, iconCode ) ) {
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						nextCharWidth = OpenQ4_ScaledFontUnits( useScale, OpenQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
					}
				}
			} else if ( idStr::CharIsPrintable( *p ) ) {
				nextCharWidth = CharWidth( *p, textScale, adjust );
			} else {
				nextCharWidth = static_cast<int>( cursorSkip );
			}
		}

		if ( !lineBreak && ( textWidth + nextCharWidth ) > rectDraw.w ) {
			if ( len > 0 && newLine == 0 ) {
				newLine = len;
				newLinePtr = p;
				newLineWidth = static_cast<int>( textWidth );
			}
			wordBreak = true;
		} else if ( OpenQ4_ShouldCaptureBreak( lineBreak, wrap, *p ) ) {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = static_cast<int>( textWidth );
		}

		if ( lineBreak || wordBreak ) {
			const float x = OpenQ4_AlignedTextX( rectDraw, textAlign, newLineWidth );

			if ( wrap || newLine > 0 ) {
				buff[newLine] = '\0';
				if ( wordBreak && cursor >= newLine && newLine == len ) {
					cursor++;
				}
			}

			if ( !calcOnly ) {
				count += DrawText( x, y, textScale, color, buff, static_cast<float>( adjust ), 0, style, cursor );
			}

			if ( cursor < newLine ) {
				cursor = Q4_TEXT_CURSOR_NONE;
			} else if ( cursor >= 0 ) {
				cursor -= ( newLine + 1 );
			}

			if ( !wrap ) {
				return newLine;
			}

			if ( ( limit && count > limit ) || *p == '\0' ) {
				return visibleCellCount;
			}

			y += lineSkip * Q4_TEXT_LINE_SPACING_SCALE;
			if ( !calcOnly && y > rectDraw.Bottom() ) {
				return visibleCellCount;
			}

			p = newLinePtr;
			if ( breaks ) {
				breaks->Append( p - text );
			}

			buff[0] = '\0';
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			textWidth = 0.0f;
			lineBreak = false;
			wordBreak = false;
			continue;
		}

		if ( escapeLength > 0 ) {
			len += escapeLength;
			p += escapeLength;
		} else {
			if ( len + 1 < static_cast<int>( sizeof( buff ) ) ) {
				buff[len++] = *p;
				buff[len] = '\0';
			}
			p++;
		}

		textWidth = static_cast<float>( TextWidth( buff, textScale, -1, adjust ) );
	}

	if ( OpenQ4_ShouldDrawFinalTextCursor( cursor ) ) {
		renderSystem->SetColor( color );
		DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
	}

	return visibleCellCount;
}

bool UI_FontParity_RunSelfTest( void ) {
	bool ok = true;

	fontInfo_t font = {};
	font.pointSize = 12.0f;
	ok &= OpenQ4_CheckNear( "12 point font scale", OpenQ4_FontRenderScale( &font, 0.25f ), 1.0f );
	font.pointSize = 24.0f;
	ok &= OpenQ4_CheckNear( "24 point font scale", OpenQ4_FontRenderScale( &font, 0.5f ), 1.0f );
	font.pointSize = Q4_GUI_FONT_BASE_POINT_SIZE;
	ok &= OpenQ4_CheckNear( "48 point font scale", OpenQ4_FontRenderScale( &font, 1.0f ), 1.0f );

	glyphInfo_t glyph = {};
	glyph.horiAdvance = 7.2f;
	glyph.height = 11.9f;
	glyph.horiBearingX = -1.5f;
	glyph.horiBearingY = 10.0f;
	ok &= OpenQ4_CheckNear( "rounded glyph advance", static_cast<float>( OpenQ4_RoundedGlyphAdvance( &glyph ) ), 8.0f );
	ok &= OpenQ4_CheckNear( "adjusted glyph advance units", static_cast<float>( OpenQ4_GlyphAdvanceUnits( &glyph, -1 ) ), 7.0f );
	ok &= OpenQ4_CheckNear( "glyph height units", static_cast<float>( OpenQ4_GlyphHeightUnits( &glyph ) ), 11.0f );
	ok &= OpenQ4_CheckNear( "scaled glyph advance", OpenQ4_ScaledGlyphAdvance( 1.0f, &glyph, -1.0f ), 7.0f );
	ok &= OpenQ4_CheckNear( "glyph draw x bearing", OpenQ4_GlyphDrawX( 20.0f, 2.0f, &glyph ), 17.0f );
	ok &= OpenQ4_CheckNear( "glyph draw y bearing", OpenQ4_GlyphDrawY( 30.0f, 2.0f, &glyph ), 10.0f );

	glyphInfo_t guardedGlyph = {};
	guardedGlyph.width = 8.5625f;
	guardedGlyph.s = 145.0f / 256.0f;
	guardedGlyph.s2 = 153.5625f / 256.0f;
	float guardedX = 20.0f;
	float guardedW = guardedGlyph.width;
	float guardedS1 = guardedGlyph.s;
	float guardedS2 = guardedGlyph.s2;
	ok &= OpenQ4_CheckBool( "glyph horizontal guard applied", OpenQ4_ApplyGlyphHorizontalGuard( &guardedGlyph, 0.8f, guardedX, guardedW, guardedS1, guardedS2 ), true );
	ok &= OpenQ4_CheckNear( "glyph horizontal guard x", guardedX, 19.6f );
	ok &= OpenQ4_CheckNear( "glyph horizontal guard width", guardedW, 9.5625f );
	ok &= OpenQ4_CheckNear( "glyph horizontal guard s1", guardedS1, 144.5f / 256.0f );
	ok &= OpenQ4_CheckNear( "glyph horizontal guard s2", guardedS2, 154.0625f / 256.0f );
	ok &= OpenQ4_CheckNear( "embedded icon draw width units", static_cast<float>( OpenQ4_EmbeddedIconWidthUnits( 32.0f, 16.0f, 12.0f, Q4_EMBEDDED_ICON_DRAW_WIDTH ) ), 24.0f );
	ok &= OpenQ4_CheckNear( "embedded icon registered width units", static_cast<float>( OpenQ4_EmbeddedIconWidthUnits( 32.0f, 16.0f, 12.0f, Q4_EMBEDDED_ICON_REGISTERED_WIDTH ) ), 32.0f );
	ok &= OpenQ4_CheckNear( "embedded icon full-image dimension", static_cast<float>( OpenQ4_EmbeddedIconDimensionOrImageSize( Q4_EMBEDDED_ICON_FULL_IMAGE, 64.0f ) ), 64.0f );
	ok &= OpenQ4_CheckNear( "embedded icon registered dimension", static_cast<float>( OpenQ4_EmbeddedIconDimensionOrImageSize( 24, 64.0f ) ), 24.0f );

	float iconUv1 = -1.0f;
	float iconUv2 = -1.0f;
	OpenQ4_SetEmbeddedIconAxisUV( iconUv1, iconUv2, Q4_EMBEDDED_ICON_FULL_IMAGE, 16, 64.0f );
	ok &= OpenQ4_CheckNear( "embedded icon full-image uv1", iconUv1, 0.0f );
	ok &= OpenQ4_CheckNear( "embedded icon full-image uv2", iconUv2, 1.0f );
	OpenQ4_SetEmbeddedIconAxisUV( iconUv1, iconUv2, 8, 16, 64.0f );
	ok &= OpenQ4_CheckNear( "embedded icon sprite uv1", iconUv1, 0.125f );
	ok &= OpenQ4_CheckNear( "embedded icon sprite uv2", iconUv2, 0.375f );

	idRectangle alignRect( 100.0f, 50.0f, 200.0f, 40.0f );
	ok &= OpenQ4_CheckNear( "text left align x", OpenQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_LEFT, 40 ), 100.0f );
	ok &= OpenQ4_CheckNear( "text center align x", OpenQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_CENTER, 40 ), 180.0f );
	ok &= OpenQ4_CheckNear( "text right align x", OpenQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_RIGHT, 40 ), 260.0f );

	int verticalAlign = Q4_TEXT_ALIGN_VERTICAL_CENTER;
	idRectangle baselineRect( 100.0f, 50.0f, 200.0f, 40.0f );
	ok &= OpenQ4_CheckNear( "vertical-center baseline", OpenQ4_InitialTextBaseline( baselineRect, verticalAlign, 12.0f ), 76.0f );
	ok &= OpenQ4_CheckNear( "vertical-center align reset", static_cast<float>( verticalAlign ), static_cast<float>( idDeviceContext::ALIGN_LEFT ) );
	ok &= OpenQ4_CheckBool( "empty calcOnly cursor draw", OpenQ4_ShouldDrawEmptyTextCursor( true, 0 ), false );
	ok &= OpenQ4_CheckBool( "final calcOnly cursor draw", OpenQ4_ShouldDrawFinalTextCursor( 0 ), true );
	ok &= OpenQ4_CheckBool( "final hidden cursor draw", OpenQ4_ShouldDrawFinalTextCursor( Q4_TEXT_CURSOR_NONE ), false );

	idVec4 rgbEscapeDrawColor( 0.25f, 0.5f, 0.75f, 0.42f );
	idVec4 rgbEscapeCurrentColor( 1.0f, 1.0f, 1.0f, 0.9f );
	const unsigned char rgbEscape[] = { '^', 'c', '9', '4', '1', '\0' };
	OpenQ4_ApplyRgbTextEscapeColor( rgbEscapeDrawColor, rgbEscapeCurrentColor, rgbEscape );
	ok &= OpenQ4_CheckNear( "rgb escape red", rgbEscapeCurrentColor[0], 1.0f );
	ok &= OpenQ4_CheckNear( "rgb escape green", rgbEscapeCurrentColor[1], 4.0f / 9.0f );
	ok &= OpenQ4_CheckNear( "rgb escape blue", rgbEscapeCurrentColor[2], 1.0f / 9.0f );
	ok &= OpenQ4_CheckNear( "rgb escape preserves alpha", rgbEscapeCurrentColor[3], 0.42f );
	ok &= OpenQ4_CheckNear( "rgb escape draw color alpha", rgbEscapeDrawColor[3], 0.42f );

	idDeviceContext radioFontDc;
	radioFontDc.Init();
	const int radioFont = radioFontDc.FindFont( "fonts/marine" );
	ok &= OpenQ4_CheckBool( "hud radio marine font registered", radioFont >= 0, true );
	if ( radioFont >= 0 ) {
		radioFontDc.SetFont( radioFont );
		const float radioFontScale = 0.2f / 12.0f * Q4_GUI_FONT_BASE_POINT_SIZE;
		const float incomingGlyphBearingX = 1.140625f;
		const float incomingGlyphBearingY = 7.078125f;
		const float incomingGlyphWidth = 6.484375f;
		const float incomingGlyphHeight = 7.078125f;
		glyphInfo_t incomingGlyph = {};
		incomingGlyph.width = incomingGlyphWidth;
		incomingGlyph.height = incomingGlyphHeight;
		incomingGlyph.horiBearingY = incomingGlyphBearingY;
		incomingGlyph.s = 20.0f / 256.0f;
		incomingGlyph.t = 65.0f / 128.0f;
		incomingGlyph.s2 = 26.484375f / 256.0f;
		incomingGlyph.t2 = 72.078125f / 128.0f;
		idRectangle radioIncomingTextRect( 545.0f + 2.0f, 6.0f + 2.0f, 81.0f - 2.0f, 12.0f - 2.0f );
		idRectangle radioTransmissionTextRect( 545.0f + 2.0f, 13.0f + 2.0f, 81.0f - 2.0f, 12.0f - 2.0f );
		int radioIncomingTextAlign = idDeviceContext::ALIGN_LEFT;
		int radioTransmissionTextAlign = idDeviceContext::ALIGN_LEFT;
		const float radioLineHeight = static_cast<float>( radioFontDc.MaxCharHeight( 0.2f ) );
		const float radioIncomingBaseline = OpenQ4_InitialTextBaseline( radioIncomingTextRect, radioIncomingTextAlign, radioLineHeight );
		const float radioTransmissionBaseline = OpenQ4_InitialTextBaseline( radioTransmissionTextRect, radioTransmissionTextAlign, radioLineHeight );
		const float incomingGlyphX = radioIncomingTextRect.x + radioFontScale * incomingGlyphBearingX;
		const float incomingGlyphY = OpenQ4_GlyphDrawY( radioIncomingBaseline, radioFontScale, &incomingGlyph );
		const float transmissionGlyphX = radioTransmissionTextRect.x + radioFontScale * incomingGlyphBearingX;
		const float transmissionGlyphY = OpenQ4_GlyphDrawY( radioTransmissionBaseline, radioFontScale, &incomingGlyph );
		const float incomingGlyphH = incomingGlyphHeight * radioFontScale;
		float guardedIncomingGlyphX = incomingGlyphX;
		float guardedIncomingGlyphWidth = incomingGlyphWidth;
		float guardedIncomingGlyphS1 = incomingGlyph.s;
		float guardedIncomingGlyphS2 = incomingGlyph.s2;
		ok &= OpenQ4_CheckBool( "hud radio glyph horizontal guard applied", OpenQ4_ApplyGlyphHorizontalGuard( &incomingGlyph, radioFontScale, guardedIncomingGlyphX, guardedIncomingGlyphWidth, guardedIncomingGlyphS1, guardedIncomingGlyphS2 ), true );
		const float guardedIncomingGlyphW = guardedIncomingGlyphWidth * radioFontScale;

		ok &= OpenQ4_CheckNear( "hud radio marine scale", radioFontScale, 0.8f );
		ok &= OpenQ4_CheckNear( "hud radio marine line height", radioLineHeight, 11.0f );
		ok &= OpenQ4_CheckNear( "hud radio incoming width", static_cast<float>( radioFontDc.TextWidth( "incoming", 0.2f, 0, 0 ) ), 54.0f );
		ok &= OpenQ4_CheckNear( "hud radio transmission width", static_cast<float>( radioFontDc.TextWidth( "transmission", 0.2f, 0, 0 ) ), 79.0f );
		ok &= OpenQ4_CheckNear( "hud radio incoming text height", static_cast<float>( radioFontDc.TextHeight( "incoming", 0.2f, 0, 0 ) ), 5.0f );
		ok &= OpenQ4_CheckNear( "hud radio incoming baseline", radioIncomingBaseline, 19.0f );
		ok &= OpenQ4_CheckNear( "hud radio transmission baseline", radioTransmissionBaseline, 26.0f );
		ok &= OpenQ4_CheckNear( "hud radio incoming glyph y", incomingGlyphY, 13.3375f );
		ok &= OpenQ4_CheckNear( "hud radio incoming glyph bottom", incomingGlyphY + incomingGlyphH, 19.0f );
		ok &= OpenQ4_CheckNear( "hud radio incoming unclipped overhang", incomingGlyphY + incomingGlyphH - radioIncomingTextRect.Bottom(), 1.0f );
		ok &= OpenQ4_CheckNear( "hud radio transmission glyph y", transmissionGlyphY, 20.3375f );
		ok &= OpenQ4_CheckNear( "hud radio transmission glyph bottom", transmissionGlyphY + incomingGlyphH, 26.0f );
		ok &= OpenQ4_CheckNear( "hud radio transmission unclipped overhang", transmissionGlyphY + incomingGlyphH - radioTransmissionTextRect.Bottom(), 1.0f );
		ok &= OpenQ4_CheckNear( "hud radio guarded glyph x", guardedIncomingGlyphX, 547.5125f );
		ok &= OpenQ4_CheckNear( "hud radio guarded glyph width", guardedIncomingGlyphW, 5.9875f );
		ok &= OpenQ4_CheckNear( "hud radio guarded glyph s1", guardedIncomingGlyphS1, 19.5f / 256.0f );
		ok &= OpenQ4_CheckNear( "hud radio guarded glyph s2", guardedIncomingGlyphS2, 26.984375f / 256.0f );

		idDeviceContext radioClipDc;
		radioClipDc.EnableClipping( true );
		radioClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
		radioClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
		ok &= OpenQ4_CheckGlyphClipCase( radioClipDc, {
			"hud radio incoming parent clip",
			guardedIncomingGlyphX, incomingGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2,
			false,
			guardedIncomingGlyphX, incomingGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2
		} );
		ok &= OpenQ4_CheckGlyphClipCase( radioClipDc, {
			"hud radio transmission parent clip",
			transmissionGlyphX - ( incomingGlyphX - guardedIncomingGlyphX ), transmissionGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2,
			false,
			transmissionGlyphX - ( incomingGlyphX - guardedIncomingGlyphX ), transmissionGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2
		} );
	}

	idStr fontAtlasLang = cvarSystem->GetCVarString( "sys_lang" );
	if ( fontAtlasLang == "french" || fontAtlasLang == "german" || fontAtlasLang == "spanish" || fontAtlasLang == "italian" ) {
		fontAtlasLang = "english";
	}
	const idMaterial *fontAtlasMaterial = declManager->FindMaterial( va( "fonts/%s/marine_12.fontdat", fontAtlasLang.c_str() ), false );
	ok &= OpenQ4_CheckBool( "hud radio marine atlas material", fontAtlasMaterial != NULL, true );
	if ( fontAtlasMaterial != NULL && fontAtlasMaterial->GetNumStages() > 0 ) {
		const shaderStage_t *fontAtlasStage = fontAtlasMaterial->GetStage( 0 );
		idImage *fontAtlasImage = fontAtlasStage->texture.image;
		ok &= OpenQ4_CheckBool( "hud radio marine atlas image", fontAtlasImage != NULL, true );
		if ( fontAtlasImage != NULL ) {
			fontAtlasImage->Bind();
			const idImageOpts &fontAtlasOpts = fontAtlasImage->GetOpts();
			ok &= OpenQ4_CheckInt( "hud radio marine atlas format", static_cast<int>( fontAtlasOpts.format ), static_cast<int>( FMT_DXT1 ) );
			ok &= OpenQ4_CheckInt( "hud radio marine atlas color format", static_cast<int>( fontAtlasOpts.colorFormat ), static_cast<int>( CFM_GREEN_ALPHA ) );
			ok &= OpenQ4_CheckInt( "hud radio marine atlas mip levels", fontAtlasOpts.numLevels, 4 );
		}
	}

	q4VirtualScreenTransform_t transform;
	OpenQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 640.0f, 480.0f, true, transform );
	ok &= OpenQ4_CheckNear( "4:3 x scale", transform.xScale, 1.0f );
	ok &= OpenQ4_CheckNear( "4:3 y scale", transform.yScale, 1.0f );
	ok &= OpenQ4_CheckNear( "4:3 x offset", transform.xOffset, 0.0f );
	ok &= OpenQ4_CheckNear( "4:3 y offset", transform.yOffset, 0.0f );

	OpenQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 1920.0f, 1080.0f, true, transform );
	ok &= OpenQ4_CheckNear( "wide x scale", transform.xScale, 0.75f );
	ok &= OpenQ4_CheckNear( "wide y scale", transform.yScale, 1.0f );
	ok &= OpenQ4_CheckNear( "wide x offset", transform.xOffset, 80.0f );
	ok &= OpenQ4_CheckNear( "wide y offset", transform.yOffset, 0.0f );

	float wideExpand = 0.0f;
	float tallExpand = 0.0f;
	OpenQ4_CalcVirtualScreenExpansion( 640.0f, 480.0f, 1920.0f, 1080.0f, true, wideExpand, tallExpand );
	ok &= OpenQ4_CheckNear( "wide expansion x", wideExpand, 106.666664f );
	ok &= OpenQ4_CheckNear( "wide expansion y", tallExpand, 0.0f );
	ok &= OpenQ4_CheckNear( "wide expanded left edge", OpenQ4_ApplyVirtualX( transform, -wideExpand ), 0.0f );
	ok &= OpenQ4_CheckNear( "wide authored left edge", OpenQ4_ApplyVirtualX( transform, 0.0f ), 80.0f );
	ok &= OpenQ4_CheckNear( "wide authored right edge", OpenQ4_ApplyVirtualX( transform, 640.0f ), 560.0f );
	ok &= OpenQ4_CheckNear( "wide expanded right edge", OpenQ4_ApplyVirtualX( transform, 640.0f + wideExpand ), 640.0f );

	OpenQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 1080.0f, 1920.0f, true, transform );
	ok &= OpenQ4_CheckNear( "tall x scale", transform.xScale, 1.0f );
	ok &= OpenQ4_CheckNear( "tall y scale", transform.yScale, 0.421875f );
	ok &= OpenQ4_CheckNear( "tall x offset", transform.xOffset, 0.0f );
	ok &= OpenQ4_CheckNear( "tall y offset", transform.yOffset, 138.75f );

	float xExpand = 0.0f;
	float yExpand = 0.0f;
	OpenQ4_CalcVirtualScreenExpansion( 640.0f, 480.0f, 1080.0f, 1920.0f, true, xExpand, yExpand );
	ok &= OpenQ4_CheckNear( "tall expansion x", xExpand, 0.0f );
	ok &= OpenQ4_CheckNear( "tall expansion y", yExpand, 328.888885f );
	ok &= OpenQ4_CheckNear( "tall expanded top edge", OpenQ4_ApplyVirtualY( transform, -yExpand ), 0.0f );
	ok &= OpenQ4_CheckNear( "tall expanded bottom edge", OpenQ4_ApplyVirtualY( transform, 480.0f + yExpand ), 480.0f );

	OpenQ4_CalcVirtualScreenTransform( 320.0f, 240.0f, 1920.0f, 1080.0f, false, transform );
	ok &= OpenQ4_CheckNear( "retail x scale without aspect correction", transform.xScale, 2.0f );
	ok &= OpenQ4_CheckNear( "retail y scale without aspect correction", transform.yScale, 2.0f );
	ok &= OpenQ4_CheckNear( "retail x offset without aspect correction", transform.xOffset, 0.0f );
	ok &= OpenQ4_CheckNear( "retail y offset without aspect correction", transform.yOffset, 0.0f );

	idDeviceContext baseClipDc;
	baseClipDc.EnableClipping( true );
	baseClipDc.PushClipRect( idRectangle( 100.0f, 100.0f, 50.0f, 40.0f ) );
	ok &= OpenQ4_CheckGlyphClipCase( baseClipDc, {
		"retail base clip skip",
		90.0f, 110.0f, 20.0f, 10.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		false,
		90.0f, 110.0f, 20.0f, 10.0f,
		0.0f, 0.0f, 1.0f, 1.0f
	} );

	idDeviceContext glyphClipDc;
	glyphClipDc.EnableClipping( true );
	glyphClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
	glyphClipDc.PushClipRect( idRectangle( 100.0f, 100.0f, 50.0f, 40.0f ) );
	const q4GlyphClipCase_t glyphClipCases[] = {
		{
			"glyph partial left clip",
			90.0f, 110.0f, 20.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			100.0f, 110.0f, 10.0f, 10.0f,
			0.5f, 0.0f, 1.0f, 1.0f
		},
		{
			"glyph partial right clip",
			140.0f, 110.0f, 20.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			140.0f, 110.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 0.5f, 1.0f
		},
		{
			"glyph partial top clip",
			110.0f, 90.0f, 10.0f, 20.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			110.0f, 100.0f, 10.0f, 10.0f,
			0.0f, 0.5f, 1.0f, 1.0f
		},
		{
			"glyph partial bottom clip",
			110.0f, 130.0f, 10.0f, 20.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			110.0f, 130.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 0.5f
		},
		{
			"glyph exact right edge cull",
			150.0f, 110.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			true,
			150.0f, 110.0f, 0.0f, 10.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		},
		{
			"glyph exact bottom edge cull",
			110.0f, 140.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			true,
			110.0f, 140.0f, 10.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
		}
	};
	for ( int glyphClipCaseIndex = 0; glyphClipCaseIndex < static_cast<int>( sizeof( glyphClipCases ) / sizeof( glyphClipCases[0] ) ); ++glyphClipCaseIndex ) {
		ok &= OpenQ4_CheckGlyphClipCase( glyphClipDc, glyphClipCases[glyphClipCaseIndex] );
	}

	idDeviceContext dc;
	dc.EnableClipping( true );
	dc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
	dc.PushClipRect( idRectangle( -wideExpand, 0.0f, static_cast<float>( VIRTUAL_WIDTH ) + 2.0f * wideExpand, static_cast<float>( VIRTUAL_HEIGHT ) ) );

	float x = -110.0f;
	float y = 10.0f;
	float w = 10.0f;
	float h = 20.0f;
	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = 1.0f;
	float t2 = 1.0f;
	const bool partialClippedOut = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= OpenQ4_CheckBool( "expanded partial clip result", partialClippedOut, false );
	ok &= OpenQ4_CheckNear( "expanded partial clip x", x, -wideExpand );
	ok &= OpenQ4_CheckNear( "expanded partial clip width", w, wideExpand - 100.0f );
	ok &= OpenQ4_CheckNear( "expanded partial clip s1", s1, ( 110.0f - wideExpand ) * 0.1f );
	ok &= OpenQ4_CheckNear( "expanded partial clip s2", s2, 1.0f );
	ok &= OpenQ4_CheckNear( "expanded partial clip t1", t1, 0.0f );
	ok &= OpenQ4_CheckNear( "expanded partial clip t2", t2, 1.0f );

	x = -100.0f;
	y = 10.0f;
	w = 20.0f;
	h = 20.0f;
	s1 = 0.0f;
	t1 = 0.0f;
	s2 = 1.0f;
	t2 = 1.0f;
	const bool insideClippedOut = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= OpenQ4_CheckBool( "expanded inside clip result", insideClippedOut, false );
	ok &= OpenQ4_CheckNear( "expanded inside clip x", x, -100.0f );
	ok &= OpenQ4_CheckNear( "expanded inside clip width", w, 20.0f );
	ok &= OpenQ4_CheckNear( "expanded inside clip s1", s1, 0.0f );
	ok &= OpenQ4_CheckNear( "expanded inside clip s2", s2, 1.0f );

	if ( ok ) {
		common->Printf( "uiFontParitySelfTest passed: retail glyph metrics, atlas upload, icon sizing, cursor handling, alignment, aspect expansion, and clipping are stable\n" );
	}
	return ok;
}

/*
=============
idRectangle::String
=============
*/
char *idRectangle::String( void ) const {
	static	int		index = 0;
	static	char	str[ 8 ][ 48 ];
	char	*s;

	// use an array so that multiple toString's won't collide
	s = str[ index ];
	index = (index + 1)&7;

	sprintf( s, "%.2f %.2f %.2f %.2f", x, y, w, h );

	return s;
}
