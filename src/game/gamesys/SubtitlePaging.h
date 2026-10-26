// openQ4 additions to the SDK game sources; see src/game/LICENSE.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The caller measures actual font advances and decodes UTF-8/legacy text.
// Pages contain at most two lines. Timing is proportional to text coverage,
// not word alignment: retail lipSync declarations do not supply word times.
namespace OpenQ4Subtitles {
// Cinematic letterboxes occupy y=0..60 and 420..480 in the centred 640x480
// artwork below 4:3, and captions keep to the lower third of that picture. A
// narrower screen (portrait) has room under the artwork, which spans 360 +/-
// 270 * aspect of the 720-high canvas. That picture is too short for enlarged
// captions, which then covered the title text cinematics draw in it, so when
// two lines of the given pitch fit there captions hang from the artwork's
// lower edge instead. The panel is 12 + pitch * lines high; deciding on two
// lines keeps the position still while a caption's line count changes.
inline float CanvasBottom(float aspect, bool cinematic, float pitch = 0.0f, int lines = 1) {
	if (!cinematic) return 570.0f;
	if (!(aspect <= 4.0f / 3.0f)) return 640.0f;
	const float below = 360.0f + 270.0f * aspect + 12.0f;
	if (std::isfinite(pitch) && pitch > 0.0f && below + 12.0f + 2.0f * pitch <= 708.0f)
		return below + 12.0f + pitch * std::clamp(lines, 1, 2);
	return 348.0f + 202.5f * aspect;
}
struct Page { std::string lines[2]; int end = 0; };
template<class Glyph> std::vector<Page> Paginate(const char* text, float width, int rows, Glyph glyph) {
	std::vector<Page> pages;
	if (!text || !std::isfinite(width) || width <= 0) return pages;
	std::size_t length = 0;
	while (length < 4096 && text[length]) ++length;
	std::string source(text, length);
	rows = std::clamp(rows, 1, 2);
	std::size_t pos = 0;
	while (pos < source.size() && pages.size() < 256) {
		Page page;
		for (int row = 0; row < rows && pos < source.size(); ++row) {
			while (pos < source.size() && static_cast<unsigned char>(source[pos]) <= ' ') ++pos;
			const auto begin = pos;
			auto lastSpace = pos;
			float used = 0;
			while (pos < source.size()) {
				if (source[pos] == '\n' || source[pos] == '\r') break;
				float advance = 0;
				int bytes = glyph(source.c_str() + pos, static_cast<int>(source.size() - pos), advance);
				bytes = std::clamp(bytes, 1, static_cast<int>(source.size() - pos));
				advance = std::isfinite(advance) ? (std::max)(0.0f, advance) : width;
				if (used + advance > width && pos > begin) {
					if (lastSpace > begin && source[pos] != ' ' && source[pos] != '\t') pos = lastSpace;
					break;
				}
				if (source[pos] == ' ' || source[pos] == '\t') lastSpace = pos;
				used += advance;
				pos += bytes;
			}
			auto end = pos;
			while (end > begin && static_cast<unsigned char>(source[end - 1]) <= ' ') --end;
			page.lines[row] = source.substr(begin, end - begin);
		}
		page.end = static_cast<int>(pos);
		if (!page.lines[0].empty() || !page.lines[1].empty()) pages.push_back(page);
	}
	return pages;
}
inline int PageAt(const std::vector<Page>& pages, int time, int duration) {
	if (pages.empty() || duration <= 0) return -1;
	const long long offset = static_cast<long long>(std::clamp(time, 0, duration - 1)) * pages.back().end / duration;
	for (std::size_t i = 0; i < pages.size(); ++i) if (offset < pages[i].end) return static_cast<int>(i);
	return static_cast<int>(pages.size() - 1);
}
}
