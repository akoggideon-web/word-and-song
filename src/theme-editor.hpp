// Word & Song for OBS — theme editor with live preview (like ProPresenter's theme editor).
#pragma once

#include "live-state.hpp"

#include <QDialog>

#include <string>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
struct obs_data;

namespace ws {

class ThemeEditor : public QDialog {
	Q_OBJECT
public:
	// startTheme: theme to open first. previewKind: what the preview shows first.
	ThemeEditor(const std::string &startTheme, ContentKind previewKind, QWidget *parent = nullptr);
	~ThemeEditor() override;

	// The theme that was selected when the editor closed (e.g. a newly saved one).
	std::string selectedTheme() const { return currentId_; }

private:
	void reloadList(const std::string &select);
	void loadTheme(const std::string &id);
	void loadControls();
	void onControlChanged();
	void updatePreview();
	bool save();
	bool saveAsNew();
	void duplicate();
	void renameTheme();
	void deleteTheme();
	bool confirmDiscard();
	void reject() override;
	void setColorButton(QPushButton *b, long long abgr);
	void pickColor(QPushButton *b, const char *key, bool alpha);

	obs_data *working_ = nullptr; // the look being edited
	std::string currentId_;
	bool currentBuiltIn_ = false;
	bool dirty_ = false;
	bool loading_ = false;

	QListWidget *list_ = nullptr;
	QLabel *preview_ = nullptr;
	QComboBox *previewKind_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *saveBtn_ = nullptr;
	QPushButton *deleteBtn_ = nullptr;
	QPushButton *renameBtn_ = nullptr;

	QComboBox *style_ = nullptr;
	QFontComboBox *face_ = nullptr;
	QCheckBox *bold_ = nullptr;
	QCheckBox *italic_ = nullptr;
	QSpinBox *bodySize_ = nullptr;
	QSpinBox *titleSize_ = nullptr;
	QPushButton *textColor_ = nullptr;
	QPushButton *titleColor_ = nullptr;
	QPushButton *accentColor_ = nullptr;
	QPushButton *boxColor_ = nullptr;
	QComboBox *align_ = nullptr;
	QCheckBox *showTitle_ = nullptr;
	QComboBox *titlePos_ = nullptr;
	QCheckBox *upper_ = nullptr;
	QCheckBox *shadow_ = nullptr;
	QSlider *width_ = nullptr;
	QSpinBox *maxLines_ = nullptr;
	QSpinBox *margin_ = nullptr;
	QSpinBox *padding_ = nullptr;
	QSpinBox *radius_ = nullptr;
	QSpinBox *fade_ = nullptr;
};

} // namespace ws
