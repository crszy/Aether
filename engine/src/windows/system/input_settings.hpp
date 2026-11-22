#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class InputSettings: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(int keyboardRepeatDelay READ keyboardRepeatDelay WRITE setKeyboardRepeatDelay NOTIFY keyboardRepeatDelayChanged);
	Q_PROPERTY(int keyboardRepeatRate READ keyboardRepeatRate WRITE setKeyboardRepeatRate NOTIFY keyboardRepeatRateChanged);
	Q_PROPERTY(bool numLockOnAtSignIn READ numLockOnAtSignIn WRITE setNumLockOnAtSignIn NOTIFY numLockOnAtSignInChanged);

	Q_PROPERTY(int pointerSpeed READ pointerSpeed WRITE setPointerSpeed NOTIFY pointerSpeedChanged);
	Q_PROPERTY(bool enhancePointerPrecision READ enhancePointerPrecision WRITE setEnhancePointerPrecision NOTIFY enhancePointerPrecisionChanged);
	Q_PROPERTY(bool leftHanded READ leftHanded WRITE setLeftHanded NOTIFY leftHandedChanged);
	Q_PROPERTY(bool hidePointerWhileTyping READ hidePointerWhileTyping WRITE setHidePointerWhileTyping NOTIFY hidePointerWhileTypingChanged);
	Q_PROPERTY(int wheelScrollLines READ wheelScrollLines WRITE setWheelScrollLines NOTIFY wheelScrollLinesChanged);
	Q_PROPERTY(bool focusFollowsMouse READ focusFollowsMouse WRITE setFocusFollowsMouse NOTIFY focusFollowsMouseChanged);
	Q_PROPERTY(bool raiseOnFocus READ raiseOnFocus WRITE setRaiseOnFocus NOTIFY raiseOnFocusChanged);
	Q_PROPERTY(int focusFollowsMouseDelayMs READ focusFollowsMouseDelayMs WRITE setFocusFollowsMouseDelayMs NOTIFY focusFollowsMouseDelayMsChanged);

	Q_PROPERTY(bool touchpadAvailable READ touchpadAvailable NOTIFY touchpadAvailableChanged);
	Q_PROPERTY(bool touchpadEnabled READ touchpadEnabled WRITE setTouchpadEnabled NOTIFY touchpadEnabledChanged);
	Q_PROPERTY(bool touchpadTapToClick READ touchpadTapToClick WRITE setTouchpadTapToClick NOTIFY touchpadTapToClickChanged);
	Q_PROPERTY(bool touchpadNaturalScroll READ touchpadNaturalScroll WRITE setTouchpadNaturalScroll NOTIFY touchpadNaturalScrollChanged);
	Q_PROPERTY(int touchpadSensitivity READ touchpadSensitivity WRITE setTouchpadSensitivity NOTIFY touchpadSensitivityChanged);

	Q_PROPERTY(QStringList cursorSchemeNames READ cursorSchemeNames NOTIFY cursorSchemeNamesChanged);
	Q_PROPERTY(QStringList cursorSchemeLabels READ cursorSchemeLabels NOTIFY cursorSchemeLabelsChanged);
	Q_PROPERTY(QString cursorSchemeName READ cursorSchemeName NOTIFY cursorSchemeNameChanged);
	Q_PROPERTY(int cursorSize READ cursorSize WRITE setCursorSize NOTIFY cursorSizeChanged);

	Q_PROPERTY(bool clientAreaAnimations READ clientAreaAnimations WRITE setClientAreaAnimations NOTIFY clientAreaAnimationsChanged);
	Q_PROPERTY(bool minimizeRestoreAnimations READ minimizeRestoreAnimations WRITE setMinimizeRestoreAnimations NOTIFY minimizeRestoreAnimationsChanged);
	// clang-format on

