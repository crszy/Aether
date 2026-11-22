import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.noctalia.Commons

// Caelestia for Windows: Noctalia's lock-screen authentication without PAM. The password is checked by
// Quickshell.Windows' PasswordCheck - the same check Caelestia's lock uses: your Caelestia lock PIN when one is
// set (Settings > Windows > Lock screen), otherwise your Windows password. Same API as the original LockContext.
Scope {
  id: root
  signal unlocked
  signal failed

  property string currentText: ""
  property bool waitingForPassword: false
  property bool unlockInProgress: false
  property bool showFailure: false
  property bool showInfo: false
  property string errorMessage: ""
  property string infoMessage: ""

  readonly property string pamConfigDirectory: ""
  property string pamConfig: "windows"
  readonly property bool pamReady: true

  onShowInfoChanged: if (showInfo) showFailure = false
  onShowFailureChanged: if (showFailure) showInfo = false
  onCurrentTextChanged: if (currentText !== "") {
    showInfo = false;
    showFailure = false;
  }

  function tryUnlock() {
    if (unlockInProgress)
      return;
    unlockInProgress = true;
    Win.PasswordCheck.verify(currentText);
  }

  Connections {
    target: Win.PasswordCheck
    function onFinished(ok, message) {
      if (!root.unlockInProgress)
        return;
      root.unlockInProgress = false;
      if (ok) {
        root.unlocked();
      } else {
        root.currentText = "";
        root.errorMessage = message || I18n.tr("authentication.failed");
        root.showFailure = true;
        root.failed();
      }
    }
  }
}
