// Word & Song for OBS — theme editor with live preview (like ProPresenter's theme editor).
#include "theme-editor.hpp"

#include "lower-third-source.hpp"
#include "themes.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QImageReader>
#include <QPainter>

#include <cstring>
#include <utility>

namespace ws {
namespace {

QString T(const char *s)
{
	return QString::fromUtf8(obs_module_text(s));
}

QColor fromAbgr(long long c)
{
	uint32_t v = (uint32_t)c;
	return QColor((int)(v & 0xFF), (int)((v >> 8) & 0xFF), (int)((v >> 16) & 0xFF), (int)((v >> 24) & 0xFF));
}

long long toAbgr(const QColor &c)
{
	return (long long)(((uint32_t)c.alpha() << 24) | ((uint32_t)c.blue() << 16) | ((uint32_t)c.green() << 8) |
			   (uint32_t)c.red());
}

QSpinBox *spin(QWidget *parent, int lo, int hi, const QString &suffix = QString())
{
	auto *s = new QSpinBox(parent);
	s->setRange(lo, hi);
	s->setSuffix(suffix);
	return s;
}

} // namespace

ThemeEditor::ThemeEditor(const std::string &startTheme, ContentKind previewKind, QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("Editor.Title"));
	resize(1240, 760);
	setMinimumSize(1100, 640);
	auto *root = new QHBoxLayout(this);

	// Left: the theme list
	auto *left = new QVBoxLayout();
	auto *listLabel = new QLabel(T("Editor.Themes"), this);
	listLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
	left->addWidget(listLabel);
	list_ = new QListWidget(this);
	list_->setFixedWidth(230);
	left->addWidget(list_, 1);
	auto *listBtns = new QHBoxLayout();
	auto *dupBtn = new QPushButton(T("Editor.Duplicate"), this);
	dupBtn->setToolTip(T("Editor.DuplicateTip"));
	renameBtn_ = new QPushButton(T("Editor.Rename"), this);
	deleteBtn_ = new QPushButton(T("Editor.Delete"), this);
	listBtns->addWidget(dupBtn);
	listBtns->addWidget(renameBtn_);
	listBtns->addWidget(deleteBtn_);
	left->addLayout(listBtns);
	root->addLayout(left);

	// Right: preview + controls
	auto *right = new QVBoxLayout();
	preview_ = new QLabel(this);
	preview_->setFixedSize(576, 324);
	preview_->setAlignment(Qt::AlignCenter);
	preview_->setStyleSheet(QStringLiteral("background:#111;border-radius:4px;"));
	auto *previewRow = new QHBoxLayout();
	previewRow->addStretch(1);
	previewRow->addWidget(preview_);
	previewRow->addStretch(1);
	right->addLayout(previewRow);
	auto *kindRow = new QHBoxLayout();
	kindRow->addWidget(new QLabel(T("Editor.PreviewWith"), this));
	previewKind_ = new QComboBox(this);
	previewKind_->addItem(T("Tab.Scripture"), (int)ContentKind::Scripture);
	previewKind_->addItem(T("Tab.Lyrics"), (int)ContentKind::Lyrics);
	previewKind_->addItem(T("Tab.Cards"), (int)ContentKind::Card);
	previewKind_->setCurrentIndex(std::max(0, previewKind_->findData((int)previewKind)));
	kindRow->addWidget(previewKind_);
	kindRow->addStretch(1);
	right->addLayout(kindRow);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *panel = new QWidget(scroll);
	auto *cols = new QHBoxLayout(panel);
	auto *f1 = new QFormLayout();
	auto *f2 = new QFormLayout();
	cols->addLayout(f1, 1);
	cols->addSpacing(16);
	cols->addLayout(f2, 1);
	for (QFormLayout *f : {f1, f2}) {
		f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}
	auto *panelOuter = new QWidget(scroll);
	auto *outerV = new QVBoxLayout(panelOuter);
	outerV->setContentsMargins(0, 0, 0, 0);
	panel->setParent(panelOuter);
	outerV->addWidget(panel);
	outerV->addWidget(buildImagesBox(panelOuter));
	outerV->addStretch(1);
	scroll->setWidget(panelOuter);
	right->addWidget(scroll, 1);

