// Word & Song for OBS — the "Word & Song Display" source that draws scripture and lyrics.
#include "lower-third-source.hpp"

#include "live-state.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QString>

#include <algorithm>
#include <cmath>

namespace ws {
namespace {

enum Layout { LowerThird = 0, FullScreen = 1, Top = 2 };
enum Filter { ShowAll = 0, ShowScripture = 1, ShowLyrics = 2 };

struct Style {
	int filter = ShowAll;
	int layout = LowerThird;
	QString face = QStringLiteral("Arial");
	int bodySize = 46;
	bool bold = false, italic = false;
	int titleSize = 30;
	QColor textColor = Qt::white;
	QColor titleColor = QColor(0xF2, 0xC1, 0x4E);
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
	bool sample = false;
};

struct Card {
	gs_texture_t *tex = nullptr;
	int x = 0, y = 0, w = 0, h = 0;
};

struct DisplaySource {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	Style style;
	uint64_t seenRevision = 0;
	bool dirty = true;
	Card cur, prev;
	uint64_t changeTs = 0;
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

QImage renderImage(const LiveContent &content, const Style &s, uint32_t cw, uint32_t ch, int &outX, int &outY)
{
	const bool full = s.layout == FullScreen;
	const int boxW = full ? (int)cw - 2 * s.margin : (int)((double)cw * s.widthPct / 100.0);
	const int innerW = std::max(100, boxW - 2 * s.padding);
	const int align = (s.center || content.kind == ContentKind::Lyrics || full ? Qt::AlignHCenter : Qt::AlignLeft) |
			  Qt::TextWordWrap;

	QString title = s.showTitle ? QString::fromStdString(content.title) : QString();
	if (s.upperTitle)
		title = title.toUpper();
	const QString body = QString::fromStdString(content.body);

	QFont titleFont(s.face);
	titleFont.setPixelSize(std::max(10, s.titleSize));
	titleFont.setBold(true);
	titleFont.setLetterSpacing(QFont::PercentageSpacing, s.upperTitle ? 108 : 100);
	const int titleH = title.isEmpty() ? 0 : QFontMetrics(titleFont).height();
	const int gap = title.isEmpty() || body.isEmpty() ? 0 : std::max(6, s.titleSize / 3);

	// Shrink the body font until it fits the line limit (lower third) or the screen (full).
	QFont bodyFont(s.face);
	bodyFont.setBold(s.bold);
	bodyFont.setItalic(s.italic);
	int size = std::max(12, s.bodySize);
	const int minSize = std::max(12, s.bodySize * 45 / 100);
	const int maxBodyH = full ? (int)ch - 2 * s.margin - 2 * s.padding - titleH - gap : 0;
	int bodyH = 0;
	for (;; size -= 2) {
		bodyFont.setPixelSize(size);
		QFontMetrics fm(bodyFont);
		bodyH = body.isEmpty() ? 0 : fm.boundingRect(QRect(0, 0, innerW, 100000), align, body).height();
		bool fits = full ? bodyH <= maxBodyH : bodyH <= fm.lineSpacing() * s.maxLines + fm.descent();
		// Lyric lines read best unbroken: shrink until each written line fits on one line.
		if (fits && content.kind == ContentKind::Lyrics) {
			for (const QString &line : body.split('\n'))
				if (fm.horizontalAdvance(line) > innerW) {
					fits = false;
					break;
				}
		}
		if (fits || size <= minSize)
			break;
	}

	const int boxH = full ? (int)ch - 2 * s.margin : bodyH + titleH + gap + 2 * s.padding;
	outX = full ? s.margin : ((int)cw - boxW) / 2;
	outY = full ? s.margin : (s.layout == Top ? s.margin : (int)ch - s.margin - boxH);

	QImage img(boxW, std::max(1, boxH), QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	if (s.boxColor.alpha() > 0) {
		QPainterPath path;
		path.addRoundedRect(QRectF(0, 0, boxW, boxH), s.radius, s.radius);
		p.fillPath(path, s.boxColor);
	}

	int contentH = bodyH + titleH + gap;
	int y = full ? (boxH - contentH) / 2 : s.padding;
	QRect titleRect, bodyRect;
	if (s.titleBelow) {
		bodyRect = QRect(s.padding, y, innerW, bodyH);
		titleRect = QRect(s.padding, y + bodyH + gap, innerW, titleH);
	} else {
		titleRect = QRect(s.padding, y, innerW, titleH);
		bodyRect = QRect(s.padding, y + titleH + gap, innerW, bodyH);
	}

	auto drawText = [&](const QRect &r, const QFont &f, const QColor &c, const QString &t) {
		if (t.isEmpty())
			return;
		p.setFont(f);
		if (s.shadow) {
			int off = std::max(1, f.pixelSize() / 22);
			p.setPen(QColor(0, 0, 0, 150));
			p.drawText(r.translated(off, off), align, t);
		}
		p.setPen(c);
		p.drawText(r, align, t);
	};
	drawText(titleRect, titleFont, s.titleColor, title);
	drawText(bodyRect, bodyFont, s.textColor, body);
	p.end();
	return img;
}

Card makeCard(const QImage &img, int x, int y)
{
	Card c;
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

void readStyle(DisplaySource *d, obs_data_t *settings)
{
	Style &s = d->style;
	s.filter = (int)obs_data_get_int(settings, "content");
	s.layout = (int)obs_data_get_int(settings, "layout");
	obs_data_t *font = obs_data_get_obj(settings, "font");
	if (font) {
		const char *face = obs_data_get_string(font, "face");
		if (face && *face)
			s.face = QString::fromUtf8(face);
		s.bodySize = (int)obs_data_get_int(font, "size");
		long long flags = obs_data_get_int(font, "flags");
		s.bold = (flags & OBS_FONT_BOLD) != 0;
		s.italic = (flags & OBS_FONT_ITALIC) != 0;
		obs_data_release(font);
	}
	s.titleSize = (int)obs_data_get_int(settings, "title_size");
	s.textColor = fromObs(obs_data_get_int(settings, "text_color"));
	s.titleColor = fromObs(obs_data_get_int(settings, "title_color"));
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
	s.sample = obs_data_get_bool(settings, "sample");
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
	readStyle(d, settings);
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
	readStyle(static_cast<DisplaySource *>(data), settings);
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "content", ShowAll);
	obs_data_set_default_int(settings, "layout", LowerThird);
	obs_data_t *font = obs_data_create();
#ifdef __APPLE__
	obs_data_set_default_string(font, "face", "Helvetica Neue");
#elif defined(_WIN32)
	obs_data_set_default_string(font, "face", "Segoe UI");
#else
	obs_data_set_default_string(font, "face", "Sans Serif");
#endif
	obs_data_set_default_string(font, "style", "Regular");
	obs_data_set_default_int(font, "size", 46);
	obs_data_set_default_int(font, "flags", 0);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);
	obs_data_set_default_int(settings, "title_size", 30);
	obs_data_set_default_int(settings, "text_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "title_color", 0xFF4EC1F2);
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
}

obs_properties_t *getProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(props, "content", obs_module_text("Content"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Content.All"), ShowAll);
	obs_property_list_add_int(p, obs_module_text("Content.Scripture"), ShowScripture);
	obs_property_list_add_int(p, obs_module_text("Content.Lyrics"), ShowLyrics);

	p = obs_properties_add_list(props, "layout", obs_module_text("Layout"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Layout.LowerThird"), LowerThird);
	obs_property_list_add_int(p, obs_module_text("Layout.FullScreen"), FullScreen);
	obs_property_list_add_int(p, obs_module_text("Layout.Top"), Top);

	obs_properties_add_bool(props, "sample", obs_module_text("Sample"));
	obs_properties_add_font(props, "font", obs_module_text("Font"));
	obs_properties_add_int_slider(props, "title_size", obs_module_text("TitleSize"), 12, 120, 1);
	obs_properties_add_color(props, "text_color", obs_module_text("TextColor"));
	obs_properties_add_color(props, "title_color", obs_module_text("TitleColor"));
	obs_properties_add_color_alpha(props, "box_color", obs_module_text("BoxColor"));

	p = obs_properties_add_list(props, "align", obs_module_text("Align"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Align.Left"), 0);
	obs_property_list_add_int(p, obs_module_text("Align.Center"), 1);

	obs_properties_add_bool(props, "show_title", obs_module_text("ShowTitle"));
	p = obs_properties_add_list(props, "title_position", obs_module_text("TitlePosition"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("TitlePosition.Above"), 0);
	obs_property_list_add_int(p, obs_module_text("TitlePosition.Below"), 1);
	obs_properties_add_bool(props, "upper_title", obs_module_text("UpperTitle"));
	obs_properties_add_bool(props, "shadow", obs_module_text("Shadow"));

	obs_properties_add_int_slider(props, "box_width", obs_module_text("BoxWidth"), 30, 100, 1);
	obs_properties_add_int_slider(props, "max_lines", obs_module_text("MaxLines"), 1, 10, 1);
	obs_properties_add_int(props, "margin", obs_module_text("Margin"), 0, 600, 2);
	obs_properties_add_int(props, "padding", obs_module_text("Padding"), 0, 200, 2);
	obs_properties_add_int(props, "radius", obs_module_text("Radius"), 0, 200, 1);
	obs_properties_add_int_slider(props, "fade_ms", obs_module_text("FadeMs"), 0, 2000, 50);
	return props;
}

LiveContent sampleContent(int filter)
{
	LiveContent c;
	c.visible = true;
	if (filter == ShowLyrics) {
		c.kind = ContentKind::Lyrics;
		c.title = "Chorus";
		c.body = "Great is Thy faithfulness\nMorning by morning new mercies I see";
	} else {
		c.kind = ContentKind::Scripture;
		c.title = "John 3:16 (KJV)";
		c.body = "For God so loved the world, that he gave his only begotten Son, that whosoever believeth "
			 "in him should not perish, but have everlasting life.";
	}
	return c;
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
	uint64_t rev = liveRevision();
	if (rev == d->seenRevision && !d->dirty)
		return;
	const bool animate = rev != d->seenRevision;
	LiveContent c = getLive(&d->seenRevision);
	d->dirty = false;

	bool show = c.visible && c.kind != ContentKind::None;
	if (show && d->style.filter == ShowScripture && c.kind != ContentKind::Scripture)
		show = false;
	if (show && d->style.filter == ShowLyrics && c.kind != ContentKind::Lyrics)
		show = false;
	if (!show && d->style.sample) {
		c = sampleContent(d->style.filter);
		show = true;
	}

	Card next;
	if (show) {
		int x = 0, y = 0;
		QImage img = renderImage(c, d->style, d->canvasW, d->canvasH, x, y);
		next = makeCard(img, x, y);
	}

	obs_enter_graphics();
	if (animate && d->style.fadeMs > 0) {
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
	if (d->changeTs && d->style.fadeMs > 0) {
		double elapsed = (double)(os_gettime_ns() - d->changeTs) / 1e6;
		t = (float)std::min(1.0, elapsed / d->style.fadeMs);
	}
	float e = 1.0f - (1.0f - t) * (1.0f - t); // ease-out

	const bool prevSrgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	if (t < 1.0f)
		drawCard(d, d->prev, 1.0f - e, 0.0f);
	drawCard(d, d->cur, e, (1.0f - e) * 14.0f);

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
