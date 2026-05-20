#include "framework/font.h"
#include "framework/data.h"
#include "framework/framework.h"
#include "framework/image.h"
#include "framework/options.h"
#include "library/sp.h"

#if ENABLE_FREETYPE
#include <algorithm>
#include <ft2build.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#define SHADOW_OPACITY 0.8f
#define FT_OUTLINE_SIZE (0.8 * 64)
#define RENDER_MODE FT_RENDER_MODE_LIGHT
#endif

namespace OpenApoc
{

BitmapFont::~BitmapFont() = default;

#if ENABLE_FREETYPE
struct FontSystem
{
  public:
	FT_Library library;
	FT_Face face;
	FT_Render_Mode render_mode = RENDER_MODE;
	FT_UInt font_size;
	std::vector<uint8_t> shadow_opacity_map;
	std::unordered_map<char32_t, int> charWidthCache;
	bool valid = false;
	struct
	{
		UString fontName;
		int fontBigSize;
		int fontSmallSize;
		int fontSmallsetSize;
		int fontShadowRadius;
		int fontCharInterval;
		int fontBigWidth;
		int fontSmallWidth;
		int fontSmallsetWidth;

	} cfg;

	void Open()
	{
		LogInfo("[FreeType] Initializing FreeType...");
		if (FT_Init_FreeType(&library))
		{
			LogError("[FreeType] FT_Init_FreeType failed");
			return;
		}
		LogInfo("[FreeType] FT_Init_FreeType succeeded");

		cfg = {Options::fontName.get(),         Options::fontBigSize.get(),
		       Options::fontSmallSize.get(),    Options::fontSmallsetSize.get(),
		       Options::fontShadowRadius.get(), Options::fontCharInterval.get(),
		       Options::fontBigWidth.get(),     Options::fontSmallWidth.get(),
		       Options::fontSmallsetWidth.get()};
		LogInfo("[FreeType] Loading font: %s", cfg.fontName);

		if (FT_New_Face(library, cfg.fontName.c_str(), 0, &face))
		{
			LogError("[FreeType] FT_New_Face failed for font: %s", cfg.fontName);
			FT_Done_FreeType(library);
			return;
		}
		LogInfo("[FreeType] Font loaded successfully: %s", cfg.fontName);
		GenerateShadowMap();
		valid = true;
	}

	void Close()
	{
		if (valid)
		{
			OpenApoc::config().save();
			if (face)
				FT_Done_Face(face);
			if (library)
				FT_Done_FreeType(library);
			valid = false;
		}
	}
	~FontSystem() { Close(); }