	style_ = new QComboBox(panel);
	style_->addItem(T("Style.Card"), StyleCard);
	style_->addItem(T("Style.Bar"), StyleBar);
	style_->addItem(T("Style.Tag"), StyleTag);
	style_->addItem(T("Style.Clean"), StyleClean);
	style_->addItem(T("Style.Split"), StyleSplit);
	style_->addItem(T("Style.Image"), StyleImage);
	face_ = new QFontComboBox(panel);
	face_->setMinimumContentsLength(12);
	face_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	auto *fontStyleRow = new QHBoxLayout();
	bold_ = new QCheckBox(T("Editor.Bold"), panel);
	italic_ = new QCheckBox(T("Editor.Italic"), panel);
	fontStyleRow->addWidget(bold_);
	fontStyleRow->addWidget(italic_);
	fontStyleRow->addStretch(1);
	bodySize_ = spin(panel, 12, 200, QStringLiteral(" px"));
	titleSize_ = spin(panel, 8, 140, QStringLiteral(" px"));
	textColor_ = new QPushButton(panel);
	titleColor_ = new QPushButton(panel);
	accentColor_ = new QPushButton(panel);
	boxColor_ = new QPushButton(panel);
	f1->addRow(T("Style"), style_);
	f1->addRow(T("Font"), face_);
	f1->addRow(QString(), fontStyleRow);
	f1->addRow(T("Editor.TextSize"), bodySize_);
	f1->addRow(T("TitleSize"), titleSize_);
	f1->addRow(T("TextColor"), textColor_);
	f1->addRow(T("TitleColor"), titleColor_);
	f1->addRow(T("AccentColor"), accentColor_);
	f1->addRow(T("BoxColor"), boxColor_);

	align_ = new QComboBox(panel);
	align_->addItem(T("Align.Left"), 0);
	align_->addItem(T("Align.Center"), 1);
	showTitle_ = new QCheckBox(T("ShowTitle"), panel);
	titlePos_ = new QComboBox(panel);
	titlePos_->addItem(T("TitlePosition.Above"), 0);
	titlePos_->addItem(T("TitlePosition.Below"), 1);
	upper_ = new QCheckBox(T("UpperTitle"), panel);
	shadow_ = new QCheckBox(T("Shadow"), panel);
	width_ = new QSlider(Qt::Horizontal, panel);
	width_->setRange(30, 100);
	auto *widthLabel = new QLabel(panel);
	auto *widthRow = new QHBoxLayout();
	widthRow->addWidget(width_, 1);
	widthRow->addWidget(widthLabel);
	connect(width_, &QSlider::valueChanged, widthLabel,
		[widthLabel](int v) { widthLabel->setText(QStringLiteral("%1%").arg(v)); });
	maxLines_ = spin(panel, 1, 10);
	margin_ = spin(panel, 0, 600, QStringLiteral(" px"));
	padding_ = spin(panel, 0, 200, QStringLiteral(" px"));
	radius_ = spin(panel, 0, 200, QStringLiteral(" px"));
	fade_ = spin(panel, 0, 3000, QStringLiteral(" ms"));
	fade_->setSingleStep(50);
	f2->addRow(T("Align"), align_);
	f2->addRow(QString(), showTitle_);
	f2->addRow(T("TitlePosition"), titlePos_);
	f2->addRow(QString(), upper_);
	f2->addRow(QString(), shadow_);
	f2->addRow(T("BoxWidth"), widthRow);
	f2->addRow(T("MaxLines"), maxLines_);
	f2->addRow(T("Margin"), margin_);
	f2->addRow(T("Padding"), padding_);
	f2->addRow(T("Radius"), radius_);
	f2->addRow(T("FadeMs"), fade_);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setStyleSheet(QStringLiteral("color:#f2c14e;"));
	right->addWidget(status_);
	auto *buttons = new QHBoxLayout();
	auto *saveAs = new QPushButton(T("Editor.SaveAs"), this);
	saveBtn_ = new QPushButton(T("Editor.Save"), this);
	saveBtn_->setDefault(true);
	auto *close = new QPushButton(T("Editor.Close"), this);
	buttons->addStretch(1);
	buttons->addWidget(saveAs);
	buttons->addWidget(saveBtn_);
	buttons->addWidget(close);
	right->addLayout(buttons);
	root->addLayout(right, 1);