public:
	explicit InputSettings(QObject* parent = nullptr);

	[[nodiscard]] int keyboardRepeatDelay() const { return this->mKeyboardRepeatDelay; }
	void setKeyboardRepeatDelay(int value);

	[[nodiscard]] int keyboardRepeatRate() const { return this->mKeyboardRepeatRate; }
	void setKeyboardRepeatRate(int value);

	[[nodiscard]] bool numLockOnAtSignIn() const { return this->mNumLockOnAtSignIn; }
	void setNumLockOnAtSignIn(bool value);

	[[nodiscard]] int pointerSpeed() const { return this->mPointerSpeed; }
	void setPointerSpeed(int value);

	[[nodiscard]] bool enhancePointerPrecision() const { return this->mEnhancePointerPrecision; }
	void setEnhancePointerPrecision(bool value);

	[[nodiscard]] bool leftHanded() const { return this->mLeftHanded; }
	void setLeftHanded(bool value);

	[[nodiscard]] bool hidePointerWhileTyping() const { return this->mHidePointerWhileTyping; }
	void setHidePointerWhileTyping(bool value);

	[[nodiscard]] int wheelScrollLines() const { return this->mWheelScrollLines; }
	void setWheelScrollLines(int value);

	[[nodiscard]] bool focusFollowsMouse() const { return this->mFocusFollowsMouse; }
	void setFocusFollowsMouse(bool value);

	[[nodiscard]] bool raiseOnFocus() const { return this->mRaiseOnFocus; }
	void setRaiseOnFocus(bool value);

	[[nodiscard]] int focusFollowsMouseDelayMs() const { return this->mFocusFollowsMouseDelayMs; }
	void setFocusFollowsMouseDelayMs(int value);

	[[nodiscard]] bool touchpadAvailable() const { return this->mTouchpadAvailable; }

	[[nodiscard]] bool touchpadEnabled() const { return this->mTouchpadEnabled; }
	void setTouchpadEnabled(bool value);

	[[nodiscard]] bool touchpadTapToClick() const { return this->mTouchpadTapToClick; }
	void setTouchpadTapToClick(bool value);

	[[nodiscard]] bool touchpadNaturalScroll() const { return this->mTouchpadNaturalScroll; }
	void setTouchpadNaturalScroll(bool value);

	[[nodiscard]] int touchpadSensitivity() const { return this->mTouchpadSensitivity; }
	void setTouchpadSensitivity(int value);

	[[nodiscard]] QStringList cursorSchemeNames() const { return this->mCursorSchemeNames; }
	[[nodiscard]] QStringList cursorSchemeLabels() const { return this->mCursorSchemeLabels; }
	[[nodiscard]] QString cursorSchemeName() const { return this->mCursorSchemeName; }
	Q_INVOKABLE bool setCursorScheme(const QString& name);

	[[nodiscard]] int cursorSize() const { return this->mCursorSize; }
	void setCursorSize(int value);

	[[nodiscard]] bool clientAreaAnimations() const { return this->mClientAreaAnimations; }
	void setClientAreaAnimations(bool value);

	[[nodiscard]] bool minimizeRestoreAnimations() const { return this->mMinimizeRestoreAnimations; }
	void setMinimizeRestoreAnimations(bool value);

	Q_INVOKABLE void refresh();

signals:
	void keyboardRepeatDelayChanged();
	void keyboardRepeatRateChanged();
	void numLockOnAtSignInChanged();

	void pointerSpeedChanged();
	void enhancePointerPrecisionChanged();
	void leftHandedChanged();
	void hidePointerWhileTypingChanged();
	void wheelScrollLinesChanged();
	void focusFollowsMouseChanged();
	void raiseOnFocusChanged();
	void focusFollowsMouseDelayMsChanged();

	void touchpadAvailableChanged();
	void touchpadEnabledChanged();
	void touchpadTapToClickChanged();
	void touchpadNaturalScrollChanged();
	void touchpadSensitivityChanged();

	void cursorSchemeNamesChanged();
	void cursorSchemeLabelsChanged();
	void cursorSchemeNameChanged();
	void cursorSizeChanged();

	void clientAreaAnimationsChanged();
	void minimizeRestoreAnimationsChanged();

private:
	void refreshKeyboard();
	void refreshMouse();
	void refreshTouchpad();
	void refreshCursor();
	void refreshAnimations();
	void onSettingChanged(quintptr wparam, const QString& lparam);

	int mKeyboardRepeatDelay = 0;
	int mKeyboardRepeatRate = 0;
	bool mNumLockOnAtSignIn = false;

	int mPointerSpeed = 10;
	bool mEnhancePointerPrecision = false;
	bool mLeftHanded = false;
	bool mHidePointerWhileTyping = false;
	int mWheelScrollLines = 3;
	bool mFocusFollowsMouse = false;
	bool mRaiseOnFocus = false;
	int mFocusFollowsMouseDelayMs = 0;

	bool mTouchpadAvailable = false;
	bool mTouchpadEnabled = true;
	bool mTouchpadTapToClick = true;
	bool mTouchpadNaturalScroll = false;
	int mTouchpadSensitivity = 2;

	QStringList mCursorSchemeNames;
	QStringList mCursorSchemeLabels;
	QString mCursorSchemeName;
	int mCursorSize = 1;

	bool mClientAreaAnimations = true;
	bool mMinimizeRestoreAnimations = true;
};

} // namespace qs::windows::sys