  private:
	void GenerateShadowMap()
	{
		const int radius = cfg.fontShadowRadius;
		const int size = 2 * radius + 1;
		shadow_opacity_map.resize(size * size);

		const int radius_sq = radius * radius;
		const float inv_radius = 1.0f / radius;
		const float opacity_factor = SHADOW_OPACITY * 255.0f;

		const int center = radius;
		for (int dy = 0; dy <= radius; ++dy)
		{
			const int dy_sq = dy * dy;
			for (int dx = 0; dx <= radius; ++dx)
			{
				const int dist_sq = dx * dx + dy_sq;

				if (dist_sq > radius_sq)
				{
					shadow_opacity_map[(center + dy) * size + (center + dx)] = 0;
					shadow_opacity_map[(center + dy) * size + (center - dx)] = 0;
					shadow_opacity_map[(center - dy) * size + (center + dx)] = 0;
					shadow_opacity_map[(center - dy) * size + (center - dx)] = 0;
					continue;
				}

				const float distance = std::sqrt(static_cast<float>(dist_sq));
				const uint8_t val =
				    static_cast<uint8_t>(opacity_factor * (1.0f - distance * inv_radius));

				shadow_opacity_map[(center + dy) * size + (center + dx)] = val;
				shadow_opacity_map[(center + dy) * size + (center - dx)] = val;
				shadow_opacity_map[(center - dy) * size + (center + dx)] = val;
				shadow_opacity_map[(center - dy) * size + (center - dx)] = val;
			}
		}
	}
};

static FontSystem fs;

void FontInit()
{
	fs.Open();
	if (fs.valid)
	{
		LogInfo("FreeType font initialized: %s", fs.cfg.fontName);
	}
	else
	{
		LogWarning("FreeType font initialization FAILED");
	}
}

void FontDeinit() { fs.Close(); }

const UString fontNameBig = "bigfont";
const UString fontNameSmallSet = "smallset";
void FT_SetFontSize(UString fontName)
{
	FT_UInt tagSize = fs.font_size;
	FT_UInt w;
	if (fontName == fontNameBig)
	{
		tagSize = fs.cfg.fontBigSize;
		w = fs.cfg.fontBigWidth;
	}
	else if (fontName == fontNameSmallSet)
	{
		tagSize = fs.cfg.fontSmallsetSize;
		w = fs.cfg.fontSmallsetWidth;
	}
	else
	{
		tagSize = fs.cfg.fontSmallSize;
		w = fs.cfg.fontSmallWidth;
	}
	if (fs.font_size != tagSize)
	{
		FT_Set_Pixel_Sizes(fs.face, w, tagSize);
		fs.font_size = tagSize;
		fs.charWidthCache.clear();
	}
}

void render_shadow(sp<Image> out_img, std::vector<uint8_t> &img_buf, int width, int height)
{
	auto rgbImg = std::dynamic_pointer_cast<RGBImage>(out_img);
	const int radius = fs.cfg.fontShadowRadius;
	if (!rgbImg || radius <= 0)
		return;
	const int map_size = 2 * radius + 1;

	int y_min = height, y_max = 0, x_min = width, x_max = 0;
	for (int y = 0; y < height; ++y)
	{
		const uint8_t *row = &img_buf[y * width];
		for (int x = 0; x < width; ++x)
		{
			if (row[x] != 0)
			{
				if (y < y_min)
					y_min = y;
				if (y > y_max)
					y_max = y;
				if (x < x_min)
					x_min = x;
				if (x > x_max)
					x_max = x;
			}
		}
	}
	if (y_min > y_max)
		return;

	y_min = (y_min - radius > 0) ? y_min - radius : 0;
	y_max = (y_max + radius < height - 1) ? y_max + radius : height - 1;
	x_min = (x_min - radius > 0) ? x_min - radius : 0;
	x_max = (x_max + radius < width - 1) ? x_max + radius : width - 1;

	std::vector<uint8_t> shadow_alpha(width * height, 0);

	for (int y = y_min; y <= y_max; ++y)
	{
		for (int x = x_min; x <= x_max; ++x)
		{
			if (img_buf[y * width + x] == 0)
				continue;

			const int min_dx = (-x > -radius) ? -x : -radius;
			const int max_dx = (width - x - 1 < radius) ? (width - x - 1) : radius;
			const int min_dy = (-y > -radius) ? -y : -radius;
			const int max_dy = (height - y - 1 < radius) ? (height - y - 1) : radius;

			for (int dy = min_dy; dy <= max_dy; ++dy)
			{
				for (int dx = min_dx; dx <= max_dx; ++dx)
				{
					const uint8_t opacity =
					    fs.shadow_opacity_map[(dy + radius) * map_size + (dx + radius)];
					if (opacity == 0)
						continue;
					uint8_t *p = &shadow_alpha[(y + dy) * width + (x + dx)];
					int alpha = *p + opacity;
					*p = alpha > 255 ? 255 : alpha;
				}
			}
		}
	}

	RGBImageLock rbgRw(rgbImg, ImageLockUse::ReadWrite);
	for (int y = y_min; y <= y_max; ++y)
	{
		for (int x = x_min; x <= x_max; ++x)
		{
			const int index = y * width + x;
			const uint8_t alpha = shadow_alpha[index];
			if (alpha == 0)
				continue;

			Colour c;
			int a = img_buf[index];
			if (a == 0)
			{
				c = {0, 0, 0, alpha};
			}
			else
			{
				c.r = (0xff * a + 0x7F) >> 8;
				c.g = c.r;
				c.b = c.r;
				c.a = 255;
			}
			rbgRw.set(Vec2<int>{x, y}, c);
		}
	}
}

void render_font(FT_Face face, int tx, int ty, std::vector<uint8_t> &dstbuf, int width, int height)
{
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	int baseline = (face->size->metrics.ascender + 63) / 64;
	ty += baseline - face->glyph->bitmap_top;
	int drawHeight = face->glyph->bitmap.rows;
	int drawWidth = bitmap->width;
	int drawY = 0;

	if (ty < 0)
	{
		drawY = -ty;
		ty = 0;
	}
	if ((ty + drawHeight) > height)
	{
		drawHeight -= (ty + drawHeight) - height;
	}
	if (tx < 0)
		tx = 0;
	if ((tx + drawWidth) > width)
	{
		drawWidth -= (tx + drawWidth) - width;
	}

	if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
	{
		for (int y = drawY; y < drawHeight; y++)
		{
			unsigned char *row = &bitmap->buffer[y * bitmap->pitch];
			for (unsigned int x = 0; x < drawWidth; x++)
			{
				if (row[x / 8] & (0x80 >> (x % 8)))
				{
					dstbuf[ty * width + (x + tx)] = 0xff;
				}
			}
			ty++;
		}
	}
	else
	{
		for (int y = drawY; y < drawHeight; y++)
		{
			for (unsigned int x = 0; x < drawWidth; x++)
			{
				int val = bitmap->buffer[y * bitmap->pitch + x];
				if (val && dstbuf[ty * width + (x + tx)] != 0xff)
				{
					dstbuf[ty * width + (x + tx)] = val;
				}
			}
			ty++;
		}
	}
}

int FT_getStringHeight()
{
	int line_height = (fs.face->size->metrics.ascender - fs.face->size->metrics.descender);
	line_height = (line_height + 63) / 64;
	return line_height + fs.cfg.fontShadowRadius;
}

int FT_GetCharWidth(char32_t c)
{
	auto it = fs.charWidthCache.find(c);
	if (it != fs.charWidthCache.end())
		return it->second;
	FT_UInt glyph_index = FT_Get_Char_Index(fs.face, c);
	FT_Load_Glyph(fs.face, glyph_index, FT_LOAD_ADVANCE_ONLY);
	int w = ((fs.face->glyph->advance.x + 63) >> 6) + fs.cfg.fontCharInterval;
	fs.charWidthCache[c] = w;
	return w;
}

int FT_getStringWidth(U32String text)
{
	int len = 0;
	for (const auto &c : text)
	{
		len += FT_GetCharWidth(c);
	}
	return len;
}

#endif

sp<Image> BitmapFont::getString(const UString &Text)
{
	UString translatedText = tr(Text);

	if (translatedText.find('\n') != std::string::npos)
	{
		LogWarning(
		    "Multiline text not supported. Newline characters will be ignored. Text : \"{0}\"",
		    translatedText);
	}

	auto img = fw().data->getFontStringCacheEntry(this->name, translatedText);
	if (img)
		return img;
	auto u32Text = to_u32string(translatedText);
#if ENABLE_FREETYPE
	if (fs.valid)
	{
		FT_SetFontSize(this->name);
		int width = this->getFontWidth(translatedText);
		int line_height = this->getFontHeight();

		sp<Image> newImg;

		if (fs.render_mode != FT_RENDER_MODE_MONO)
			newImg = mksp<RGBImage>(Vec2<int>{width, line_height});
		else
			newImg = mksp<PaletteImage>(Vec2<int>{width, line_height});

		std::vector<uint8_t> str_bitmap(width * line_height, 0);
		if ((width * line_height) > 0)
		{
			int x = fs.cfg.fontShadowRadius;
			int y = fs.cfg.fontShadowRadius / 2;
			for (const auto &c : u32Text)
			{
				FT_UInt glyph_index = FT_Get_Char_Index(fs.face, c);
				FT_Load_Glyph(fs.face, glyph_index, FT_LOAD_DEFAULT);

				if (fs.render_mode != FT_RENDER_MODE_MONO)
					FT_Outline_Embolden(&fs.face->glyph->outline, FT_OUTLINE_SIZE);
				FT_Render_Glyph(fs.face->glyph, fs.render_mode);
				render_font(fs.face, x, y, str_bitmap, width, line_height);
				x += ((fs.face->glyph->advance.x + 63) >> 6) + fs.cfg.fontCharInterval;
			}
			if (fs.cfg.fontShadowRadius)
			{
				render_shadow(newImg, str_bitmap, width, line_height);
			}
			else
			{
				auto palImg = std::dynamic_pointer_cast<PaletteImage>(newImg);
				if (palImg)
				{
					PaletteImageLock w(palImg, ImageLockUse::Write);
					for (y = 0; y < line_height; y++)
					{
						for (x = 0; x < width; x++)
						{
							w.set(Vec2<unsigned int>(x, y),
							      (str_bitmap[y * width + x] != 0) ? 1 : 0);
						}
					}
				}
				else
				{
					auto rgbImg = std::dynamic_pointer_cast<RGBImage>(newImg);
					if (rgbImg)
					{
						RGBImageLock w(rgbImg, ImageLockUse::Write);
						for (y = 0; y < line_height; y++)
						{
							for (x = 0; x < width; x++)
							{
								w.set(Vec2<unsigned int>(x, y),
								      Colour(0xff, 0xff, 0xff, str_bitmap[y * width + x]));
							}
						}
					}
				}
			}
		}
		img = newImg;
		fw().data->putFontStringCacheEntry(this->name, translatedText, img);
		return img;
	}
#endif

	int height = this->getFontHeight();
	int width = this->getFontWidth(translatedText);
	int pos = 0;
	auto palImg = mksp<PaletteImage>(Vec2<int>{width, height});
	img = palImg;

	for (const auto &c : u32Text)
	{
		auto glyph = this->getGlyph(c);
		PaletteImage::blit(glyph, palImg, {0, 0}, {pos, 0});
		pos += glyph->size.x;
	}
	fw().data->putFontStringCacheEntry(this->name, translatedText, img);

	return img;
}

int BitmapFont::getFontWidth(const UString &Text)
{
	int textlen = 0;
	auto u32Text = to_u32string(Text);
#if ENABLE_FREETYPE
	if (fs.valid)
	{

		FT_SetFontSize(this->name);
		textlen = FT_getStringWidth(u32Text);
		return textlen + fs.cfg.fontShadowRadius * 2;
	}
#endif
	for (const auto &c : u32Text)
	{
		auto glyph = this->getGlyph(c);
		textlen += glyph->size.x;
	}
	return textlen;
}

int BitmapFont::getFontHeight() const
{
#if ENABLE_FREETYPE
	if (fs.valid)
	{

		FT_SetFontSize(this->name);
		return FT_getStringHeight();
	}
#endif
	return fontheight;
}

int BitmapFont::getFontHeight(const UString &Text, int MaxWidth)
{
	int height = fontheight;
#if ENABLE_FREETYPE
	if (fs.valid)
	{
		FT_SetFontSize(this->name);
		height = FT_getStringHeight();
	}
#endif
	std::list<UString> lines = wordWrapText(Text, MaxWidth);
	return lines.size() * height;
}

UString BitmapFont::getName() const { return this->name; }

int BitmapFont::getEstimateCharacters(int FitInWidth) const
{
	return FitInWidth / averagecharacterwidth;
}

sp<PaletteImage> BitmapFont::getGlyph(char32_t codepoint)
{
	if (fontbitmaps.find(codepoint) == fontbitmaps.end())
	{
		// FIXME: Hack - assume all missing glyphs are spaces
		// TODO: Fallback fonts?
		LogWarning("Font {0} missing glyph for character \"{1}\" (codepoint {2})", this->getName(),
		           to_ustring(std::u32string(1, codepoint)), static_cast<uint32_t>(codepoint));
		auto missingGlyph = this->getGlyph(to_char32(' '));
		fontbitmaps.emplace(codepoint, missingGlyph);
	}
	return fontbitmaps[codepoint];
}

sp<Palette> BitmapFont::getPalette() const { return this->palette; }

sp<BitmapFont> BitmapFont::loadFont(const std::map<char32_t, UString> &glyphMap, int spaceWidth,
                                    int fontHeight, int kerning, UString fontName,
                                    sp<Palette> defaultPalette)
{
	auto font = mksp<BitmapFont>();

	font->spacewidth = spaceWidth;
	font->fontheight = fontHeight;
	font->kerning = kerning;
	font->averagecharacterwidth = 0;
	font->name = fontName;
	font->palette = defaultPalette;

	size_t totalGlyphWidth = 0;

	for (auto &p : glyphMap)
	{
		auto fontImage = fw().data->loadImage(p.second);
		if (!fontImage)
		{
			LogError("Failed to read glyph image \"{0}\"", p.second);
			continue;
		}
		auto paletteImage = std::dynamic_pointer_cast<PaletteImage>(fontImage);
		if (!paletteImage)
		{
			LogError("Glyph image \"{0}\" doesn't look like a PaletteImage", p.second);
			continue;
		}
		unsigned int maxWidth = 0;

		// FIXME: Proper kerning
		// First find the widest non-transparent part of the glyph
		{
			PaletteImageLock imgLock(paletteImage, ImageLockUse::Read);
			for (unsigned int y = 0; y < paletteImage->size.y; y++)
			{
				for (unsigned int x = 0; x < paletteImage->size.x; x++)
				{
					if (imgLock.get({x, y}) != 0)
					{
						if (x > maxWidth)
							maxWidth = x;
					}
				}
			}
		}
		// Trim the glyph to the max non-transparent width
		auto trimmedGlyph = mksp<PaletteImage>(Vec2<int>{maxWidth + kerning, fontHeight});
		PaletteImage::blit(paletteImage, trimmedGlyph);

		font->fontbitmaps[p.first] = trimmedGlyph;
		totalGlyphWidth += trimmedGlyph->size.x;
	}

	font->averagecharacterwidth = totalGlyphWidth / font->fontbitmaps.size();

	// Always add a 'space' image:

	auto spaceImage = mksp<PaletteImage>(Vec2<int>{spaceWidth, fontHeight});
	font->fontbitmaps[to_char32(' ')] = spaceImage;

	return font;
}

#if ENABLE_FREETYPE
bool isCJKChar(const uint32_t c)
{
	return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
	       (c >= 0x20000 && c <= 0x2A6DF) || (c >= 0x3040 && c <= 0x309F) ||
	       (c >= 0x30A0 && c <= 0x30FF) || (c >= 0xAC00 && c <= 0xD7AF);
}

int findLastOfPrevCJK(U32String str)
{
	int len = str.length();
	for (auto rit = str.rbegin(); rit != str.rend(); ++rit)
	{
		char32_t c = *rit;
		len--;
		if (isCJKChar(c))
			return len;
	}
	return -1;
}
#endif

static std::list<UString> wordWrapTextBySpaces(const UString &str, int MaxWidth,
                                               const std::function<int(const UString &)> &getWidth)
{
	auto remainingChunksVector = split(str, " ");
	auto remainingChunks =
	    std::list<UString>(remainingChunksVector.begin(), remainingChunksVector.end());
	std::list<UString> wrappedLines;
	UString currentLine;

	while (!remainingChunks.empty())
	{
		UString currentTestLine;
		if (currentLine != "")
			currentTestLine = currentLine + " ";

		auto &currentChunk = remainingChunks.front();
		currentTestLine += currentChunk;

		auto estimatedLength = getWidth(currentTestLine);

		if (estimatedLength < MaxWidth)
		{
			currentLine = currentTestLine;
			remainingChunks.pop_front();
		}
		else
		{
			if (currentLine == "")
			{
				LogWarning("No break in line \"{0}\" found - this will probably overflow "
				           "the control",
				           currentTestLine);
				currentLine = currentTestLine;
				remainingChunks.pop_front();
			}
			else
			{
				wrappedLines.push_back(currentLine);
				currentLine = "";
			}
		}
	}
	if (currentLine != "")
		wrappedLines.push_back(currentLine);
	return wrappedLines;
}

std::list<UString> BitmapFont::wordWrapText(const UString &Text, int MaxWidth)
{
	UString translatedText = tr(Text);
	auto lines = split(translatedText, "\n");
	std::list<UString> wrappedLines;

#if ENABLE_FREETYPE
	if (fs.valid)
	{
		FT_SetFontSize(this->name);
		if (MaxWidth > (fs.cfg.fontShadowRadius * 2))
			MaxWidth -= fs.cfg.fontShadowRadius * 2;

		for (const UString &str : lines)
		{
			int txtwidth = getFontWidth(str);

			if (txtwidth > MaxWidth)
			{
				auto U32Text = to_u32string(translatedText);
				int linelen = 0;
				int fontlen;
				U32String curLine = U"";
				bool isHaveCJK = false;
				bool isCjkChar = false;

				for (const auto &c : U32Text)
				{
					fontlen = FT_GetCharWidth(c);
					linelen += fontlen;
					isCjkChar = isCJKChar(c);
					if (isHaveCJK == false)
						isHaveCJK = isCjkChar;

					if (linelen < MaxWidth)
					{
						curLine += c;
					}
					else
					{
						if (isCjkChar)
						{
							wrappedLines.push_back(to_ustring(curLine));
							curLine = c;
							linelen = fontlen;
						}
						else
						{
							size_t pos = curLine.find_last_of(U" ");
							U32String saveLine = U"";
							if (pos != curLine.npos)
								saveLine = curLine.substr(0, pos);
							if (isHaveCJK && (saveLine.length() > 3 || saveLine == U""))
							{
								size_t cjkPos = findLastOfPrevCJK(curLine);
								if (cjkPos != -1)
								{
									pos = cjkPos;
									saveLine = curLine.substr(0, pos + 1);
								}
							}
							if (saveLine.length())
							{
								wrappedLines.push_back(to_ustring(saveLine));
								curLine = curLine.substr(pos + 1) + c;
								linelen = FT_getStringWidth(curLine);
							}
							else
							{
								wrappedLines.push_back(to_ustring(curLine));
								curLine = c;
								linelen = fontlen;
							}
						}
						isHaveCJK = false;
					}
				}
				if (curLine != U"")
					wrappedLines.push_back(to_ustring(curLine));
			}
			else
			{
				wrappedLines.push_back(str);
			}
		}
		return wrappedLines;
	}
#endif

	for (const UString &str : lines)
	{
		int txtwidth = getFontWidth(str);

		if (txtwidth > MaxWidth)
		{
			auto subLines = wordWrapTextBySpaces(str, MaxWidth, [this](const UString &s)
			                                     { return this->getFontWidth(s); });
			wrappedLines.splice(wrappedLines.end(), subLines);
		}
		else
		{
			wrappedLines.push_back(str);
		}
	}

	return wrappedLines;
}

}; // namespace OpenApoc