	// Any control change updates the working look and the preview.
	for (QComboBox *c : {style_, align_, titlePos_})
		connect(c, &QComboBox::currentIndexChanged, this, &ThemeEditor::onControlChanged);
	connect(face_, &QFontComboBox::currentFontChanged, this, &ThemeEditor::onControlChanged);
	for (QCheckBox *c : {bold_, italic_, showTitle_, upper_, shadow_})
		connect(c, &QCheckBox::toggled, this, &ThemeEditor::onControlChanged);
	for (QSpinBox *s : {bodySize_, titleSize_, maxLines_, margin_, padding_, radius_, fade_})
		connect(s, &QSpinBox::valueChanged, this, &ThemeEditor::onControlChanged);
	connect(width_, &QSlider::valueChanged, this, &ThemeEditor::onControlChanged);
	for (QComboBox *c : {bgFit_, logoPos_, textV_})
		connect(c, &QComboBox::currentIndexChanged, this, &ThemeEditor::onControlChanged);
	for (QSpinBox *sp : {logoSize_, textX_, textY_, textW_, textH_})
		connect(sp, &QSpinBox::valueChanged, this, &ThemeEditor::onControlChanged);
	connect(textColor_, &QPushButton::clicked, this, [this] { pickColor(textColor_, "text_color", false); });
	connect(titleColor_, &QPushButton::clicked, this, [this] { pickColor(titleColor_, "title_color", false); });
	connect(accentColor_, &QPushButton::clicked, this, [this] { pickColor(accentColor_, "accent_color", false); });
	connect(boxColor_, &QPushButton::clicked, this, [this] { pickColor(boxColor_, "box_color", true); });
	connect(previewKind_, &QComboBox::currentIndexChanged, this, &ThemeEditor::updatePreview);

	connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0 || loading_)
			return;
		std::string id = list_->item(row)->data(Qt::UserRole).toString().toStdString();
		if (id == currentId_)
			return;
		if (!confirmDiscard()) {
			QSignalBlocker b(list_);
			reloadList(currentId_);
			return;
		}
		loadTheme(id);
	});
	connect(dupBtn, &QPushButton::clicked, this, &ThemeEditor::duplicate);
	connect(renameBtn_, &QPushButton::clicked, this, &ThemeEditor::renameTheme);
	connect(deleteBtn_, &QPushButton::clicked, this, &ThemeEditor::deleteTheme);
	connect(saveBtn_, &QPushButton::clicked, this, [this] { save(); });
	connect(saveAs, &QPushButton::clicked, this, [this] { saveAsNew(); });
	connect(close, &QPushButton::clicked, this, &ThemeEditor::reject);

	reloadList(startTheme);
	loadTheme(startTheme.empty() ? "midnight" : startTheme);
}

ThemeEditor::~ThemeEditor()
{
	obs_data_release(working_);
}

void ThemeEditor::reloadList(const std::string &select)
{
	loading_ = true;
	list_->clear();
	for (const ThemeEntry &t : ThemeLibrary::instance().list()) {
		QString label = QString::fromStdString(t.name);
		if (t.builtIn)
			label += QStringLiteral("  ") + T("Editor.BuiltIn");
		auto *it = new QListWidgetItem(label, list_);
		it->setData(Qt::UserRole, QString::fromStdString(t.id));
		if (t.id == select)
			list_->setCurrentItem(it);
	}
	loading_ = false;
}

void ThemeEditor::loadTheme(const std::string &id)
{
	ThemeEntry t;
	if (!ThemeLibrary::instance().find(id, t) && !ThemeLibrary::instance().find("midnight", t))
		return;
	obs_data_release(working_);
	working_ = obs_data_create_from_json(t.json.c_str());
	if (!working_)
		working_ = obs_data_create();
	applyLookDefaults(working_);
	currentId_ = t.id;
	currentBuiltIn_ = t.builtIn;
	dirty_ = false;
	{
		QSignalBlocker b(list_);
		for (int i = 0; i < list_->count(); i++)
			if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == t.id)
				list_->setCurrentRow(i);
	}
	renameBtn_->setEnabled(!t.builtIn);
	deleteBtn_->setEnabled(!t.builtIn);
	status_->setText(t.builtIn ? T("Editor.BuiltInNote") : QString());
	loadControls();
	updatePreview();
}

