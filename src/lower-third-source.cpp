// Word & Song for OBS — the "Word & Song Display" source that draws scripture, lyrics,
// cards and graphics, styled by the dock's themes or by its own settings.
#include "lower-third-source.hpp"

#include "live-state.hpp"
#include "themes.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QString>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ws {
namespace {

enum Layout { LowerThird = 0, FullScreen = 1, Top = 2 };
enum ThemeMode { FollowDock = 0, OwnSettings = 1 };

#ifdef __APPLE__
const char *kDefaultFace = "Helvetica Neue";
#elif defined(_WIN32)
const char *kDefaultFace = "Segoe UI";
#else
const char *kDefaultFace = "Sans Serif";
#endif

struct Style {
	int variant = StyleCard;
	QString face = QString::fromLatin1(kDefaultFace);
	int bodySize = 46;
	bool bold = false, italic = false;
	int titleSize = 30;
	QColor textColor = Qt::white;
	QColor titleColor = QColor(0xF2, 0xC1, 0x4E);
	QColor accentColor = QColor(0xF2, 0xC1, 0x4E);
	QColor boxColor = QColor(16, 16, 20, 217);
	int widthPct = 84;
	int margin = 56;
	int padding = 36;
	int radius = 18;
	bool center = false;
	bool showTitle = true;
	bool titleBelow = false;
	bool upperTitle = true;
	bool shadow = true;
	int maxLines = 4;
	int fadeMs = 350;
};

struct Card {
	gs_texture_t *tex = nullptr;
	int x = 0, y = 0, w = 0, h = 0;
};

struct DisplaySource {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	Style own;
	int layout = LowerThird;
	int themeMode = FollowDock;
	bool showScripture = true, showLyrics = true, showCards = true, showGraphics = true;
	bool sample = false;
	int sizePct = 100; // per-display size adjustment, applied on top of any theme
	int offsetY = 0;   // per-display nudge, positive = up
	uint64_t seenRevision = 0, seenThemeRevision = 0;
	bool dirty = true;
	Card cur, prev;
	uint64_t changeTs = 0;
	int fadeMs = 350;
	uint32_t canvasW = 1920, canvasH = 1080;
};

// OBS colour properties are 0xAABBGGRR.
QColor fromObs(long long c)
{
	uint32_t v = (uint32_t)c;
	return QColor((int)(v & 0xFF), (int)((v >> 8) & 0xFF), (int)((v >> 16) & 0xFF), (int)((v >> 24) & 0xFF));
}

void destroyCard(Card &c)
{
	if (c.tex) {
		gs_texture_destroy(c.tex);
		c.tex = nullptr;
	}
}

void getDefaults(obs_data_t *settings);

// Reads the look keys of a settings object (defaults applied) into a Style.
Style readLook(obs_data_t *settings)
{
	Style s;
	s.variant = (int)obs_data_get_int(settings, "style");
	obs_data_t *font = obs_data_get_obj(settings, "font");
	if (font) {
		const char *face = obs_data_get_string(font, "face");
		if (face && *face)
			s.face = QString::fromUtf8(face);
		long long size = obs_data_get_int(font, "size");
		if (size > 0)
			s.bodySize = (int)size;
		long long flags = obs_data_get_int(font, "flags");
		s.bold = (flags & OBS_FONT_BOLD) != 0;
		s.italic = (flags & OBS_FONT_ITALIC) != 0;
		obs_data_release(font);
	}
	s.titleSize = (int)obs_data_get_int(settings, "title_size");
	s.textColor = fromObs(obs_data_get_int(settings, "text_color"));
	s.titleColor = fromObs(obs_data_get_int(settings, "title_color"));
	s.accentColor = fromObs(obs_data_get_int(settings, "accent_color"));
	s.boxColor = fromObs(obs_data_get_int(settings, "box_color"));
	s.widthPct = (int)obs_data_get_int(settings, "box_width");
	s.margin = (int)obs_data_get_int(settings, "margin");
	s.padding = (int)obs_data_get_int(settings, "padding");
	s.radius = (int)obs_data_get_int(settings, "radius");
	s.center = obs_data_get_int(settings, "align") == 1;
	s.showTitle = obs_data_get_bool(settings, "show_title");
	s.titleBelow = obs_data_get_int(settings, "title_position") == 1;
	s.upperTitle = obs_data_get_bool(settings, "upper_title");
	s.shadow = obs_data_get_bool(settings, "shadow");
	s.maxLines = (int)obs_data_get_int(settings, "max_lines");
	s.fadeMs = (int)obs_data_get_int(settings, "fade_ms");
	return s;
}

// Sizes are designed for a 1080p canvas; scale them for other canvases and the display's size %.
void scaleStyle(Style &s, double f)
{
	if (std::abs(f - 1.0) < 0.001)
		return;
	auto sc = [f](int v) {
		return std::max(0, (int)std::lround(v * f));
	};
	s.bodySize = std::max(8, sc(s.bodySize));
	s.titleSize = std::max(8, sc(s.titleSize));
	s.padding = sc(s.padding);
	s.margin = sc(s.margin);
	s.radius = sc(s.radius);
}

Style themeStyle(const std::string &json)
{
	Style s;
	obs_data_t *look = obs_data_create_from_json(json.c_str());
	if (look) {
		getDefaults(look);
		s = readLook(look);
		obs_data_release(look);
	}
	return s;
}

// The style to draw `content` with: the dock's theme for it, or this display's own settings.
Style effectiveStyle(DisplaySource *d, const LiveContent &content)
{
	Style s = d->own;
	bool fromTheme = false;
	if (d->themeMode == FollowDock && !content.themeId.empty()) {
		ThemeEntry theme;
		if (ThemeLibrary::instance().find(content.themeId, theme)) {
			s = themeStyle(theme.json);
			fromTheme = true;
		}
	}
	if (fromTheme && d->layout == FullScreen) {
		// Themes are designed as lower thirds; scale them up for a full screen.
		s.bodySize = s.bodySize * 3 / 2;
		s.titleSize = s.titleSize * 5 / 4;
		s.padding = std::max(s.padding, 90);
	}
	scaleStyle(s, (double)d->canvasH / 1080.0 * (double)d->sizePct / 100.0);
	return s;
}

void drawShadowedText(QPainter &p, const QRect &r, int align, const QFont &f, const QColor &c, const QString &t,
		      bool shadow, bool strong)
{
	if (t.isEmpty())
		return;
	p.setFont(f);
	if (shadow || strong) {
		int off = std::max(1, f.pixelSize() / 22);
		if (strong) {
			// A soft outline so text stays readable with no box behind it.
			p.setPen(QColor(0, 0, 0, 110));
			int o = std::max(2, f.pixelSize() / 24);
			for (int dx = -o; dx <= o; dx += o)
				for (int dy = -o; dy <= o; dy += o)
					if (dx || dy)
						p.drawText(r.translated(dx, dy), align, t);
		}
		p.setPen(QColor(0, 0, 0, strong ? 170 : 150));
		p.drawText(r.translated(off, off), align, t);
	}
	p.setPen(c);
	p.drawText(r, align, t);
}

// Lays out and paints a text card. Returns the image; outX/outY = where it goes on the canvas.
QImage renderText(const LiveContent &content, Style s, int layout, uint32_t cw, uint32_t ch, int &outX, int &outY)
{
	const bool full = layout == FullScreen;
	const bool isCard = content.kind == ContentKind::Card;
	const bool isLyrics = content.kind == ContentKind::Lyrics;
	int variant = s.variant;
	if (full && variant != StyleClean)
		variant = StyleCard; // bars, tags and splits are lower-third shapes
	if (variant == StyleBar)
		s.radius = 0;

	QString title = s.showTitle ? QString::fromStdString(content.title) : QString();
	if (s.upperTitle)
		title = title.toUpper();
	const QString body = QString::fromStdString(content.body);
	if (variant == StyleSplit && title.isEmpty())
		variant = StyleCard;
	// Cards read name-first: the small line goes under the big one.
	bool titleBelow = s.titleBelow || isCard;

	// Full screen fills the frame inside the margin; a bar always runs edge to edge.
	const int boxW = full                  ? (int)cw - 2 * s.margin
			 : variant == StyleBar ? (int)cw
					       : (int)((double)cw * s.widthPct / 100.0);
	const int stripe = variant == StyleBar ? std::max(8, s.padding / 3) : 0;
	const int splitW = variant == StyleSplit ? std::max(160, boxW * 30 / 100) : 0;
	const int textX = s.padding + stripe + splitW;
	const int innerW = std::max(100, boxW - textX - s.padding);
	const int align = (s.center || isLyrics || full ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::TextWordWrap;

	QFont titleFont(s.face);
	titleFont.setPixelSize(std::max(10, s.titleSize));
	titleFont.setBold(true);
	titleFont.setLetterSpacing(QFont::PercentageSpacing, s.upperTitle ? 108 : 100);
	QFontMetrics tfm(titleFont);

	// Where the title goes: inside the text column (card/bar/clean), in a tag above the box,
	// or in the left panel (split).
	const bool titleInColumn = variant != StyleTag && variant != StyleSplit;
	const int titleH = title.isEmpty() || !titleInColumn
				   ? 0
				   : tfm.boundingRect(QRect(0, 0, innerW, 10000), align, title).height();
	const int gap = titleH == 0 || body.isEmpty() ? 0 : std::max(6, s.titleSize / 3);

	// Shrink the body font until it fits the line limit (lower third) or the screen (full).
	QFont bodyFont(s.face);
	bodyFont.setBold(s.bold || isCard);
	bodyFont.setItalic(s.italic);
	int size = std::max(12, s.bodySize);
	const int minSize = std::max(12, s.bodySize * 45 / 100);
	const int maxBodyH = full ? (int)ch - 2 * s.margin - 2 * s.padding - titleH - gap : 0;
	const int maxLines = isCard ? std::min(s.maxLines, 2) : s.maxLines;
	int bodyH = 0;
	for (;; size -= 2) {
		bodyFont.setPixelSize(size);
		QFontMetrics fm(bodyFont);
		bodyH = body.isEmpty() ? 0 : fm.boundingRect(QRect(0, 0, innerW, 100000), align, body).height();
		bool fits = full ? bodyH <= maxBodyH : bodyH <= fm.lineSpacing() * maxLines + fm.descent();
		// Lyric lines read best unbroken: shrink until each written line fits on one line.
		if (fits && isLyrics) {
			for (const QString &line : body.split('\n'))
				if (fm.horizontalAdvance(line) > innerW) {
					fits = false;
					break;
				}
		}
		if (fits || size <= minSize)
			break;
	}

	// Tag: the title sits in a coloured tab overlapping the top edge of the box.
	const bool hasTag = variant == StyleTag && !title.isEmpty();
	const int tagPadV = std::max(4, s.titleSize / 4), tagPadH = std::max(10, s.titleSize / 2);
	const int tagH = hasTag ? tfm.height() + 2 * tagPadV : 0;
	const int tagW = hasTag ? std::min(boxW - 2 * s.padding, tfm.horizontalAdvance(title) + 2 * tagPadH) : 0;
	const int tagOverlap = tagH / 2;

	// Split: the title is wrapped inside the left panel.
	int splitTitleH = 0;
	if (variant == StyleSplit)
		splitTitleH = tfm.boundingRect(QRect(0, 0, splitW - 2 * s.padding / 2, 10000),
					       Qt::AlignCenter | Qt::TextWordWrap, title)
				      .height();

	int contentH = bodyH + titleH + gap;
	int boxH = full ? (int)ch - 2 * s.margin
			: std::max(contentH, splitTitleH) + 2 * s.padding + (hasTag ? tagOverlap : 0);
	const int imgH = boxH + (hasTag ? tagH - tagOverlap : 0);
	const int boxTop = hasTag ? tagH - tagOverlap : 0;

	outX = full ? s.margin : ((int)cw - boxW) / 2;
	if (full)
		outY = s.margin;
	else if (layout == Top)
		outY = s.margin;
	else
		outY = (int)ch - s.margin - imgH;

	QImage img(boxW, std::max(1, imgH), QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	const QRectF boxRect(0, boxTop, boxW, boxH);
	if (variant != StyleClean && s.boxColor.alpha() > 0) {
		QPainterPath path;
		path.addRoundedRect(boxRect, s.radius, s.radius);
		p.fillPath(path, s.boxColor);
	}
	if (variant == StyleBar) {
		p.fillRect(QRectF(0, boxTop, stripe, boxH), s.accentColor);
	}
	if (variant == StyleSplit) {
		QPainterPath clip;
		clip.addRoundedRect(boxRect, s.radius, s.radius);
		p.save();
		p.setClipPath(clip);
		p.fillRect(QRectF(0, boxTop, splitW, boxH), s.accentColor);
		p.restore();
		QRect tr(s.padding / 2, boxTop, splitW - s.padding, boxH);
		drawShadowedText(p, tr, Qt::AlignCenter | Qt::TextWordWrap, titleFont, s.titleColor, title, false,
				 false);
	}
	if (hasTag) {
		QPainterPath tagPath;
		const int tagX = s.center ? (boxW - tagW) / 2 : s.padding;
		tagPath.addRoundedRect(QRectF(tagX, 0, tagW, tagH), std::min(s.radius, tagH / 2),
				       std::min(s.radius, tagH / 2));
		p.fillPath(tagPath, s.accentColor);
		drawShadowedText(p, QRect(tagX, 0, tagW, tagH), Qt::AlignCenter, titleFont, s.titleColor, title, false,
				 false);
	}

	const bool strong = variant == StyleClean;
	int y = boxTop + (full ? (boxH - contentH) / 2 : s.padding + (hasTag ? tagOverlap : 0));
	if (!full && variant == StyleSplit)
		y = boxTop + (boxH - contentH) / 2;
	QRect titleRect, bodyRect;
	if (titleBelow) {
		bodyRect = QRect(textX, y, innerW, bodyH);
		titleRect = QRect(textX, y + bodyH + gap, innerW, titleH);
	} else {
		titleRect = QRect(textX, y, innerW, titleH);
		bodyRect = QRect(textX, y + titleH + gap, innerW, bodyH);
	}
	if (titleInColumn)
		drawShadowedText(p, titleRect, align, titleFont, s.titleColor, title, s.shadow, strong);
	drawShadowedText(p, bodyRect, align, bodyFont, s.textColor, body, s.shadow, strong);
	p.end();
	return img;
}

// Graphics fill the frame (keeping their shape), so full-frame PNGs with transparency line up.
QImage renderGraphic(const std::string &path, uint32_t cw, uint32_t ch, int &outX, int &outY)
{
	QImageReader reader(QString::fromStdString(path));
	reader.setAutoTransform(true);
	QImage src = reader.read();
	if (src.isNull()) {
		blog(LOG_WARNING, "[word-and-song] could not open image %s", path.c_str());
		return {};
	}
	QImage scaled = src.scaled((int)cw, (int)ch, Qt::KeepAspectRatio, Qt::SmoothTransformation)
				.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
	outX = ((int)cw - scaled.width()) / 2;
	outY = ((int)ch - scaled.height()) / 2;
	return scaled;
}

Card makeCard(const QImage &img, int x, int y)
{
	Card c;
	if (img.isNull())
		return c;
	c.x = x;
	c.y = y;
	c.w = img.width();
	c.h = img.height();
	const uint8_t *bits = img.constBits();
	obs_enter_graphics();
	c.tex = gs_texture_create((uint32_t)c.w, (uint32_t)c.h, GS_RGBA, 1, &bits, 0);
	obs_leave_graphics();
	return c;
}

void readSettings(DisplaySource *d, obs_data_t *settings)
{
	d->own = readLook(settings);
	d->layout = (int)obs_data_get_int(settings, "layout");
	d->themeMode = (int)obs_data_get_int(settings, "theme_mode");
	d->showScripture = obs_data_get_bool(settings, "show_scripture");
	d->showLyrics = obs_data_get_bool(settings, "show_lyrics");
	d->showCards = obs_data_get_bool(settings, "show_cards");
	d->showGraphics = obs_data_get_bool(settings, "show_graphics");
	d->sample = obs_data_get_bool(settings, "sample");
	d->sizePct = (int)obs_data_get_int(settings, "size_pct");
	d->offsetY = (int)obs_data_get_int(settings, "offset_y");
	d->dirty = true;
}

const char *getName(void *)
{
	return obs_module_text("DisplaySource");
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new DisplaySource();
	d->source = source;
	char *path = obs_module_file("effects/fade.effect");
	obs_enter_graphics();
	d->effect = path ? gs_effect_create_from_file(path, nullptr) : nullptr;
	obs_leave_graphics();
	bfree(path);
	if (!d->effect)
		blog(LOG_WARNING, "[word-and-song] could not load fade.effect");
	readSettings(d, settings);
	return d;
}

void destroy(void *data)
{
	auto *d = static_cast<DisplaySource *>(data);
	obs_enter_graphics();
	destroyCard(d->cur);
	destroyCard(d->prev);
	if (d->effect)
		gs_effect_destroy(d->effect);
	obs_leave_graphics();
	delete d;
}

void update(void *data, obs_data_t *settings)
{
	readSettings(static_cast<DisplaySource *>(data), settings);
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "theme_mode", FollowDock);
	obs_data_set_default_int(settings, "layout", LowerThird);
	obs_data_set_default_bool(settings, "show_scripture", true);
	obs_data_set_default_bool(settings, "show_lyrics", true);
	obs_data_set_default_bool(settings, "show_cards", true);
	obs_data_set_default_bool(settings, "show_graphics", true);
	obs_data_set_default_int(settings, "style", StyleCard);
	obs_data_t *font = obs_data_create();
	obs_data_set_default_string(font, "face", kDefaultFace);
	obs_data_set_default_string(font, "style", "Regular");
	obs_data_set_default_int(font, "size", 46);
	obs_data_set_default_int(font, "flags", 0);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);
	obs_data_set_default_int(settings, "title_size", 30);
	obs_data_set_default_int(settings, "text_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "title_color", 0xFF4EC1F2);
	obs_data_set_default_int(settings, "accent_color", 0xFF4EC1F2);
	obs_data_set_default_int(settings, "box_color", 0xD9141010);
	obs_data_set_default_int(settings, "box_width", 84);
	obs_data_set_default_int(settings, "margin", 56);
	obs_data_set_default_int(settings, "padding", 36);
	obs_data_set_default_int(settings, "radius", 18);
	obs_data_set_default_int(settings, "align", 0);
	obs_data_set_default_bool(settings, "show_title", true);
	obs_data_set_default_int(settings, "title_position", 0);
	obs_data_set_default_bool(settings, "upper_title", true);
	obs_data_set_default_bool(settings, "shadow", true);
	obs_data_set_default_int(settings, "max_lines", 4);
	obs_data_set_default_int(settings, "fade_ms", 350);
	obs_data_set_default_bool(settings, "sample", false);
	obs_data_set_default_int(settings, "size_pct", 100);
	obs_data_set_default_int(settings, "offset_y", 0);
}

std::function<void()> g_editThemes;

bool editThemesClicked(obs_properties_t *, obs_property_t *, void *)
{
	if (g_editThemes)
		g_editThemes();
	return false;
}

bool themeModeChanged(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	bool own = obs_data_get_int(settings, "theme_mode") == OwnSettings;
	obs_property_set_visible(obs_properties_get(props, "look"), own);
	return true;
}

obs_properties_t *getProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(props, "theme_mode", obs_module_text("ThemeMode"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ThemeMode.Follow"), FollowDock);
	obs_property_list_add_int(p, obs_module_text("ThemeMode.Own"), OwnSettings);
	obs_property_set_modified_callback(p, themeModeChanged);
	obs_property_set_long_description(p, obs_module_text("ThemeMode.Tip"));

	p = obs_properties_add_list(props, "layout", obs_module_text("Layout"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Layout.LowerThird"), LowerThird);
	obs_property_list_add_int(p, obs_module_text("Layout.FullScreen"), FullScreen);
	obs_property_list_add_int(p, obs_module_text("Layout.Top"), Top);

	obs_properties_add_bool(props, "show_scripture", obs_module_text("Show.Scripture"));
	obs_properties_add_bool(props, "show_lyrics", obs_module_text("Show.Lyrics"));
	obs_properties_add_bool(props, "show_cards", obs_module_text("Show.Cards"));
	obs_properties_add_bool(props, "show_graphics", obs_module_text("Show.Graphics"));
	obs_properties_add_bool(props, "sample", obs_module_text("Sample"));
	obs_properties_add_int_slider(props, "size_pct", obs_module_text("SizePct"), 50, 200, 5);
	p = obs_properties_add_int_slider(props, "offset_y", obs_module_text("OffsetY"), -500, 500, 2);
	obs_property_set_long_description(p, obs_module_text("OffsetY.Tip"));
	obs_properties_add_button(props, "edit_themes", obs_module_text("EditThemes"), editThemesClicked);

	obs_properties_t *look = obs_properties_create();
	p = obs_properties_add_list(look, "style", obs_module_text("Style"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Style.Card"), StyleCard);
	obs_property_list_add_int(p, obs_module_text("Style.Bar"), StyleBar);
	obs_property_list_add_int(p, obs_module_text("Style.Tag"), StyleTag);
	obs_property_list_add_int(p, obs_module_text("Style.Clean"), StyleClean);
	obs_property_list_add_int(p, obs_module_text("Style.Split"), StyleSplit);
	obs_properties_add_font(look, "font", obs_module_text("Font"));
	obs_properties_add_int_slider(look, "title_size", obs_module_text("TitleSize"), 12, 120, 1);
	obs_properties_add_color(look, "text_color", obs_module_text("TextColor"));
	obs_properties_add_color(look, "title_color", obs_module_text("TitleColor"));
	obs_properties_add_color(look, "accent_color", obs_module_text("AccentColor"));
	obs_properties_add_color_alpha(look, "box_color", obs_module_text("BoxColor"));
	p = obs_properties_add_list(look, "align", obs_module_text("Align"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Align.Left"), 0);
	obs_property_list_add_int(p, obs_module_text("Align.Center"), 1);
	obs_properties_add_bool(look, "show_title", obs_module_text("ShowTitle"));
	p = obs_properties_add_list(look, "title_position", obs_module_text("TitlePosition"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("TitlePosition.Above"), 0);
	obs_property_list_add_int(p, obs_module_text("TitlePosition.Below"), 1);
	obs_properties_add_bool(look, "upper_title", obs_module_text("UpperTitle"));
	obs_properties_add_bool(look, "shadow", obs_module_text("Shadow"));
	obs_properties_add_int_slider(look, "box_width", obs_module_text("BoxWidth"), 30, 100, 1);
	obs_properties_add_int_slider(look, "max_lines", obs_module_text("MaxLines"), 1, 10, 1);
	obs_properties_add_int(look, "margin", obs_module_text("Margin"), 0, 600, 2);
	obs_properties_add_int(look, "padding", obs_module_text("Padding"), 0, 200, 2);
	obs_properties_add_int(look, "radius", obs_module_text("Radius"), 0, 200, 1);
	obs_properties_add_int_slider(look, "fade_ms", obs_module_text("FadeMs"), 0, 2000, 50);
	obs_properties_add_group(props, "look", obs_module_text("OwnLook"), OBS_GROUP_NORMAL, look);
	return props;
}

LiveContent sampleContent(DisplaySource *d)
{
	LiveContent c;
	c.visible = true;
	if (!d->showScripture && d->showLyrics) {
		c.kind = ContentKind::Lyrics;
		c.title = "Chorus";
		c.body = "Great is Thy faithfulness\nMorning by morning new mercies I see";
	} else if (!d->showScripture && d->showCards) {
		c.kind = ContentKind::Card;
		c.title = "Senior Pastor";
		c.body = "Pastor Jane Doe";
	} else {
		c.kind = ContentKind::Scripture;
		c.title = "John 3:16 (KJV)";
		c.body = "For God so loved the world, that he gave his only begotten Son, that whosoever believeth "
			 "in him should not perish, but have everlasting life.";
	}
	return c;
}

bool wants(const DisplaySource *d, ContentKind k)
{
	switch (k) {
	case ContentKind::Scripture:
		return d->showScripture;
	case ContentKind::Lyrics:
		return d->showLyrics;
	case ContentKind::Card:
		return d->showCards;
	case ContentKind::Graphic:
		return d->showGraphics;
	default:
		return false;
	}
}

void videoTick(void *data, float)
{
	auto *d = static_cast<DisplaySource *>(data);
	obs_video_info ovi;
	if (obs_get_video_info(&ovi) && (ovi.base_width != d->canvasW || ovi.base_height != d->canvasH)) {
		d->canvasW = ovi.base_width;
		d->canvasH = ovi.base_height;
		d->dirty = true;
	}
	uint64_t themeRev = ThemeLibrary::instance().revision();
	uint64_t rev = liveRevision();
	if (rev == d->seenRevision && themeRev == d->seenThemeRevision && !d->dirty)
		return;
	const bool animate = rev != d->seenRevision;
	d->seenThemeRevision = themeRev;
	LiveContent c = getLive(&d->seenRevision);
	d->dirty = false;

	bool show = c.visible && wants(d, c.kind);
	if (!show && d->sample) {
		c = sampleContent(d);
		show = true;
	}

	Card next;
	Style style = effectiveStyle(d, c);
	if (show) {
		int x = 0, y = 0;
		QImage img = c.kind == ContentKind::Graphic
				     ? renderGraphic(c.imagePath, d->canvasW, d->canvasH, x, y)
				     : renderText(c, style, d->layout, d->canvasW, d->canvasH, x, y);
		if (c.kind != ContentKind::Graphic)
			y -= (int)std::lround(d->offsetY * (double)d->canvasH / 1080.0);
		next = makeCard(img, x, y);
	}

	obs_enter_graphics();
	d->fadeMs = style.fadeMs;
	if (animate && d->fadeMs > 0) {
		destroyCard(d->prev);
		d->prev = d->cur;
		d->changeTs = os_gettime_ns();
	} else {
		destroyCard(d->cur);
		destroyCard(d->prev);
		d->changeTs = 0;
	}
	d->cur = next;
	obs_leave_graphics();
}

void drawCard(DisplaySource *d, const Card &c, float opacity, float yOffset)
{
	if (!c.tex || opacity <= 0.0f)
		return;
	gs_eparam_t *img = gs_effect_get_param_by_name(d->effect, "image");
	gs_eparam_t *op = gs_effect_get_param_by_name(d->effect, "opacity");
	gs_effect_set_texture(img, c.tex);
	gs_effect_set_float(op, opacity);
	gs_matrix_push();
	gs_matrix_translate3f((float)c.x, (float)c.y + yOffset, 0.0f);
	while (gs_effect_loop(d->effect, "Draw"))
		gs_draw_sprite(c.tex, 0, (uint32_t)c.w, (uint32_t)c.h);
	gs_matrix_pop();
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<DisplaySource *>(data);
	if (!d->effect)
		return;
	float t = 1.0f;
	if (d->changeTs && d->fadeMs > 0) {
		double elapsed = (double)(os_gettime_ns() - d->changeTs) / 1e6;
		t = (float)std::min(1.0, elapsed / d->fadeMs);
	}
	float e = 1.0f - (1.0f - t) * (1.0f - t); // ease-out
	// Graphics fade in place; text cards also rise slightly.
	const bool fullFrame = d->cur.w == (int)d->canvasW || d->cur.h == (int)d->canvasH;

	const bool prevSrgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	if (t < 1.0f)
		drawCard(d, d->prev, 1.0f - e, 0.0f);
	drawCard(d, d->cur, e, fullFrame ? 0.0f : (1.0f - e) * 14.0f);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prevSrgb);

	if (t >= 1.0f && d->prev.tex)
		destroyCard(d->prev);
}

uint32_t getWidth(void *data)
{
	return static_cast<DisplaySource *>(data)->canvasW;
}

uint32_t getHeight(void *data)
{
	return static_cast<DisplaySource *>(data)->canvasH;
}

} // namespace

void setEditThemesHandler(std::function<void()> handler)
{
	g_editThemes = std::move(handler);
}

void applyLookDefaults(obs_data_t *settings)
{
	getDefaults(settings);
}

QImage renderThemePreview(const std::string &lookJson, ContentKind kind, int width, int height)
{
	const uint32_t cw = 1920, ch = 1080;
	QImage frame((int)cw, (int)ch, QImage::Format_RGBA8888_Premultiplied);
	// A stand-in for camera video so the box and text contrast read realistically.
	QPainter p(&frame);
	QLinearGradient g(0, 0, cw, ch);
	g.setColorAt(0.0, QColor(58, 74, 92));
	g.setColorAt(0.55, QColor(96, 84, 76));
	g.setColorAt(1.0, QColor(34, 38, 48));
	p.fillRect(frame.rect(), g);
	p.fillRect(QRect(760, 140, 400, 700), QColor(30, 30, 34, 90)); // a "person"
	LiveContent c;
	c.visible = true;
	c.kind = kind;
	if (kind == ContentKind::Lyrics) {
		c.title = "Chorus";
		c.body = "Great is Thy faithfulness\nMorning by morning new mercies I see";
	} else if (kind == ContentKind::Card) {
		c.title = "Senior Pastor";
		c.body = "Pastor Jane Doe";
	} else {
		c.kind = ContentKind::Scripture;
		c.title = "John 3:16 (KJV)";
		c.body = "For God so loved the world, that he gave his only begotten Son, that whosoever believeth "
			 "in him should not perish, but have everlasting life.";
	}
	int x = 0, y = 0;
	QImage card = renderText(c, themeStyle(lookJson), LowerThird, cw, ch, x, y);
	p.drawImage(x, y, card);
	p.end();
	return frame.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void registerDisplaySource()
{
	obs_source_info info = {};
	info.id = kDisplaySourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.video_tick = videoTick;
	info.video_render = videoRender;
	info.get_width = getWidth;
	info.get_height = getHeight;
	obs_register_source(&info);
}

} // namespace ws