void ThemeEditor::setColorButton(QPushButton *b, long long abgr)
{
	QColor c = fromAbgr(abgr);
	QColor solid = c;
	solid.setAlpha(255);
	QString text = c.alpha() < 255
			       ? QStringLiteral("%1  %2%").arg(solid.name().toUpper()).arg(c.alpha() * 100 / 255)
			       : solid.name().toUpper();
	b->setText(text);
	bool light = solid.lightness() > 140;
	b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid #555;padding:4px 10px;}")
				 .arg(solid.name(), light ? QStringLiteral("#111") : QStringLiteral("#fff")));
	b->setProperty("abgr", (qlonglong)abgr);
}

void ThemeEditor::loadControls()
{
	loading_ = true;
	obs_data_t *font = obs_data_get_obj(working_, "font");
	if (!font)
		font = obs_data_get_default_obj(working_, "font");
	QString face = font ? QString::fromUtf8(obs_data_get_string(font, "face")) : QString();
	int size = font ? (int)obs_data_get_int(font, "size") : 46;
	long long flags = font ? obs_data_get_int(font, "flags") : 0;
	obs_data_release(font);
	if (face.isEmpty()) {
		obs_data_t *def = obs_data_get_default_obj(working_, "font");
		face = def ? QString::fromUtf8(obs_data_get_string(def, "face")) : QString();
		obs_data_release(def);
	}
	face_->setCurrentFont(QFont(face));
	bodySize_->setValue(size > 0 ? size : 46);
	bold_->setChecked(flags & OBS_FONT_BOLD);
	italic_->setChecked(flags & OBS_FONT_ITALIC);

	style_->setCurrentIndex(std::max(0, style_->findData((int)obs_data_get_int(working_, "style"))));
	titleSize_->setValue((int)obs_data_get_int(working_, "title_size"));
	setColorButton(textColor_, obs_data_get_int(working_, "text_color"));
	setColorButton(titleColor_, obs_data_get_int(working_, "title_color"));
	setColorButton(accentColor_, obs_data_get_int(working_, "accent_color"));
	setColorButton(boxColor_, obs_data_get_int(working_, "box_color"));
	align_->setCurrentIndex(obs_data_get_int(working_, "align") == 1 ? 1 : 0);
	showTitle_->setChecked(obs_data_get_bool(working_, "show_title"));
	titlePos_->setCurrentIndex(obs_data_get_int(working_, "title_position") == 1 ? 1 : 0);
	upper_->setChecked(obs_data_get_bool(working_, "upper_title"));
	shadow_->setChecked(obs_data_get_bool(working_, "shadow"));
	width_->setValue((int)obs_data_get_int(working_, "box_width"));
	maxLines_->setValue((int)obs_data_get_int(working_, "max_lines"));
	margin_->setValue((int)obs_data_get_int(working_, "margin"));
	padding_->setValue((int)obs_data_get_int(working_, "padding"));
	radius_->setValue((int)obs_data_get_int(working_, "radius"));
	fade_->setValue((int)obs_data_get_int(working_, "fade_ms"));
	bgFit_->setCurrentIndex(obs_data_get_int(working_, "bg_fit") == 1 ? 1 : 0);
	logoSize_->setValue((int)obs_data_get_int(working_, "logo_size"));
	logoPos_->setCurrentIndex(obs_data_get_int(working_, "logo_pos") == 1 ? 1 : 0);
	textX_->setValue((int)obs_data_get_int(working_, "text_x"));
	textY_->setValue((int)obs_data_get_int(working_, "text_y"));
	textW_->setValue((int)obs_data_get_int(working_, "text_w"));
	textH_->setValue((int)obs_data_get_int(working_, "text_h"));
	textV_->setCurrentIndex(std::clamp((int)obs_data_get_int(working_, "text_valign"), 0, 2));
	updateImageLabels();
	loading_ = false;
	onControlChanged(); // writes a complete font object, keeps dirty_ unchanged
	dirty_ = false;
}

void ThemeEditor::onControlChanged()
{
	if (loading_ || !working_)
		return;
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", face_->currentFont().family().toUtf8().constData());
	obs_data_set_string(font, "style", "Regular");
	obs_data_set_int(font, "size", bodySize_->value());
	obs_data_set_int(font, "flags",
			 (bold_->isChecked() ? OBS_FONT_BOLD : 0) | (italic_->isChecked() ? OBS_FONT_ITALIC : 0));
	obs_data_set_obj(working_, "font", font);
	obs_data_release(font);
	obs_data_set_int(working_, "style", style_->currentData().toInt());
	obs_data_set_int(working_, "title_size", titleSize_->value());
	obs_data_set_int(working_, "text_color", textColor_->property("abgr").toLongLong());
	obs_data_set_int(working_, "title_color", titleColor_->property("abgr").toLongLong());
	obs_data_set_int(working_, "accent_color", accentColor_->property("abgr").toLongLong());
	obs_data_set_int(working_, "box_color", boxColor_->property("abgr").toLongLong());
	obs_data_set_int(working_, "align", align_->currentData().toInt());
	obs_data_set_bool(working_, "show_title", showTitle_->isChecked());
	obs_data_set_int(working_, "title_position", titlePos_->currentData().toInt());
	obs_data_set_bool(working_, "upper_title", upper_->isChecked());
	obs_data_set_bool(working_, "shadow", shadow_->isChecked());
	obs_data_set_int(working_, "box_width", width_->value());
	obs_data_set_int(working_, "max_lines", maxLines_->value());
	obs_data_set_int(working_, "margin", margin_->value());
	obs_data_set_int(working_, "padding", padding_->value());
	obs_data_set_int(working_, "radius", radius_->value());
	obs_data_set_int(working_, "fade_ms", fade_->value());
	obs_data_set_int(working_, "bg_fit", bgFit_->currentIndex());
	obs_data_set_int(working_, "logo_size", logoSize_->value());
	obs_data_set_int(working_, "logo_pos", logoPos_->currentIndex());
	obs_data_set_int(working_, "text_x", textX_->value());
	obs_data_set_int(working_, "text_y", textY_->value());
	obs_data_set_int(working_, "text_w", textW_->value());
	obs_data_set_int(working_, "text_h", textH_->value());
	obs_data_set_int(working_, "text_valign", textV_->currentIndex());
	areaRow_->setEnabled(style_->currentData().toInt() == StyleImage);
	dirty_ = true;
	saveBtn_->setText(currentBuiltIn_ ? T("Editor.SaveCopy") : T("Editor.Save"));
	updatePreview();
}

void ThemeEditor::pickColor(QPushButton *b, const char *key, bool alpha)
{
	QColor start = fromAbgr(b->property("abgr").toLongLong());
	QColorDialog::ColorDialogOptions opts;
	if (alpha)
		opts |= QColorDialog::ShowAlphaChannel;
	QColor c = QColorDialog::getColor(start, this, T("Editor.PickColor"), opts);
	if (!c.isValid())
		return;
	setColorButton(b, toAbgr(c));
	obs_data_set_int(working_, key, toAbgr(c));
	onControlChanged();
}

void ThemeEditor::updatePreview()
{
	if (!working_)
		return;
	auto kind = (ContentKind)previewKind_->currentData().toInt();
	QImage img = renderThemePreview(obs_data_get_json(working_), kind, preview_->width(), preview_->height());
	if (obs_data_get_int(working_, "style") == StyleImage) {
		// Show where the text goes so it's easy to line up with the design.
		QPainter p(&img);
		QPen pen(QColor(255, 220, 60, 220), 1, Qt::DashLine);
		p.setPen(pen);
		p.drawRect(QRectF(img.width() * textX_->value() / 100.0, img.height() * textY_->value() / 100.0,
				  img.width() * textW_->value() / 100.0 - 1,
				  img.height() * textH_->value() / 100.0 - 1));
		p.end();
	}
	preview_->setPixmap(QPixmap::fromImage(img));
}

bool ThemeEditor::save()
{
	if (currentBuiltIn_)
		return saveAsNew();
	ThemeEntry t;
	if (!ThemeLibrary::instance().find(currentId_, t))
		return saveAsNew();
	ThemeLibrary::instance().addOrReplace(t.name, ThemeLibrary::extractLook(working_));
	dirty_ = false;
	status_->setText(T("Editor.Saved"));
	return true;
}

bool ThemeEditor::saveAsNew()
{
	ThemeEntry cur;
	ThemeLibrary::instance().find(currentId_, cur);
	bool ok = false;
	QString suggestion =
		QString::fromStdString(cur.name) + (cur.builtIn ? QStringLiteral(" (mine)") : QStringLiteral(" copy"));
	QString name =
		QInputDialog::getText(this, T("Editor.SaveAs"), T("Theme.Name"), QLineEdit::Normal, suggestion, &ok)
			.trimmed();
	if (!ok || name.isEmpty())
		return false;
	for (const ThemeEntry &t : ThemeLibrary::instance().list())
		if (t.builtIn && QString::fromStdString(t.name).compare(name, Qt::CaseInsensitive) == 0) {
			QMessageBox::warning(this, T("Editor.SaveAs"), T("Editor.NameTaken"));
			return false;
		}
	std::string json = ThemeLibrary::extractLook(working_);
	std::string id = ThemeLibrary::instance().addOrReplace(name.toStdString(), json);
	reloadList(id);
	loadTheme(id);
	status_->setText(T("Editor.Saved"));
	return true;
}

void ThemeEditor::duplicate()
{
	saveAsNew();
}

void ThemeEditor::renameTheme()
{
	ThemeEntry t;
	if (currentBuiltIn_ || !ThemeLibrary::instance().find(currentId_, t))
		return;
	bool ok = false;
	QString name = QInputDialog::getText(this, T("Editor.Rename"), T("Theme.Name"), QLineEdit::Normal,
					     QString::fromStdString(t.name), &ok)
			       .trimmed();
	if (!ok || name.isEmpty() || name.toStdString() == t.name)
		return;
	std::string json = dirty_ ? ThemeLibrary::extractLook(working_) : t.json;
	std::string id = ThemeLibrary::instance().addOrReplace(name.toStdString(), json);
	if (id != t.id)
		ThemeLibrary::instance().remove(t.id);
	reloadList(id);
	loadTheme(id);
}

void ThemeEditor::deleteTheme()
{
	ThemeEntry t;
	if (currentBuiltIn_ || !ThemeLibrary::instance().find(currentId_, t))
		return;
	if (QMessageBox::question(this, T("Editor.Delete"),
				  T("Editor.DeleteConfirm").arg(QString::fromStdString(t.name))) != QMessageBox::Yes)
		return;
	ThemeLibrary::instance().remove(t.id);
	dirty_ = false;
	reloadList("midnight");
	loadTheme("midnight");
}

bool ThemeEditor::confirmDiscard()
{
	if (!dirty_)
		return true;
	auto r = QMessageBox::question(this, T("Editor.Title"), T("Editor.Unsaved"),
				       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
	if (r == QMessageBox::Save)
		return save();
	if (r == QMessageBox::Discard) {
		dirty_ = false;
		return true;
	}
	return false;
}

void ThemeEditor::reject()
{
	if (confirmDiscard())
		QDialog::reject();
}

QWidget *ThemeEditor::buildImagesBox(QWidget *parent)
{
	auto *box = new QGroupBox(T("Img.Section"), parent);
	auto *grid = new QGridLayout(box);
	grid->setColumnStretch(3, 1);
	auto row = [&](int r, const QString &label, const char *key, QLabel *&nameLabel) {
		grid->addWidget(new QLabel(label, box), r, 0, Qt::AlignRight);
		auto *choose = new QPushButton(T("Img.Choose"), box);
		auto *clear = new QPushButton(QStringLiteral("✕"), box);
		clear->setToolTip(T("Img.Clear"));
		clear->setFixedWidth(32);
		nameLabel = new QLabel(box);
		nameLabel->setStyleSheet(QStringLiteral("color:#aaa;"));
		std::string k = key;
		connect(choose, &QPushButton::clicked, this, [this, k] { chooseImage(k.c_str()); });
		connect(clear, &QPushButton::clicked, this, [this, k] { clearImage(k.c_str()); });
		grid->addWidget(choose, r, 1);
		grid->addWidget(clear, r, 2);
		grid->addWidget(nameLabel, r, 3);
	};
	row(0, T("Img.Background"), "bg_image", bgLabel_);
	bgFit_ = new QComboBox(box);
	bgFit_->addItem(T("Img.FitCover"));
	bgFit_->addItem(T("Img.FitStretch"));
	grid->addWidget(bgFit_, 0, 4);
	auto *tintNote = new QLabel(T("Img.TintNote"), box);
	tintNote->setStyleSheet(QStringLiteral("color:#888;font-size:11px;"));
	grid->addWidget(tintNote, 1, 1, 1, 4);

	row(2, T("Img.Logo"), "logo_image", logoLabel_);
	auto *logoOpts = new QHBoxLayout();
	logoSize_ = spin(box, 16, 400, QStringLiteral(" px"));
	logoPos_ = new QComboBox(box);
	logoPos_->addItem(T("Align.Left"));
	logoPos_->addItem(T("Img.Right"));
	logoOpts->addWidget(logoSize_);
	logoOpts->addWidget(logoPos_);
	grid->addLayout(logoOpts, 2, 4);

	row(3, T("Img.Frame"), "frame_image", frameLabel_);
	areaRow_ = new QWidget(box);
	auto *area = new QHBoxLayout(areaRow_);
	area->setContentsMargins(0, 0, 0, 0);
	textX_ = spin(areaRow_, 0, 100, QStringLiteral("%"));
	textY_ = spin(areaRow_, 0, 100, QStringLiteral("%"));
	textW_ = spin(areaRow_, 1, 100, QStringLiteral("%"));
	textH_ = spin(areaRow_, 1, 100, QStringLiteral("%"));
	textV_ = new QComboBox(areaRow_);
	textV_->addItem(T("Img.VTop"));
	textV_->addItem(T("Img.VCenter"));
	textV_->addItem(T("Img.VBottom"));
	area->addWidget(new QLabel(T("Img.TextArea"), areaRow_));
	for (auto [lbl, sp] : {std::pair{"X", textX_}, {"Y", textY_}, {"W", textW_}, {"H", textH_}}) {
		area->addWidget(new QLabel(QString::fromLatin1(lbl), areaRow_));
		area->addWidget(sp);
	}
	area->addWidget(textV_);
	area->addStretch(1);
	grid->addWidget(areaRow_, 4, 1, 1, 4);
	auto *frameNote = new QLabel(T("Img.FrameNote"), box);
	frameNote->setWordWrap(true);
	frameNote->setStyleSheet(QStringLiteral("color:#888;font-size:11px;"));
	grid->addWidget(frameNote, 5, 1, 1, 4);
	return box;
}

void ThemeEditor::updateImageLabels()
{
	auto name = [this](const char *key) {
		QString v = QString::fromUtf8(obs_data_get_string(working_, key));
		return v.isEmpty() ? T("Img.None") : QFileInfo(v).fileName();
	};
	bgLabel_->setText(name("bg_image"));
	logoLabel_->setText(name("logo_image"));
	frameLabel_->setText(name("frame_image"));
	areaRow_->setEnabled(obs_data_get_int(working_, "style") == StyleImage);
}

void ThemeEditor::chooseImage(const char *key)
{
	QString file = QFileDialog::getOpenFileName(
		this, T("Img.Choose"), QString(),
		T("Graphics.Filter") + QStringLiteral(" (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"));
	if (file.isEmpty())
		return;
	if (QImageReader(file).size().isEmpty()) {
		QMessageBox::warning(this, T("Img.Choose"), T("Img.Unreadable"));
		return;
	}
	// Keep our own copy so the theme survives the original being moved or deleted.
	QString dir = QString::fromStdString(ThemeLibrary::instance().assetsDir());
	QDir().mkpath(dir);
	QFileInfo src(file);
	QString dest = QDir(dir).filePath(src.fileName());
	for (int n = 2; QFileInfo::exists(dest); n++) {
		QFile a(file), b(dest);
		if (a.size() == QFileInfo(dest).size() && a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) &&
		    a.readAll() == b.readAll())
			break; // same image already imported
		dest = QDir(dir).filePath(
			QStringLiteral("%1-%2.%3").arg(src.completeBaseName()).arg(n).arg(src.suffix()));
	}
	if (!QFileInfo::exists(dest) && !QFile::copy(file, dest)) {
		QMessageBox::warning(this, T("Img.Choose"), T("Img.CopyFailed"));
		return;
	}
	std::string ref = "theme-assets/" + QFileInfo(dest).fileName().toStdString();
	obs_data_set_string(working_, key, ref.c_str());
	if (strcmp(key, "frame_image") == 0 && style_->currentData().toInt() != StyleImage)
		style_->setCurrentIndex(style_->findData(StyleImage)); // a design implies the designed style
	updateImageLabels();
	onControlChanged();
}

void ThemeEditor::clearImage(const char *key)
{
	obs_data_set_string(working_, key, "");
	updateImageLabels();
	onControlChanged();
}

} // namespace ws
